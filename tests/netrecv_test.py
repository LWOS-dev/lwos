#!/usr/bin/env python3
"""Real e1000 INTx + LWFT + FAT writes; disposable disks, no root/TAP needed."""
import json
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import zlib
from pathlib import Path
from sleep_test import ROOT, symbols

MAC=bytes.fromhex('525400123456')
PEER=bytes.fromhex('525400654321')
MAGIC=0x5446574c

def check(machine):
    rm=symbols(ROOT/'bin/resman.elf')
    with tempfile.TemporaryDirectory(prefix='lwos-netrecv-') as directory:
        work=Path(directory)
        obj,elf,exe=[work/('host'+ext) for ext in ('.o','.elf','.exe')]
        subprocess.run(['gcc','-m32','-ffreestanding','-fno-pie','-fno-pic','-fno-stack-protector',
            '-fno-builtin','-nostdlib','-Os','-mno-sse','-mno-sse2','-mno-80387',
            '-fno-asynchronous-unwind-tables','-I'+str(ROOT/'lib/include'),
            '-c',str(ROOT/'tests/netrecv_host.c'),'-o',str(obj)],check=True)
        objects=[str(ROOT/'build/lib/app'/name) for name in ('app.o','fs.o','mem.o','string.o','bitmap.o','convert.o')]
        subprocess.run(['ld','-m','elf_i386','--no-warn-rwx-segments','--emit-relocs',
            '-T',str(ROOT/'tools/mkexe/lwp.ld'),str(obj),*objects,'-o',str(elf)],check=True)
        subprocess.run([str(ROOT/'build/tools/mkexe'),str(elf),str(exe)],check=True,stdout=subprocess.DEVNULL)
        probe=symbols(elf)
        disk=work/'disk.img';shutil.copyfile(ROOT/'lwcnc.img',disk)
        for source,target in ((exe,'GHOST.EXE'),(ROOT/'bin/resman.bin','RESMAN.BIN'),
                              (ROOT/'bin/abi.bin','ABI.BIN'),
                              (ROOT/'bin/monitor.bin','MONITOR.BIN'),
                              (ROOT/'bin/external/ccp.exe','CCP.EXE'),
                              (ROOT/'bin/coreutils/netrecv.exe','BIN/NETRECV.EXE')):
            subprocess.run(['mcopy','-o','-i',f'{disk}@@1048576',str(source),'::/'+target],check=True)
        stale=work/'stale';stale.write_bytes(b'old contents'*10000)
        for name in ('RX.BIN','EMPTY.BIN'):
            subprocess.run(['mcopy','-o','-i',f'{disk}@@1048576',str(stale),'::/'+name],check=True)
        host,device=socket.socketpair();host.settimeout(2)
        sock=work/'qmp.sock'
        guest=subprocess.Popen(['qemu-system-i386','-machine',machine,'-accel','tcg','-m','32',
            '-display','none','-serial','none','-monitor','none','-no-reboot',
            '-drive',f'file={disk},format=raw,if=none,id=disk',
            *(['-device','piix3-ide,id=compat'] if machine=='q35' else []),
            '-device','ide-hd,drive=disk,bus='+('compat.0' if machine=='q35' else 'ide.0'),
            '-netdev',f'socket,id=n0,fd={device.fileno()}',
            '-device','e1000,netdev=n0,mac=52:54:00:12:34:56',
            '-boot','c','-qmp',f'unix:{sock},server=on,wait=off'],
            pass_fds=(device.fileno(),),stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        device.close()
        try:
            for _ in range(100):
                if sock.exists():break
                if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                time.sleep(.05)
            connection=socket.socket(socket.AF_UNIX);connection.settimeout(5);connection.connect(str(sock))
            stream=connection.makefile('rwb');stream.readline()
            def qmp(command,arguments=None):
                stream.write((json.dumps({'execute':command,'arguments':arguments or {}})+'\n').encode());stream.flush()
                while True:
                    reply=json.loads(stream.readline())
                    if 'return' in reply:return reply['return']
                    if 'error' in reply:raise RuntimeError(reply)
            def memory(address,size=4):
                path=work/'memory.bin';qmp('pmemsave',{'val':address,'size':size,'filename':str(path)})
                return path.read_bytes()
            def word(address):return int.from_bytes(memory(address),'little')
            def wait(predicate):
                until=time.monotonic()+15
                while time.monotonic()<until:
                    if predicate():return
                    if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                    time.sleep(.01)
                cells=memory(0xb8000,4000)[::2]
                raise AssertionError(('guest timeout',cells.decode('ascii','replace')))
            qmp('qmp_capabilities')
            parent=rm['task']+168
            wait(lambda:word(parent+28)!=0)
            base=word(parent+28)
            def status():return word(base+probe['stage'])
            def receive_exact(n):
                result=b''
                while len(result)<n:
                    chunk=host.recv(n-len(result));assert chunk,'network EOF';result+=chunk
                return result
            def send(op,seq,arg=0,data=b'',**kwargs):
                frame=kwargs.get('dest',MAC)+kwargs.get('src',PEER)+b'\x88\xb5'+struct.pack('<IBBHII',MAGIC,op,0,len(data),seq,arg)+data
                frame=frame.ljust(60,b'\0')
                host.sendall(struct.pack('!I',len(frame))+frame)
            def ack(op,seq,arg=0,data=b'',expected=5):
                send(op,seq,arg,data)
                packet=receive_exact(struct.unpack('!I',receive_exact(4))[0])
                assert packet[:12]==PEER+MAC,packet.hex()
                h=struct.unpack_from('<IBBHII',packet,14)
                assert h[:5]==(MAGIC,expected,0,0,seq),h
                return h[5]
            payload=bytes((i*37+19)&255 for i in range(100*1024+17))
            small=b'reopened after kill\x00\xff'
            for session,data in ((1,payload),(2,b''),(3,b'bad CRC test'),(5,small)):
                wait(lambda:status()==session or word(parent+4)==5)
                assert not word(base+probe['error']),('probe line',word(base+probe['error']))
                child=word(base+probe['child_id']);task=rm['task']+child*168
                wait(lambda:word(task+4)==4)
                if session==1:
                    # With no packets, the child stays blocked, not scanning RX.
                    ticks=word(task+48);irq_before=word(rm['net_irq_count'])
                    time.sleep(.1)
                    assert word(task+4)==4 and word(task+48)==ticks,'receiver busy polling'
                    assert word(rm['net_irq_count'])==irq_before
                    send(7,0,0,b'w0034000000000000') # legacy RUN must be ignored
                    send(2,0,0,b'x',src=bytes.fromhex('535400654321')) # multicast source
                    malformed=MAC+PEER+b'\x88\xb5'+struct.pack('<IBBHII',MAGIC,2,0,1024,0,1)
                    host.sendall(struct.pack('!I',len(malformed))+malformed) # truncated payload
                assert ack(2,0,len(data),b'ignored.bin')==0
                assert ack(2,0,len(data),b'ignored.bin')==0 # dropped PUT ACK
                if session==1:ack(2,0,len(data)+1,b'ignored.bin',expected=6)
                seq=1
                for offset in range(0,len(data),1024):
                    block=data[offset:offset+1024]
                    if offset==0:
                        ack(3,seq+3,offset,block,expected=6) # out of order
                        ack(3,seq,offset+1,block,expected=6) # incorrect byte offset
                        send(3,seq,offset,block,src=bytes.fromhex('525400654322')) # wrong peer
                    assert ack(3,seq,offset,block)==offset+len(block)
                    if offset==0:assert ack(3,seq,offset,block)==len(block) # no duplicate write
                    seq+=1
                crc=zlib.crc32(data) ^ (1 if session==3 else 0)
                ack(4,seq,crc,expected=6 if session==3 else 5)
                if session!=3:assert ack(4,seq,crc)==len(data) # duplicate END during linger
                if session==3:
                    wait(lambda:status()>=4)
                    # Fourth child is killed by its host while blocked in recv.
            wait(lambda:word(parent+4)==5)
            assert not word(base+probe['error']),('probe line',word(base+probe['error']))
            assert status()==6 and word(parent+52)==42
            assert word(rm['net_irq_count'])>100
            qmp('quit');guest.wait(timeout=3)
            for name,expected in (('RX.BIN',payload),('EMPTY.BIN',b''),('BAD.BIN',b'bad CRC test'),('AGAIN.BIN',small)):
                result=work/name
                subprocess.run(['mcopy','-o','-i',f'{disk}@@1048576','::/'+name,str(result)],check=True)
                assert result.read_bytes()==expected,name
            print(f'PASS: {machine}: IRQ blocking; 100KB/empty; duplicate PUT/DATA/END; sequence/CRC; truncate; kill/reopen; timeout',flush=True)
            stream.close();connection.close()
        finally:
            host.close()
            if guest.poll() is None:guest.terminate();guest.wait(timeout=3)

if __name__=='__main__':
    for machine in ('pc','q35'):check(machine)
