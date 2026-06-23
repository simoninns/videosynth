#!/usr/bin/env python3
"""
Generate clean (no noise, no dropout, no OSD) versions of all disc-sim YAML files.
Output files get a _clean suffix so originals are preserved.
"""

import re
import sys
from pathlib import Path

KEYS_TO_REMOVE = {'noise', 'dropouts', 'osd'}


def strip_yaml_blocks(content: str, keys: set[str]) -> str:
    """Remove top-level-section keys and their indented children from YAML text."""
    lines = content.split('\n')
    result = []
    skip_indent: int | None = None

    for line in lines:
        if skip_indent is not None:
            stripped = line.lstrip()
            if not stripped:
                # Blank lines inside a removed block — drop them
                continue
            indent = len(line) - len(stripped)
            if indent <= skip_indent:
                # Returned to same or shallower indent — end of removed block
                skip_indent = None
            else:
                continue  # still inside removed block

        stripped = line.lstrip()
        indent = len(line) - len(stripped)
        # Check if this line starts a block we want to remove
        for key in keys:
            if stripped == f'{key}:' or stripped.startswith(f'{key}: '):
                skip_indent = indent
                break

        if skip_indent is None:
            result.append(line)

    return '\n'.join(result)


def make_clean(src_path: Path, dst_path: Path) -> None:
    content = src_path.read_text()

    # Strip noise / dropout / osd blocks
    content = strip_yaml_blocks(content, KEYS_TO_REMOVE)

    # Rewrite output paths to _clean variants
    content = re.sub(
        r'(video_path:\s+\S+?)\.composite',
        r'\1_clean.composite',
        content,
    )
    content = re.sub(
        r'(metadata_path:\s+\S+?)\.meta',
        r'\1_clean.meta',
        content,
    )

    # Remove consecutive blank lines left after stripping (tidy up)
    content = re.sub(r'\n{3,}', '\n\n', content)

    dst_path.write_text(content)
    print(f'Written: {dst_path}')


if __name__ == '__main__':
    base = Path(__file__).parent.parent / 'tests' / 'projects' / 'stacking'
    sources = [
        'pal_discsim_A.yaml',
        'pal_discsim_B.yaml',
        'pal_discsim_C.yaml',
        'pal_discsim_D.yaml',
        'ntsc_discsim_A.yaml',
        'ntsc_discsim_B.yaml',
        'ntsc_discsim_C.yaml',
        'ntsc_discsim_D.yaml',
    ]
    for name in sources:
        src = base / name
        stem = src.stem  # e.g. pal_discsim_A
        dst = base / f'{stem}_clean.yaml'
        make_clean(src, dst)
