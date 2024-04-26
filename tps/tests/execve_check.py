#!/usr/bin/env python3

import subprocess
import sys

strace_args = ['strace', '-e', 'execve', sys.argv[1]]
strace = subprocess.run(
    strace_args,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    text=True
)
strace_lines = strace.stderr.splitlines()
print(strace_lines)

has_execve = False
for line in strace_lines:
    if line.startswith('+++'):
        continue
    if not has_execve:
        has_execve = True
        continue
    print(line)
    syscall, _ = line.split(' = ')
    print('Illegal syscall:', syscall.strip(), file=sys.stderr)
    exit(1)

print('execve Not Used')
