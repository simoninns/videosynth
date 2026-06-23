#!/usr/bin/env python3
"""Detailed comparison showing where differences fall (VBI vs active video)."""

import struct
from pathlib import Path

OUT = Path('/home/sdi/Coding/videosynth/tests/projects/output-stacking')

# PAL 4FSC constants
PAL_SAMPLES_PER_LINE = 1135
PAL_LINES = 625
PAL_FIRST_ACTIVE_LINE = 44
PAL_LAST_ACTIVE_LINE = 620

# NTSC 4FSC constants
NTSC_SAMPLES_PER_LINE = 910
NTSC_LINES = 525
NTSC_FIRST_ACTIVE_LINE = 20   # approx: field 1 active starts ~line 20
NTSC_FIRST_FIELD2_LINE = 263  # field 2 starts here
NTSC_FIELD2_ACTIVE_LINE = 283 # approx


def read_frame(path: Path, frame_count: int, frame_idx: int) -> bytes:
    file_size = path.stat().st_size
    frame_bytes = file_size // frame_count
    offset = frame_idx * frame_bytes
    with open(path, 'rb') as f:
        f.seek(offset)
        return f.read(frame_bytes)


def analyse_diff(label_a: str, data_a: bytes, label_b: str, data_b: bytes,
                 samples_per_line: int, first_active: int) -> None:
    differ_samples = [i for i in range(min(len(data_a), len(data_b)) // 2)
                      if data_a[i*2:i*2+2] != data_b[i*2:i*2+2]]
    if not differ_samples:
        print(f'  {label_a} vs {label_b}: IDENTICAL ✓')
        return

    total = len(differ_samples)
    first_s = differ_samples[0]
    line_of_first = first_s // samples_per_line
    in_active = line_of_first >= first_active

    # Count diffs in VBI vs active
    vbi_diffs = sum(1 for s in differ_samples if s // samples_per_line < first_active)
    active_diffs = sum(1 for s in differ_samples if s // samples_per_line >= first_active)

    print(f'  {label_a} vs {label_b}: DIFFER — {total} samples, '
          f'first at sample {first_s} (line {line_of_first})')
    print(f'    VBI diffs: {vbi_diffs}, Active video diffs: {active_diffs}')
    if active_diffs > 0:
        first_active_s = next(s for s in differ_samples
                              if s // samples_per_line >= first_active)
        print(f'    First active-video diff: sample {first_active_s} '
              f'(line {first_active_s // samples_per_line})')


def run_format(label: str, sources: list, spl: int, first_active: int) -> None:
    print(f'\n{"="*60}')
    print(f'{label} — PN 9 comparison (samples_per_line={spl}, '
          f'active from line {first_active})')
    print(f'{"="*60}')
    frames: dict[str, bytes] = {}
    for src_label, path, frame_count, pn9_idx in sources:
        data = read_frame(path, frame_count, pn9_idx)
        frames[src_label] = data
        phase_pred = pn9_idx % (4 if label == 'PAL' else 2)
        correct = 0  # PN 9 = (9-1) % 4 = 0 for PAL; (9-1)%2=0 for NTSC
        status = '✓ correct' if phase_pred == correct else f'✗ wrong (expected {correct})'
        print(f'  Src {src_label}: file_frame={pn9_idx}, '
              f'predicted_phase={phase_pred} {status}')
    print()
    labels = list(frames.keys())
    for i in range(len(labels)):
        for j in range(i + 1, len(labels)):
            la, lb = labels[i], labels[j]
            analyse_diff(la, frames[la], lb, frames[lb], spl, first_active)


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

if __name__ == '__main__':
    run_format('PAL', PAL_SOURCES, PAL_SAMPLES_PER_LINE, PAL_FIRST_ACTIVE_LINE)
    run_format('NTSC', NTSC_SOURCES, NTSC_SAMPLES_PER_LINE, NTSC_FIRST_ACTIVE_LINE)
