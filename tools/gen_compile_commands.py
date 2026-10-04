#!/usr/bin/env python3
"""Generate editor commands from Make's dry run; never compile or link."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess


root = Path(__file__).resolve().parent.parent
environment = os.environ.copy()
# This child Make only prints commands; do not inherit jobserver descriptors.
environment.pop("MAKEFLAGS", None)
environment.pop("MFLAGS", None)
result = subprocess.run(
    ["make", "--no-print-directory", "-Bn",
     "bin/abi.elf", "bin/monitor.elf", "bin/loader.elf"],
    cwd=root, env=environment, text=True, capture_output=True, check=True,
)
commands = {}


def add(source, arguments):
    source = (root / source).resolve()
    arguments = list(arguments)
    arguments[0] = shutil.which(arguments[0]) or arguments[0]
    commands.setdefault(str(source), {
        "directory": str(root), "file": str(source), "arguments": arguments,
    })


for line in result.stdout.replace("\\\n", " ").splitlines():
    arguments = shlex.split(line)
    if "-c" not in arguments:
        continue
    source = arguments[arguments.index("-c") + 1]
    if source.endswith(".c"):
        add(source, arguments)

# Host utilities do not use the project's replacement libc headers or -m32.
for source in sorted((root / "tools").rglob("*.c")):
    add(source, ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-c", str(source)])

# Host tests deliberately use the project's interfaces, but native host I/O.
for source in sorted((root / "tests").rglob("*.c")):
    add(source, ["gcc", "-std=gnu11", "-fno-builtin", "-Wall", "-Wextra",
                 "-Ilib/include", "-c", str(source)])

destination = root / "compile_commands.json"
temporary = destination.with_suffix(".json.tmp")
temporary.write_text(json.dumps(list(commands.values()), indent=2) + "\n")
temporary.replace(destination)
print(f"Generated {destination.name}: {len(commands)} source files")
