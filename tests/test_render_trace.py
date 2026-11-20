import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('analyze_render_trace', ROOT / 'scripts/analyze_render_trace.py')
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)

report = trace.summarize('''yozora-trace: phase=style begin_us=100 end_us=300
yozora-trace: phase=style begin_us=200 end_us=250
yozora-trace: phase=layout begin_us=350 end_us=500
yozora-trace: phase=update-rendering begin_us=100 end_us=400
yozora-trace: paint-batch at_us=500 full=0 dirty=0,0,320,480
yozora-trace: tile=0 paint_us=120 different_rows=0
yozora-trace: tile=1 paint_us=300 different_rows=2
yozora-trace: paint-batch at_us=1000 full=1 dirty=0,0,0,0
yozora-trace: tile=0 paint_us=100 different_rows=0
''')
assert report['phase_union_ms']['style'] == .2
assert report['inside_update_rendering'] == {'style_ms': .2, 'layout_ms': .05}
assert report['unchanged_batches'] == [2]
assert report['identical_tiles'] == 2 and report['identical_tile_paint_ms'] == .22
assert trace.summarize('')['painted_tiles'] == 0
for malformed in ('yozora-trace: phase=style begin_us=2 end_us=1', 'yozora-trace: tile=0 paint_us=1 different_rows=0'):
    try:
        trace.summarize(malformed)
    except ValueError:
        pass
    else:
        raise AssertionError(malformed)
print('render trace: nested intervals, rendering intersections, unchanged batches and malformed records passed')
