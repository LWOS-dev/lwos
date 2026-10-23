# Vendored text tools

## minised

- Source: https://github.com/tar-mirror/minised
- Commit: `1e8ce8ae9b53aedc97a1b741b86bfc5f85b8f786` (minised 1.15).
- Files: `sed.h`, `sedcomp.c`, `sedexec.c`, upstream `README`, `minised.1`, `LICENSE`.
- License: BSD 3-clause; complete notice in `minised/LICENSE`.
- Local changes: include the LWOS compatibility layer, rename main, return
  through the parent on exit, fail on missing input/unknown options, bound the
  append queue, avoid an out-of-bounds pointer in readout, annotate fallthrough
  and correct signedness for modern warning-clean builds.

## BusyBox ed

- Source: https://github.com/mirror/busybox/blob/371fe9f71d445d18be28c82a2a6d82115c8af19d/editors/ed.c
- Commit: `371fe9f71d445d18be28c82a2a6d82115c8af19d`.
- File: `editors/ed.c`; its permissive David I. Bell notice is retained in the
  file header and reproduced in `busybox/LICENSE.ed`. No libbb sources copied.
- Local changes: replace libbb services with the local compatibility layer,
  parse the supported options locally, permit an initially missing file, save
  empty buffers, and clear dirty state after saving the complete buffer.
- This is BusyBox's simplified ed: searches/substitutions use literal strings,
  not a POSIX regex engine. It has no undo or global command.

## Platform layer

`../port/compat.*` is local code. It provides FAT-backed, buffered file I/O,
terminal line input, allocation tracking, and synchronous exit via compiler
setjmp/longjmp builtins. Closing or exiting flushes open output files and frees
allocations before returning to CCP. Esc is terminal EOF; Ctrl-D is also
accepted when delivered by the window event source.

The adapted sources live alongside their provenance and licenses; builds do
not download dependencies. Binary notices are installed in `/RES/LICENSE/`.
