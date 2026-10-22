#!/usr/bin/env python3
"""QEMU sleep blocking, wraparound and TSC/PIT latency measurements.
Build with make first. Uses a temporary disk copy and replaces GHOST only there.
"""
import json
import shutil
import socket
import struct
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
    with tempfile.TemporaryDirectory(prefix='lwos-sleep-') as directory:
        work = Path(directory)
        obj, elf, exe = [work/('probe'+ext) for ext in ('.o', '.elf', '.exe')]
        subprocess.run(['gcc', '-m32', '-ffreestanding', '-fno-pie', '-fno-pic',
                        '-fno-stack-protector', '-fno-builtin', '-nostdlib', '-Os',
                        '-mno-sse', '-mno-sse2', '-mno-80387', '-fno-asynchronous-unwind-tables',
                        '-I'+str(ROOT/'lib/include'), '-DTIMER_TICKS_ADDRESS='+str(rm['timer_ticks']),
                        '-c', str(ROOT/'tests/sleep_probe.c'), '-o', str(obj)], check=True)
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
            result=base+probe['results']
            wait_for(lambda: word(result)==1 and word(task+4)==4)
            assert word(rm['task']+63*168+4)==3, 'idle did not run while probe slept'
            qmp('send-key', {'keys':[{'type':'qcode','data':'a'}], 'hold-time':20})
            time.sleep(.03)
            assert word(task+4)==4 and word(result)==1, 'keyboard woke timer waiter'
            wait_for(lambda: word(task+4)==5)
            assert word(result)==4 and word(result+4)==0, memory(result,8)
            assert word(task+52)==42, word(task+52)
            data=memory(result,8+8+3*32*8+3*32*4+4)
            calibration=struct.unpack_from('<Q',data,8)[0]
            samples=struct.unpack_from('<96Q',data,16)
            delta=struct.unpack_from('<96I',data,16+96*8)
            tick_ms=1194/1193182*1000
            scale=100*tick_ms/calibration
            for group, requested in enumerate((1,5,10)):
                measured=[sample*scale for sample in samples[group*32:(group+1)*32]]
                assert min(measured)>=requested*.95, (requested,min(measured))
                print(f'{machine}: sleep({requested}) = {min(measured):.3f}..{max(measured):.3f} ms '
                      f'(32 samples; ticks {min(delta[group*32:(group+1)*32])}..{max(delta[group*32:(group+1)*32])})', flush=True)
            print(f'PASS: {machine}: blocked sleep/idle, IRQ isolation, sleep(0), IF=0, range, tick wrap', flush=True)
            qmp('quit'); guest.wait(timeout=3)
            stream.close(); connection.close()
        finally:
            if guest.poll() is None:guest.terminate();guest.wait(timeout=3)

if __name__=='__main__':
    for machine in ('pc','q35'):check(machine)
