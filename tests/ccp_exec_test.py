#!/usr/bin/env python3
"""Real i386 CCP foreground execution; temporary disks only."""
import json
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path
from sleep_test import ROOT, symbols


def check(machine):
    rm=symbols(ROOT/'bin/resman.elf')
    image_bytes=next(int(parts[1],16) for line in subprocess.check_output(
        ['nm','-S',str(ROOT/'bin/resman.elf')],text=True).splitlines()
        if len(parts:=line.split())==4 and parts[3]=='images')
    with tempfile.TemporaryDirectory(prefix='lwos-ccp-') as directory:
        work=Path(directory)
        obj,elf,exe=[work/('host'+ext) for ext in ('.o','.elf','.exe')]
        subprocess.run(['gcc','-m32','-ffreestanding','-fno-pie','-fno-pic','-fno-stack-protector',
            '-fno-builtin','-nostdlib','-Os','-mno-sse','-mno-sse2','-mno-80387',
            '-fno-asynchronous-unwind-tables','-I'+str(ROOT/'lib/include'),
            '-c',str(ROOT/'tests/ccp_exec_host.c'),'-o',str(obj)],check=True)
        objects=[str(ROOT/'build/lib/app'/name) for name in ('app.o','fs.o','mem.o','string.o','bitmap.o','convert.o')]
        libgcc=subprocess.check_output(['gcc','-m32','-print-libgcc-file-name'],text=True).strip()
        subprocess.run(['ld','-m','elf_i386','--no-warn-rwx-segments','--emit-relocs',
            '-T',str(ROOT/'tools/mkexe/lwp.ld'),str(obj),*objects,libgcc,'-o',str(elf)],check=True)
        subprocess.run([str(ROOT/'build/tools/mkexe'),str(elf),str(exe)],check=True,stdout=subprocess.DEVNULL)
        child_obj=work/'child.o';child_elf=work/'child.elf';child_exe=work/'child.exe'
        subprocess.run(['gcc','-m32','-ffreestanding','-fno-pie','-fno-pic','-fno-stack-protector',
            '-fno-builtin','-nostdlib','-Os','-mno-sse','-mno-sse2','-mno-80387',
            '-fno-asynchronous-unwind-tables','-I'+str(ROOT/'lib/include'),
            '-c',str(ROOT/'tests/ccp_child.c'),'-o',str(child_obj)],check=True)
        runtime=[str(ROOT/'build/lib/app'/name) for name in ('crt0.o','stdio.o','vsprintf.o','string.o')]
        subprocess.run(['ld','-m','elf_i386','--no-warn-rwx-segments','--emit-relocs',
            '-T',str(ROOT/'tools/mkexe/lwp.ld'),str(child_obj),*runtime,libgcc,'-o',str(child_elf)],check=True)
        subprocess.run([str(ROOT/'build/tools/mkexe'),'--text',str(child_elf),str(child_exe)],
            check=True,stdout=subprocess.DEVNULL)
        probe=symbols(elf)
        disk=work/'disk.img';shutil.copyfile(ROOT/'lwcnc.img',disk)
        subprocess.run(['mcopy','-o','-i',f'{disk}@@{2048*512}',str(exe),'::/GHOST.EXE'],check=True)
        for source,target in ((ROOT/'bin/resman.bin','RESMAN.BIN'),
                              (ROOT/'bin/external/ccp.exe','CCP.EXE'),
                              (ROOT/'bin/external/hello.exe','HELLO.EXE'),
                              (child_exe,'ARGTEST.EXE')):
            subprocess.run(['mcopy','-o','-i',f'{disk}@@{2048*512}',str(source),'::/'+target],check=True)
        tool_dir=work/'BIN';tool_dir.mkdir()
        shutil.copyfile(ROOT/'bin/coreutils/ls.exe',tool_dir/'LS.EXE')
        shutil.copyfile(ROOT/'bin/coreutils/cat.exe',tool_dir/'CAT.EXE')
        shutil.copyfile(ROOT/'bin/coreutils/sed.exe',tool_dir/'SED.EXE')
        shutil.copyfile(ROOT/'bin/coreutils/ed.exe',tool_dir/'ED.EXE')
        shutil.copyfile(ROOT/'bin/coreutils/hed.exe',tool_dir/'HED.EXE')
        shutil.copyfile(ROOT/'bin/coreutils/touch.exe',tool_dir/'TOUCH.EXE')
        shutil.copyfile(ROOT/'bin/coreutils/xxd.exe',tool_dir/'XXD.EXE')
        subprocess.run(['mcopy','-s','-o','-i',f'{disk}@@{2048*512}',str(tool_dir),'::/'],check=True)
        text_file=work/'CATTEST.TXT';text_file.write_bytes(b'cat data\n'*70+b'CAT END')
        subprocess.run(['mcopy','-o','-i',f'{disk}@@{2048*512}',str(text_file),'::/CATTEST.TXT'],check=True)
        for name,contents in {
            'SEDIN.TXT':b'alpha 12\nbeta 34\nalpha 56\n',
            'SCRIPT.SED':br's/\([a-z]*\) \([0-9]*\)/\2:\1/'+b'\n',
            'EDIN.TXT':b'one\nold old\nthree\n',
            'EDOUT.TXT':b'obsolete tail that must disappear\n'*20,
            'EMPTY.TXT':b'truncate me\n',
            'HEDIN.BIN':bytes([0,255,10,65,66]),
            'HEDOUT.BIN':b'obsolete binary tail'*20,
        }.items():
            fixture=work/name;fixture.write_bytes(contents)
            subprocess.run(['mcopy','-o','-i',f'{disk}@@{2048*512}',str(fixture),'::/'+name],check=True)
        marker=work/'TESTDIR';marker.mkdir();(marker/'A.TXT').write_bytes(b'test\n')
        subprocess.run(['mcopy','-s','-o','-i',f'{disk}@@{2048*512}',str(marker),'::/'],check=True)
        sock=work/'qmp.sock'
        guest=subprocess.Popen(['qemu-system-i386','-machine',machine,'-accel','tcg','-m','32',
            '-display','none','-serial','none','-monitor','none','-no-reboot',
            '-drive',f'file={disk},format=raw,if=none,id=disk',
            *(['-device','piix3-ide,id=compat'] if machine=='q35' else []),
            '-device','ide-hd,drive=disk,bus='+('compat.0' if machine=='q35' else 'ide.0'),
            '-boot','c','-qmp',f'unix:{sock},server=on,wait=off'],
            stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                if sock.exists():break
                if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as connection:
                connection.settimeout(5);connection.connect(str(sock))
                stream=connection.makefile('rwb');stream.readline()
                def qmp(command,arguments=None):
                    stream.write((json.dumps({'execute':command,'arguments':arguments or {}})+'\n').encode());stream.flush()
                    while True:
                        reply=json.loads(stream.readline())
                        if 'return' in reply:return reply['return']
                        if 'error' in reply:raise RuntimeError(reply)
                def word(address):
                    file=work/'memory.bin'
                    qmp('pmemsave',{'val':address,'size':4,'filename':str(file)})
                    return int.from_bytes(file.read_bytes(),'little')
                qmp('qmp_capabilities')
                task=rm['task']+168
                deadline=time.monotonic()+40
                while time.monotonic()<deadline:
                    if word(task+4)==5:break
                    if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                    time.sleep(.02)
                else:raise AssertionError('CCP host timeout')
                base=word(task+28)
                if word(base+probe['error']):
                    dump=work/'screen.bin'
                    qmp('pmemsave',{'val':base+probe['snapshot']+16,'size':4000,'filename':str(dump)})
                    cells=dump.read_bytes()[::2].decode('ascii','replace')
                    print('\n'.join(cells[i:i+80] for i in range(0,2000,80)))
                assert word(base+probe['error'])==0, ('probe line',word(base+probe['error']))
                assert word(base+probe['stage'])==4 and word(task+52)==42
                for child in (2,):assert word(rm['task']+child*168+4)==0, 'unreaped child'
                for slot in range(32):
                    state=word(rm['images']+slot*(image_bytes//32)+28)>>16
                    assert state in (0,4), ('leaked child image',slot,state)
                qmp('quit');guest.wait(timeout=3)
                for name,expected in {'EDOUT.TXT':b'one\nnew new\n',
                                      'NEW.TXT':b'hello ed\n','EMPTY.TXT':b'',
                                      'SEDOUT.TXT':b'alpha 12\n',
                                      'TOUCH.TXT':b'','TESTDIR/REL.TXT':b'',
                                      'HEDIN.BIN':bytes([0,255,10,65,66]),
                                      'HEDOUT.BIN':bytes([0,127,0,222,173,190,239]),
                                      'NEWHEX.BIN':bytes([0,255,10])}.items():
                    actual=work/('saved-'+name.replace('/','-'))
                    subprocess.run(['mcopy','-o','-i',f'{disk}@@{2048*512}','::/'+name,str(actual)],check=True)
                    assert actual.read_bytes()==expected, (name,actual.read_bytes())
                absent=subprocess.run(['mdir','-i',f'{disk}@@{2048*512}','::/NOEXIST'],
                    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                assert absent.returncode!=0, 'touch unexpectedly created a parent directory'
                print(f'PASS: {machine}: touch; sed/ed; hed binary edits/undo/dirty quit/write; CCP resumes; no leaked images',flush=True)
        finally:
            if guest.poll() is None:guest.terminate();guest.wait(timeout=3)

if __name__=='__main__':
    for machine in ('pc','q35'):check(machine)
