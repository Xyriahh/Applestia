#!/usr/bin/env python3
"""Offline CPU/GLSL/EGL regressions. Never connects to a running compositor."""
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def cpu_tests():
    def circular(distance, bezel):
        x = 1 - max(0, min(distance / bezel, 1))
        epsilon = max(.0001, min(.5 / bezel, .5))
        top = math.sqrt(1 + epsilon)
        return (top - math.sqrt(max(1 - x*x, 0) + epsilon)) / (top - math.sqrt(epsilon))

    for bezel in (2, 4, 8, 12, 16, 32, 64):
        values = [circular(bezel*i/1000, bezel) for i in range(1001)]
        assert abs(values[0] - 1) < 1e-12 and abs(values[-1]) < 1e-12
        assert all(math.isfinite(v) and 0 <= v <= 1 for v in values)
        assert all(a >= b for a, b in zip(values, values[1:]))

    # Stable ascending depth is the material parent-before-child contract.
    shapes = [(2, 'child A'), (0, 'panel'), (2, 'child B'), (1, 'parent')]
    assert [name for _, name in sorted(shapes, key=lambda s: s[0])] == ['panel', 'parent', 'child A', 'child B']
    # Outward bounds contain the whole fractional old/new damage footprint.
    for scale in (.5, 1, 1.25, 1.5, 2, 4, 8):
        for width, height, radius in ((16, 16, 8), (96, 32, 16), (300, 240, 32)):
            bezel = min(max(12, min(1.5 * min(radius, min(width, height)/2), 24)), .35*min(width,height)) * scale
            footprint = math.ceil(.9*bezel + .25*scale + 2.5*scale + 2)
            maximum = math.ceil((.9*24+.25+2.5)*scale+2)
            assert footprint <= max(60, maximum)
            x, y = 7.13*scale - footprint, -2.29*scale - footprint
            w, h = width*scale + 2*footprint, height*scale + 2*footprint
            assert math.floor(x) <= x and math.ceil(x+w) >= x+w
            assert math.floor(y) <= y and math.ceil(y+h) >= y+h
    print('PASS CPU circular profile, stable depth, scale/footprint/outward damage bounds', flush=True)


def main():
    cpu_tests()
    temporary_root = Path('/tmp/opencode')
    temporary_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='hyprglass-shapes-', dir=temporary_root) as directory:
        directory = Path(directory)
        source = (ROOT / 'src/Shaders.hpp').read_text()
        shaders = re.findall(r'\{"([^\"]+)", R"GLSL\((.*?)\)GLSL"\}', source, re.S)
        assert len(shaders) >= 10
        for name, shader in shaders:
            file = directory / name
            file.write_text(shader.lstrip())
            subprocess.run(['glslangValidator', '-S', {'.comp': 'comp', '.vert': 'vert'}.get(file.suffix, 'frag'), str(file)], check=True)
        print(f'PASS GLSL validation ({len(shaders)} shaders)', flush=True)
        for reference in (ROOT / 'tests/shapes').glob('*-reference.frag'):
            (directory / reference.name).write_text(reference.read_text())
        flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'egl', 'glesv2'], text=True))
        binary = directory / 'offscreen'
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', str(ROOT / 'tests/shapes/offscreen.cpp'), *flags, '-o', str(binary)], check=True)
        environment = os.environ.copy()
        # Pin Mesa + llvmpipe. No Wayland/X11 connection, physical compositor,
        # nested compositor or GPU driver under test is involved.
        environment.update(EGL_PLATFORM='surfaceless', LIBGL_ALWAYS_SOFTWARE='1',
                           __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json')
        subprocess.run([str(binary), str(directory)], env=environment, check=True)
        binary = directory / 'lens-model'
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', str(ROOT / 'tests/shapes/lens-model.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        # Compile the production guard itself against modeled Hyprland caches,
        # not a duplicate raw-GL-only implementation that could drift from it.
        renderer = (ROOT / 'src/GlassShapesRenderer.cpp').read_text()
        start = renderer.index('class CStateGuard {')
        end = renderer.index('\n};', start) + len('\n};')
        (directory / 'state-guard-under-test.hpp').write_text(renderer[start:end])
        binary = directory / 'state-cache'
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra',
                        '-I', str(directory), str(ROOT / 'tests/shapes/state-cache.cpp'),
                        *flags, '-o', str(binary)], check=True)
        subprocess.run([str(binary)], env=environment, check=True)


if __name__ == '__main__':
    main()
