#!/usr/bin/env python3
"""Renderer eligibility + real helper global generations, completely offline."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
temporary = Path('/tmp/opencode')
temporary.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='hyprglass-capability-', dir=temporary) as directory:
    directory = Path(directory)
    xml = root / 'protocols/applestia-glass-shapes-v1.xml'
    header = directory / 'applestia-glass-shapes-v1-server-protocol.h'
    code = directory / 'applestia-glass-shapes-v1-protocol.c'
    subprocess.run(['wayland-scanner', 'server-header', str(xml), str(header)], check=True)
    subprocess.run(['wayland-scanner', 'private-code', str(xml), str(code)], check=True)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'wayland-server'], text=True))
    objects = []
    for source in (root / 'tests/capability/helper-harness.c', code):
        obj = directory / (source.stem + '.o')
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(directory),
                        '-c', str(source), '-o', str(obj), *flags], check=True)
        objects.append(str(obj))
    binary = directory / 'policy'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++23', '-Wall', '-Wextra', '-Werror',
                    str(root / 'tests/capability/policy.cpp'), *objects, '-o', str(binary), *flags, '-lm'], check=True)
    subprocess.run([str(binary)], check=True)
