#!/usr/bin/env python3
"""Regenerate xtal single-precision LUTs offline; no runtime libm required."""
import math
import struct
from pathlib import Path
p=Path(__file__).resolve().parent.parent / 'external/xtal_lut.h'
arrays=[('xtal_sin_lut',[math.sin(2*math.pi*i/1024) for i in range(1025)]),('xtal_sqrt_lut',[math.sqrt(1+3*i/768) for i in range(769)])]
for n in (2,3,5,48,52,76,96,128):
    arrays.append((f'xtal_pow{n}_lut',[(i/1024)**n for i in range(1025)]))
out=['/* Offline-generated float LUTs; linear interpolation in xtal.c. */','#ifndef XTAL_LUT_H','#define XTAL_LUT_H']
for name, values in arrays:
    out.append(f'static const float {name}[{len(values)}] = {{')
    for i in range(0,len(values),8):
        row=[]
        for v in values[i:i+8]:
            v=struct.unpack('<f',struct.pack('<f',v))[0]
            s=format(v,'.9g')
            if '.' not in s and 'e' not in s: s += '.0'
            row.append(s+'f')
        out.append('    '+', '.join(row)+',')
    out.append('};')
out.append('#endif\n')
p.write_text('\n'.join(out))
