#!/usr/bin/env python3
"""Test the production applied-shapes formatter without a compositor or GL."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
temporary = Path('/tmp/opencode')
temporary.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='hyprglass-shapes-diagnostic-', dir=temporary) as directory:
    directory = Path(directory)
    source = (root / 'src/Diagnostics.cpp').read_text()
    begin = source.index('// BEGIN APPLIED_SHAPES_FORMATTER')
    end = source.index('// END APPLIED_SHAPES_FORMATTER', begin)
    actual = directory / 'actual-formatter.cpp'
    actual.write_text('#include "DiagnosticsShapes.hpp"\n#include "GlassShapeLens.hpp"\n'
                      '#include <algorithm>\n#include <cmath>\n#include <format>\n#include <string_view>\n'
                      'namespace Diagnostics {\n' + source[begin:end] + '\n}\n')
    binary = directory / 'formatter'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++23', '-Wall', '-Wextra',
                    '-I', str(root / 'src'), str(actual), str(root / 'tests/shape-diagnostics/formatter.cpp'),
                    '-o', str(binary)], check=True)
    def output(mode):
        return subprocess.check_output([str(binary), mode], text=True)
    data = json.loads(output('json'))
    assert data['dataSource'] == 'applied' and data['protocolActive'] is True
    by_ns = {layer['namespace']: layer for layer in data['layers']}
    assert by_ns['legacy']['binding'] == 'unbound' and by_ns['legacy']['shapes'] is None
    assert by_ns['legacy']['bound'] is False and by_ns['legacy']['nativeActive'] is False
    assert by_ns['empty']['binding'] == 'bound-empty' and by_ns['empty']['shapes'] == []
    assert by_ns['empty']['nativeActive'] is True
    assert by_ns['excluded']['bound'] is True and by_ns['excluded']['nativeActive'] is False
    native = by_ns['applestia-drawers"\nλ']
    assert native['generation'] == 9007199254740993 and native['dataSource'] == 'applied'
    assert native['layerBoxGlobal'] == [100, 200, 1600, 1000]
    assert native['monitorPosition'] == [100, 200] and native['monitorScale'] == 1.25
    first, second = native['shapes']
    assert first['insertionIndex'] == 0 and first['depth'] == 3
    assert second['insertionIndex'] == 1 and second['depth'] == 1 # Original order, not sorted depth.
    assert [first[k] for k in ('x','y','width','height')] == [1407.125,552.375,120.25,40.5]
    assert first['radii'] == [20,4,4,20] and first['clip'] == [4,5,1500,900]
    assert second['clip'] is None and second['x'] == -3.5
    assert first['tint'] == 0xfedcba98 and first['tintHex'] == '0xfedcba98'
    assert first['preset'] == 'clear"\\\n\t' and first['opacity'] == .5
    assert first['effectiveBezelLogical'] == 12 and abs(first['maxDisplacementLogical']-10.8)<.00001
    bounded = json.loads(output('bounded'))
    assert len(bounded['layers']) == 16 and bounded['layersTotal'] == 41 and bounded['layersOmitted'] == 25
    bound = bounded['layers'][0]
    assert bound['bound'] and len(bound['shapes']) == 128 and bound['shapesOmitted'] == 12
    assert len(bound['namespace']) == 259 and bound['namespace'].endswith('...')
    assert len(bound['shapes'][0]['preset']) == 259
    inactive = json.loads(output('inactive'))
    assert inactive['protocolActive'] is False and all(not layer['nativeActive'] for layer in inactive['layers'])
    nonfinite = json.loads(output('nonfinite'))
    assert nonfinite['layers'][0]['shapes'][0]['x'] is None
    assert nonfinite['layers'][0]['shapes'][0]['opacity'] is None
    text = output('text')
    assert 'APPLIED' in text and 'binding=unbound' in text and 'binding=bound-empty' in text
    assert 'insertionIndex=0 depth=3' in text and 'clip=[4,5,1500,900]' in text
    # Integration entry is the EXISTING dispatcher; do not silently add another command.
    assert 'if (rest == "shapes")\n                return formatShapes(format);' in source
    collection = source[source.index('std::string formatShapes('):source.index('std::string formatStats(')]
    assert 'GlassShapes::forSurface(root.get())' in collection
    assert 'GlassShapes::generationForSurface(root.get())' in collection
    assert 'currentFB' not in collection and 'resetCounters' not in collection
    print('PASS production APPLIED formatter: JSON/text, bound-empty/unbound, order/original hints, escaping, bounded output, inactive capability, dispatcher')
