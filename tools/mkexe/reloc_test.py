"""Regression tests for the ELF -> LWP relocation table (host only)."""
import pathlib
import struct
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
TOOL = HERE / '../../build/tools/mkexe'


class RelocationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(['make', '-C', str(HERE)], check=True)
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.tmp.name)
        source = cls.root / 'input.s'
        source.write_text('''
.section .text.start,"ax"
.global lwp_main
lwp_main:
    call helper
    mov $datum, %eax
    ret
.section .text.helper,"ax"
helper: ret
.data
datum: .long helper
.long absolute
.bss
.space 4
.global absolute
.set absolute, 0x12345678
''')
        subprocess.run(['as', '--32', str(source), '-o', str(cls.root / 'input.o')], check=True)
        subprocess.run(['ld', '-m', 'elf_i386', '--emit-relocs', '--no-warn-rwx-segments',
                        '-T', str(HERE / 'lwp.ld'), str(cls.root / 'input.o'),
                        '-o', str(cls.root / 'input.elf')], check=True)
        cls.elf = (cls.root / 'input.elf').read_bytes()
        shoff = struct.unpack_from('<I', cls.elf, 32)[0]
        shsize, shnum = struct.unpack_from('<HH', cls.elf, 46)
        cls.sections = [struct.unpack_from('<10I', cls.elf, shoff + i * shsize)
                        for i in range(shnum)]
        cls.rel = next(s for s in cls.sections if s[1] == 9)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def convert(self, data, success=True):
        src, dst = self.root / 'case.elf', self.root / 'case.exe'
        src.write_bytes(data)
        dst.unlink(missing_ok=True)
        result = subprocess.run([str(TOOL), str(src), str(dst)], capture_output=True)
        self.assertEqual(result.returncode == 0, success, result.stderr.decode())
        if success:
            return dst.read_bytes()
        self.assertFalse(dst.exists())

    def test_table_and_two_load_bases(self):
        output = self.convert(self.elf)
        image_off, image_size = struct.unpack_from('<II', output, 20)
        reloc_off, count = struct.unpack_from('<II', output, 36)
        self.assertEqual(reloc_off, image_off + image_size)
        self.assertEqual(len(output), reloc_off + 4 * count)
        entries = list(struct.unpack_from('<' + 'I' * count, output, reloc_off))
        expected, pc32 = [], []
        for sec in self.sections:
            if sec[1] != 9:
                continue
            syms = self.sections[sec[6]]
            for offset in range(sec[4], sec[4] + sec[5], 8):
                rva, info = struct.unpack_from('<II', self.elf, offset)
                shndx = struct.unpack_from('<H', self.elf, syms[4] + (info >> 8) * 16 + 14)[0]
                if info & 255 == 1 and shndx != 0xfff1:
                    expected.append(rva)
                elif info & 255 == 2:
                    pc32.append(rva)
        self.assertEqual(entries, sorted(expected))
        self.assertTrue(pc32)
        self.assertEqual(count, 2)
        original = output[image_off:reloc_off]
        for base in (0x200000, 0x510000):
            loaded = bytearray(original)
            for rva in entries:
                value = struct.unpack_from('<I', loaded, rva)[0]
                struct.pack_into('<I', loaded, rva, value + base)
                self.assertEqual(struct.unpack_from('<I', loaded, rva)[0],
                                 struct.unpack_from('<I', original, rva)[0] + base)
            for rva in pc32:
                self.assertEqual(loaded[rva:rva+4], original[rva:rva+4])

    def test_bad_relocations(self):
        start = self.rel[4]
        info = struct.unpack_from('<I', self.elf, start + 4)[0]
        for field, value in ((start + 4, (info & ~255) | 3),
                             (start + 4, 1),
                             (start + 4, 0xffffff01),
                             (start, 0xfffffff0)):
            with self.subTest(field=field, value=value):
                damaged = bytearray(self.elf)
                struct.pack_into('<I', damaged, field, value)
                self.convert(damaged, False)

    def test_duplicate(self):
        damaged = bytearray(self.elf)
        start = self.rel[4]
        damaged[start:start+8] = damaged[start+8:start+16]
        self.convert(damaged, False)

    def test_empty_table(self):
        damaged = bytearray(self.elf)
        shoff = struct.unpack_from('<I', damaged, 32)[0]
        for i, sec in enumerate(self.sections):
            if sec[1] == 9:
                struct.pack_into('<I', damaged, shoff + i * 40 + 20, 0)
        output = self.convert(damaged)
        self.assertEqual(struct.unpack_from('<II', output, 36), (0, 0))
        off, size = struct.unpack_from('<II', output, 20)
        self.assertEqual(len(output), off + size)

    def test_truncated(self):
        self.convert(self.elf[:16], False)
        self.convert(self.elf[:-100], False)


if __name__ == '__main__':
    unittest.main()
