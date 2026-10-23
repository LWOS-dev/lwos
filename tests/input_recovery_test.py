#!/usr/bin/env python3
"""QEMU input recovery with IRQ1 masked, idle wait and tick wraparound.
Build with make first. Uses a temporary disk copy and replaces GHOST only there.
"""
import json
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def symbols(path):
    return {parts[2]: int(parts[0], 16)
            for line in subprocess.check_output(['nm', '-n', str(path)], text=True).splitlines()
            if len(parts := line.split()) == 3}


def check(machine):
    rm = symbols(ROOT/'bin/resman.elf')
    with tempfile.TemporaryDirectory(prefix='lwos-input-recovery-') as directory:
        work = Path(directory)
        obj, elf, exe = [work/('probe'+ext) for ext in ('.o', '.elf', '.exe')]
        subprocess.run(['gcc', '-m32', '-ffreestanding', '-fno-pie', '-fno-pic',
                        '-fno-stack-protector', '-fno-builtin', '-nostdlib', '-Os',
                        '-mno-sse', '-mno-sse2', '-mno-80387', '-fno-asynchronous-unwind-tables',
                        '-I'+str(ROOT/'lib/include'), '-DTIMER_TICKS_ADDRESS='+str(rm['timer_ticks']),
                        '-DKEYBOARD_IRQ_ADDRESS='+str(rm['kb_irq']),
                        '-DKEYBOARD_POLL_ADDRESS='+str(rm['resman_keyboard_poll']),
                        '-c', str(ROOT/'tests/input_recovery_probe.c'), '-o', str(obj)], check=True)
        libgcc = subprocess.check_output(['gcc', '-m32', '-print-libgcc-file-name'], text=True).strip()
        subprocess.run(['ld', '-m', 'elf_i386', '--no-warn-rwx-segments', '--emit-relocs', '-T', str(ROOT/'tools/mkexe/lwp.ld'),
                        str(obj), libgcc, '-o', str(elf)], check=True)
        subprocess.run([str(ROOT/'build/tools/mkexe'), str(elf), str(exe)],
                       check=True, stdout=subprocess.DEVNULL)
        probe = symbols(elf)
        disk = work/'disk.img'
        shutil.copyfile(ROOT/'lwcnc.img', disk)
        subprocess.run(['mcopy', '-o', '-i', f'{disk}@@{2048*512}', str(exe), '::/GHOST.EXE'], check=True)
        sock = work/'qmp.sock'
        guest = subprocess.Popen(['qemu-system-i386', '-machine', machine, '-accel', 'tcg', '-m', '32',
            '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
            '-drive', f'file={disk},format=raw,if=none,id=disk',
            *(['-device', 'piix3-ide,id=compat'] if machine == 'q35' else []),
            '-device', 'ide-hd,drive=disk,bus='+('compat.0' if machine == 'q35' else 'ide.0'),
            '-boot', 'c', '-qmp', f'unix:{sock},server=on,wait=off'],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                if sock.exists():break
                if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                time.sleep(.05)
            connection = socket.socket(socket.AF_UNIX)
            connection.settimeout(5)
            connection.connect(str(sock))
            stream = connection.makefile('rwb'); stream.readline()
            def qmp(command, arguments=None):
                stream.write((json.dumps({'execute':command, 'arguments':arguments or {}})+'\n').encode())
                stream.flush()
                while True:
                    reply=json.loads(stream.readline())
                    if 'return' in reply:return reply['return']
                    if 'error' in reply:raise RuntimeError(reply)
            def memory(address, size):
                file=work/'memory.bin'
                qmp('pmemsave', {'val':address,'size':size,'filename':str(file)})
                return file.read_bytes()
            def word(address):return int.from_bytes(memory(address,4),'little')
            def wait_for(predicate):
                deadline=time.monotonic()+15
                while time.monotonic()<deadline:
                    if predicate():return
                    if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                    time.sleep(.01)
                raise AssertionError('probe timeout')
            qmp('qmp_capabilities')
            task = rm['task']+168
            wait_for(lambda: word(task+28)!=0)
            base=word(task+28)
            result=base+probe['stage']
            recovered=word(rm['kb_recovered'])
            irq_count=word(rm['kb_irq_cnt'])
            for phase, key in enumerate(('a','b','c'),1):
                wait_for(lambda: word(result)==phase and word(task+4)==4)
                # Leave the task blocked and idle running before injecting input.
                time.sleep(.15)
                assert word(task+4)==4, 'unexpected wake without input'
                qmp('send-key', {'keys':[{'type':'qcode','data':key}], 'hold-time':30})
                wait_for(lambda: word(result)>phase)
                assert word(base+probe['error'])==0, 'wrong key/duplicate input'
            wait_for(lambda: word(task+4)==5)
            assert word(task+52)==42, 'probe failed'
            assert word(rm['kb_recovered'])>recovered, 'no fallback input'
            assert word(rm['kb_irq_cnt'])>irq_count, 'normal IRQ input failed'
            assert word(rm['kb_dropped'])==0, 'input dropped'
            assert word(rm['pic_spurious7'])>=1 and word(rm['pic_spurious15'])>=1
            print(f'PASS: {machine}: spurious IRQ7/15; mixed/error bytes; bounded drain; IRQ1 masked recovery; idle wake; normal IRQ; tick wrap; no duplicates',flush=True)
            qmp('quit'); guest.wait(timeout=3)
            stream.close(); connection.close()
        finally:
            if guest.poll() is None:guest.terminate();guest.wait(timeout=3)

if __name__=='__main__':
    for machine in ('pc','q35'):check(machine)
