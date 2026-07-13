#!/usr/bin/env python3
"""
Bit-exact comparison of a specific picture number across 4 clean composite sources.

For each format (PAL, NTSC), reads the raw uint16_t frame data for PN 9 from
each of the 4 clean source files and compares them byte-for-byte.

PN 9 is present in all 4 sources:
  Source A: starts at PN 1  → PN 9 is file frame 8
  Source B: starts at PN 5  → PN 9 is file frame 4
  Source C: starts at PN 3  → PN 9 is file frame 6
  Source D: starts at PN 8  → PN 9 is file frame 1
"""

import struct
from pathlib import Path

OUT = Path('/home/sdi/Coding/videosynth/tests/stacking-output')

# (label, path, frame_count, pn9_frame_index)
PAL_SOURCES = [
    ('A', OUT / 'videosynth_pal_discsim_A_clean.composite', 44, 8),
    ('B', OUT / 'videosynth_pal_discsim_B_clean.composite', 34, 4),
    ('C', OUT / 'videosynth_pal_discsim_C_clean.composite', 40, 6),
    ('D', OUT / 'videosynth_pal_discsim_D_clean.composite', 34, 1),
]

NTSC_SOURCES = [
    ('A', OUT / 'videosynth_ntsc_discsim_A_clean.composite', 44, 8),
    ('B', OUT / 'videosynth_ntsc_discsim_B_clean.composite', 34, 4),
    ('C', OUT / 'videosynth_ntsc_discsim_C_clean.composite', 40, 6),
    ('D', OUT / 'videosynth_ntsc_discsim_D_clean.composite', 34, 1),
]


def read_frame(path: Path, frame_count: int, frame_idx: int) -> bytes:
    file_size = path.stat().st_size
    assert file_size % frame_count == 0, f'{path.name}: size {file_size} not divisible by {frame_count}'
    frame_bytes = file_size // frame_count
    offset = frame_idx * frame_bytes
    with open(path, 'rb') as f:
        f.seek(offset)
        data = f.read(frame_bytes)
    assert len(data) == frame_bytes, f'Short read from {path.name}'
    return data


def compare_sources(label: str, sources: list) -> None:
    print(f'\n{"="*60}')
    print(f'{label} — PN 9 bit-exact comparison')
    print(f'{"="*60}')

    frames: dict[str, bytes] = {}
    for src_label, path, frame_count, pn9_idx in sources:
        frame_bytes = path.stat().st_size // frame_count
        data = read_frame(path, frame_count, pn9_idx)
        frames[src_label] = data
        print(f'  Source {src_label}: {path.name}')
        print(f'    frame_count={frame_count}, pn9_file_frame={pn9_idx}, '
              f'frame_size={frame_bytes} bytes ({frame_bytes//2} samples)')

    print()
    labels = list(frames.keys())
    all_match = True
    for i in range(len(labels)):
        for j in range(i + 1, len(labels)):
            la, lb = labels[i], labels[j]
            a, b = frames[la], frames[lb]
            if a == b:
                print(f'  {la} vs {lb}: IDENTICAL ✓')
            else:
                all_match = False
                # Find first difference
                first_diff = next(k for k in range(min(len(a), len(b))) if a[k] != b[k])
                diff_count = sum(1 for k in range(min(len(a), len(b))) if a[k] != b[k])
                print(f'  {la} vs {lb}: DIFFER — {diff_count} bytes differ, first at byte {first_diff}')
                # Show a few samples around the first difference
                sample_idx = first_diff // 2
                sample_a = struct.unpack_from('<H', a, sample_idx * 2)[0]
                sample_b = struct.unpack_from('<H', b, sample_idx * 2)[0]
                print(f'    first diff at sample {sample_idx}: src{la}={sample_a}, src{lb}={sample_b}')

    print()
    if all_match:
        print(f'  RESULT: All 4 {label} sources are BIT-IDENTICAL for PN 9 ✓')
    else:
        print(f'  RESULT: Sources DIFFER — colour phase mismatch confirmed ✗')


if __name__ == '__main__':
    compare_sources('PAL', PAL_SOURCES)
    compare_sources('NTSC', NTSC_SOURCES)
