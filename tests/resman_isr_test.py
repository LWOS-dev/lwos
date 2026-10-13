#!/usr/bin/env python3
"""QEMU snapshots: check IDT ownership, common-stub return, #UD and #GP frames."""
import re
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent.parent


def symbols(name):
    return {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ['nm', '-n', str(ROOT / 'bin' / (name + '.elf'))], text=True).splitlines()
        if len(p := line.split()) == 3}


RM = symbols('resman')
MON = symbols('monitor')


class Remote:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(5)
        self.socket.connect(str(path))

    def command(self, text):
        payload = text.encode()
        self.socket.sendall(b'$' + payload + b'#' + f'{sum(payload) & 255:02x}'.encode())
        def read_byte():
            byte = self.socket.recv(1)
            if not byte:
                raise EOFError('QEMU debug socket closed')
            return byte
        while read_byte() != b'$':
            pass
        result = bytearray()
        while (char := read_byte()) != b'#':
            result += char
        checksum = read_byte() + read_byte()
        assert int(checksum, 16) == sum(result) & 255
        self.socket.sendall(b'+')
        return result.decode()

    def write(self, address, data):
        assert self.command(f'M{address:x},{len(data):x}:' + data.hex()) == 'OK'

    def start(self, address):
        assert self.command('P8=' + struct.pack('<I', address).hex()) == 'OK'
        assert self.command('D') == 'OK'
        self.socket.close()


def check(machine, fault):
    with tempfile.TemporaryDirectory(prefix='lwcnc-isr-') as temporary:
        work = Path(temporary)
        qmp_path, gdb_path = work / 'qmp.sock', work / 'gdb.sock'
        args = ['qemu-system-i386', '-machine', machine, '-accel', 'tcg', '-m', '32',
                '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
                '-snapshot', '-drive', f'file={ROOT}/lwcnc.img,format=raw,if=none,id=disk',
                '-device', 'ide-hd,drive=disk,bus=ide.0', '-boot', 'c',
                '-qmp', f'unix:{qmp_path},server=on,wait=off',
                '-gdb', f'unix:{gdb_path},server=on,wait=off']
        guest = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                if qmp_path.exists():
                    break
                if guest.poll() is not None:
                    raise RuntimeError(guest.stderr.read().decode())
                time.sleep(.05)
            connection = socket.socket(socket.AF_UNIX)
            connection.settimeout(5)
            connection.connect(str(qmp_path))
            stream = connection.makefile('rwb')
            stream.readline()

            def qmp(command, arguments=None):
                stream.write((json.dumps({'execute': command, 'arguments': arguments or {}}) + '\n').encode())
                stream.flush()
                while True:
                    reply = json.loads(stream.readline())
                    if 'return' in reply:
                        return reply['return']
                    if 'error' in reply:
                        raise RuntimeError(reply)

            def memory(address, size):
                path = work / 'memory.bin'
                qmp('pmemsave', {'val': address, 'size': size, 'filename': str(path)})
                return path.read_bytes()

            qmp('qmp_capabilities')
            for _ in range(100):
                registers = qmp('human-monitor-command', {'command-line': 'info registers'})
                if any(line.split() == ['IDT=', '00370000', '000007ff']
                       for line in registers.splitlines()):
                    break
                if guest.poll() is not None:
                    raise RuntimeError('guest exited before RESMAN IDT installation')
                time.sleep(.1)
            else:
                raise RuntimeError('RESMAN IDT was not installed')
            qmp('stop')
            idt = memory(0x370000, 2048)
            old_idt = memory(MON['idt'], 48 * 8)
            entries = struct.unpack('<256I', memory(RM['resman_isr_table'], 1024))
            for vector in range(256):
                gate = idt[vector * 8:vector * 8 + 8]
                if vector in (1, 3, 8):
                    assert gate == old_idt[vector * 8:vector * 8 + 8]
                else:
                    low, selector, reserved, attr, high = struct.unpack('<HHBBH', gate)
                    assert low | high << 16 == entries[vector]
                    assert (selector, reserved, attr) == (8, 0, 0x8e)

            if fault == 'gp':
                # A test callback records the real frame and entry flags, then returns.
                # It runs only in temporary guest RAM; production dispatch is unchanged.
                callback_source = work / 'callback.s'
                callback_source.write_text('''bits 32
mov eax, [esp+4]
mov edx, [eax+48]
mov [0x380100], edx
mov edx, [eax+52]
mov [0x380104], edx
pushfd
pop edx
mov [0x380110], edx
mov edx, esp
and edx, 15
mov [0x380114], edx
ret
''')
                subprocess.run(['nasm', '-f', 'bin', str(callback_source), '-o', str(work / 'callback.bin')], check=True)
                remote = Remote(gdb_path)
                remote.write(0x380040, (work / 'callback.bin').read_bytes())
                remote.write(RM['handlers'] + 128 * 4, struct.pack('<I', 0x380040))
                # EAX/DF must survive dispatch; a high vector checks push sign extension.
                remote.write(0x380000, bytes.fromhex('b878563412fdcd80a3080138009c58a30c013800faebfe'))
                before = qmp('human-monitor-command', {'command-line': 'info registers'})
                original_esp = re.search(r'ESP=([0-9A-Fa-f]+)', before).group(1)
                remote.start(0x380000)
                qmp('cont')
                qmp('send-key', {'keys':[{'type':'qcode','data':'a'}], 'hold-time':10})
                time.sleep(.2)
                vector, error, eax, flags, handler_flags, alignment = struct.unpack('<6I', memory(0x380100, 24))
                assert (vector, error, eax) == (128, 0, 0x12345678)
                assert flags & 0x400 and not handler_flags & 0x400
                assert alignment == 12
                after = qmp('human-monitor-command', {'command-line': 'info registers'})
                assert re.search(r'ESP=([0-9A-Fa-f]+)', after).group(1) == original_esp
                qmp('stop')
                remote = Remote(gdb_path)
                # Invalid DS selector causes hardware #GP with a nonzero error code.
                remote.write(0x380000, bytes.fromhex('fc66b8f8ff8ed8faf4'))
                expected_vector, expected_error, expected_eip = 13, 0xfff8, 0x380005
            else:
                remote = Remote(gdb_path)
                remote.write(0x380000, bytes.fromhex('0f0bfaf4'))  # ud2
                expected_vector, expected_error, expected_eip = 6, 0, 0x380000
            remote.start(0x380000)
            qmp('cont')
            qmp('send-key', {'keys':[{'type':'qcode','data':'a'}], 'hold-time':10})
            time.sleep(.2)
            raw = memory(0xb8000, 640)
            rows = [raw[i * 160:(i + 1) * 160:2].decode('ascii', 'replace') for i in range(4)]
            assert rows[0].startswith('RESMAN UNHANDLED INTERRUPT'), rows
            assert rows[1].startswith(f'VECTOR={expected_vector:08X}'), rows
            assert rows[2].startswith(f'ERROR ={expected_error:08X}'), rows
            assert rows[3].startswith(f'EIP   ={expected_eip:08X}'), rows
            print(f'PASS: {machine} IDT; {fault} exception frame' + ('; common stub return/DF flag/stack alignment' if fault == 'gp' else ''), flush=True)
            qmp('quit')
            guest.wait(timeout=3)
            stream.close()
            connection.close()
        finally:
            if guest.poll() is None:
                guest.terminate()
                guest.wait(timeout=3)


if __name__ == '__main__':
    for machine in ('pc', 'q35'):
        for fault in ('ud', 'gp'):
            check(machine, fault)
