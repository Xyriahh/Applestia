#!/usr/bin/env python3
"""Exact APPLIED metadata -> private software EGL reproduction, no compositor."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('snapshot', type=Path)
args = parser.parse_args()
data = json.loads(args.snapshot.read_text())
layer = next(l for l in data['layers'] if l['namespace'] == 'applestia-drawers')
assert data['dataSource'] == layer['dataSource'] == 'applied'
assert layer['nativeActive'] and layer['monitorScale'] == 1 and layer['monitorTransform'] == 0
assert layer['layerBoxGlobal'] == [0, 0, 1600, 1000]
assert layer['shapeCount'] == 28 and layer['shapesOmitted'] == 0
shapes = sorted(layer['shapes'], key=lambda s: s['insertionIndex'])
assert [s['insertionIndex'] for s in shapes] == list(range(28))
assert all(s['preset'] == 'applestia_control' for s in shapes)

def number(value):
    return repr(float(value)) + 'f'

def array(values):
    return '{' + ','.join(number(v) for v in values) + '}'

temporary = Path('/tmp/opencode')
temporary.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='hyprglass-applied-pill-', dir=temporary) as directory:
    directory = Path(directory)
    text = 'static constexpr uint64_t TRACE_GENERATION = ' + str(layer['generation']) + 'ULL;\n'
    text += 'static const std::vector<STracedShape> TRACE = {\n'
    for s in shapes:
        clip = 'std::array<float,4>' + array(s['clip']) if s['clip'] is not None else 'std::nullopt'
        text += '{' + str(s['insertionIndex']) + ', {'
        text += ','.join('.'+key+'='+number(s[key]) for key in ('x', 'y', 'width', 'height'))
        text += ',.radii=' + array(s['radii']) + ',.depth=' + str(s['depth']) + 'U,.tint=' + str(s['tint']) + 'U'
        text += ',.preset=' + json.dumps(s['preset']) + ',.opacity=' + number(s['opacity']) + ',.clip=' + clip + '}},\n'
    text += '};\n'
    (directory / 'applied-fixture.hpp').write_text(text)
    source = (root / 'src/Shaders.hpp').read_text()
    shader = dict(re.findall(r'\{"([^\"]+)", R"GLSL\((.*?)\)GLSL"\}', source, re.S))['liquidshape.frag'].lstrip()
    (directory / 'liquidshape.frag').write_text(shader)
    bend = 'vec2 bend = inward * lens * lensPhysical.x;'
    dispersion = 'vec2 dispersion = inward * profile(d, max(CHROMATIC_SUPPORT_FRACTION * bezel, MIN_CHROMATIC_SUPPORT_PX)) * lensPhysical.y;'
    assert shader.count(bend) == shader.count(dispersion) == 1
    # TEST-ONLY matched material: freeze only sample displacement/chromatic
    # coordinates, retaining exactly the same SDF/frost/specular/tint/opacity.
    flat = shader.replace(bend, 'vec2 bend = vec2(0.0);').replace(dispersion, 'vec2 dispersion = vec2(0.0);')
    (directory / 'matched-material-flat.frag').write_text(flat)
    for name in ('liquidshape.frag', 'matched-material-flat.frag'):
        subprocess.run(['glslangValidator', '-S', 'frag', str(directory/name)], check=True)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'egl', 'glesv2'], text=True))
    binary = directory / 'applied-pill'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++23', '-O2', '-I', str(directory),
                    str(root/'tests/shapes/applied-pill.cpp'), *flags, '-o', str(binary)], check=True)
    environment = os.environ.copy()
    environment.update(EGL_PLATFORM='surfaceless', LIBGL_ALWAYS_SOFTWARE='1',
                       __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json')
    subprocess.run([str(binary), str(directory)], env=environment, check=True)
