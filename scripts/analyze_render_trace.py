#!/usr/bin/env python3
"""summarize optional yozora-render phase and tile diagnostics from one process"""
import argparse
import json
from pathlib import Path
import re


def union(intervals):
    result = []
    for start, end in sorted(intervals):
        if result and start <= result[-1][1]:
            result[-1] = (result[-1][0], max(result[-1][1], end))
        else:
            result.append((start, end))
    return result


def milliseconds(intervals):
    return round(sum(end - start for start, end in union(intervals)) / 1000, 3)


def summarize(log):
    phases = {}
    batches = []
    for line in log.splitlines():
        match = re.fullmatch(r'yozora-trace: phase=(\S+) begin_us=(\d+) end_us=(\d+)', line)
        if match:
            name, start, end = match.groups()
            start, end = int(start), int(end)
            if end < start:
                raise ValueError('trace phase ends before it starts')
            phases.setdefault(name, []).append((start, end))
        elif line.startswith('yozora-trace: paint-batch '):
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            batches.append(dict(fields, tiles=[]))
        elif line.startswith('yozora-trace: tile='):
            if not batches:
                raise ValueError('tile record has no paint batch')
            batches[-1]['tiles'].append({key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', line)})
    rendering = union(phases.get('update-rendering', []))
    inside = {}
    for name in ('style', 'layout'):
        intervals = union(phases.get(name, []))
        intersections = [(max(start, outer_start), min(end, outer_end))
                         for start, end in intervals for outer_start, outer_end in rendering
                         if start < outer_end and end > outer_start]
        inside[name + '_ms'] = milliseconds(intersections)
    tiles = [tile for batch in batches for tile in batch['tiles']]
    identical = [tile for tile in tiles if tile['different_rows'] == 0]
    unchanged_batches = [index + 1 for index, batch in enumerate(batches)
                         if batch['tiles'] and all(tile['different_rows'] == 0 for tile in batch['tiles'])]
    return {
        'phase_union_ms': {name: milliseconds(intervals) for name, intervals in sorted(phases.items())},
        'inside_update_rendering': inside,
        'paint_batches': len(batches),
        'painted_tiles': len(tiles),
        'identical_tiles': len(identical),
        'identical_tile_paint_ms': round(sum(tile['paint_us'] for tile in identical) / 1000, 3),
        'unchanged_batches': unchanged_batches,
        'batches': batches,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(args.log.read_text()), indent=2))


if __name__ == '__main__':
    main()
