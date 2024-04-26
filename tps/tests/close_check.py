#!/usr/bin/env python3

import subprocess
import sys

strace_args = ['strace', '-e', 'openat,close', sys.argv[1]]
strace = subprocess.run(
    strace_args,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    text=True
)
strace_lines = strace.stderr.splitlines()

opened_proc = False
open_fds = set()
for line in strace_lines:
    if line.startswith('+++'):
        continue
    syscall, ret = line.split(' = ')
    if syscall.startswith('open'):
        if not ret.isdigit():
           print(f"open returned an error: {ret}", file=sys.stderr)
           exit(1)
        if ret == '5':
            print("close file descriptors when they're no longer needed",
                  file=sys.stderr)
            exit(1)
        if '"/proc"' in syscall or '"/proc/"' in syscall:
            opened_proc = True
        open_fds.add(ret)
    elif syscall.startswith('close'):
        if ret != '0':
           print(f"close returned an error: {ret}", file=sys.stderr)
           exit(1)
        fd = syscall.strip()[6:-1]
        if fd not in open_fds:
           print(f"closed an unknown file descriptor: {fd}", file=sys.stderr)
           exit(1)
        open_fds.remove(fd)

if len(open_fds) != 0:
    print(f"file descriptors still open: {', '.join(open_fds)}",
          file=sys.stderr)
    exit(1)

if not opened_proc:
    print("No file descriptors opened in /proc", file=sys.stderr)
    exit(1)

print('All file descriptors closed')
