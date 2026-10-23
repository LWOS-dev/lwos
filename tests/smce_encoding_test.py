#!/usr/bin/env python3
"""Compare SMCE's supported numeric AT&T MOV forms with GNU as --32.
Added with AI assistance. Only uses temporary files and the host test binary.
"""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def check():
    cases = []
    groups = [('b', ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh']),
              ('w', ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']),
              ('l', ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi'])]
    memory = ['0x1234', '(%eax)', '(%esp)', '(%ebp)', '127(%ebx)',
              '128(%ebx)', '-128(%edx)', '-129(%edx)', '0(,%ecx,4)',
              '4(%esp,%edx,8)', '-16(%ebx,%ecx,2)', '(%ebp,%esi)']
    for suffix, registers in groups:
        for i, reg in enumerate(registers):
            cases += [f'mov{suffix} $-1,%{reg}', f'mov{suffix} $0x7f,%{reg}',
                      f'mov{suffix} %{reg},%{registers[(i+1) % 8]}']
        for address in memory:
            for reg in (registers[0], registers[1]):
                cases += [f'mov{suffix} {address},%{reg}', f'mov{suffix} %{reg},{address}']
            cases.append(f'mov{suffix} $7,{address}')
    cases += ['mov $123,%eax', 'mov %ax,(%ebx)', 'mov (%eax),%dh',
              'nop', '.byte 1, 02, 0xff', '.word -1, 0xabcd', '.long 0x12345678']
    actual = b''.join(bytes.fromhex(subprocess.check_output(
        [str(ROOT / 'build/tests/smce_test'), line], text=True).strip()) for line in cases)
    with tempfile.TemporaryDirectory(prefix='smce-encoding-') as directory:
        work = Path(directory)
        (work / 'input.s').write_text('.text\n.code32\n' + '\n'.join(cases) + '\n')
        subprocess.run(['as', '--32', str(work / 'input.s'), '-o', str(work / 'out.o')], check=True)
        subprocess.run(['objcopy', '-O', 'binary', '-j', '.text', str(work / 'out.o'), str(work / 'out.bin')], check=True)
        expected = (work / 'out.bin').read_bytes()
    assert actual == expected, 'SMCE encodings differ from GNU as'
    print(f'PASS: {len(cases)} AT&T cases match GNU as --32 ({len(actual)} bytes)')


if __name__ == '__main__':
    check()
