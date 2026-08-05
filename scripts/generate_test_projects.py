#!/usr/bin/env python3
"""Derive the generated project fixtures from the committed ones.

The committed tree under projects/ holds only hand-authored projects. Every
mechanically derivable variant (Y/C renderings, impairment-free reference
copies) is produced here at build time and written into the build tree, so the
repository never carries near-duplicate YAML.

The rules live in projects/variants.json; see that file for the format. Each
variant supports three transformations, all applied to the YAML *text* so the
authored comments survive into the derived project:

  remove_section_blocks   drop the named keys (and their children) from every
                          entry of the top-level `sections:` list
  signal_type             set output.signal_type and switch the video_path
                          suffix between .cvbs and .cvbsy accordingly
  name_suffix             append a suffix to both the generated filename and
                          the video_path stem

Usage:
  generate_test_projects.py --source projects --output <dir> [--stamp <file>]
"""

import argparse
import json
import re
import sys
from pathlib import Path

# Video path suffix per output signal type.
SIGNAL_TYPE_SUFFIX = {"composite": ".cvbs", "yc": ".cvbsy"}

VIDEO_PATH_RE = re.compile(r'^(?P<indent>\s*)video_path:\s*(?P<value>.+?)\s*$')
SIGNAL_TYPE_RE = re.compile(r'^(?P<indent>\s*)signal_type:\s*(?P<value>.+?)\s*$')


def indent_of(line: str) -> int:
    """Column of the first non-space character; len(line) for a blank line."""
    return len(line) - len(line.lstrip())


def remove_section_blocks(text: str, keys: set[str]) -> str:
    """Remove `keys` and their children from every entry of `sections:`.

    Only keys at a section entry's own property indent are removed, so a
    like-named key nested deeper inside another block is left alone.
    """
    lines = text.split('\n')
    result: list[str] = []
    in_sections = False
    item_indent: int | None = None
    property_indent: int | None = None
    skip_indent: int | None = None

    for line in lines:
        stripped = line.strip()

        # A run of lines belonging to a removed block: they are all indented
        # deeper than the key that introduced it. Blank lines inside the run
        # are dropped with it.
        if skip_indent is not None:
            if not stripped or indent_of(line) > skip_indent:
                continue
            skip_indent = None

        if stripped and indent_of(line) == 0:
            # A new top-level key ends any previous top-level block.
            in_sections = stripped.startswith('sections:')
            item_indent = None
            property_indent = None
            result.append(line)
            continue

        if in_sections and stripped.startswith('- '):
            # The first dash after `sections:` fixes the list's item indent;
            # deeper dashes belong to nested lists (line_injections, overlays)
            # and must not be mistaken for a new section entry.
            if item_indent is None:
                item_indent = indent_of(line)
            if indent_of(line) == item_indent:
                # A section's properties sit one level in from its dash.
                property_indent = item_indent + 2

        if (in_sections and property_indent is not None and stripped and
                indent_of(line) == property_indent):
            key = stripped.split(':', 1)[0].lstrip('- ').strip()
            if key in keys:
                skip_indent = property_indent
                continue

        result.append(line)

    # Collapse the blank-line runs left behind by the removed blocks.
    return re.sub(r'\n{3,}', '\n\n', '\n'.join(result))


def retarget_video_path(text: str, suffix: str, name_suffix: str) -> str:
    """Rewrite output.video_path to use `suffix`, with `name_suffix` on the stem."""
    lines = text.split('\n')
    for index, line in enumerate(lines):
        match = VIDEO_PATH_RE.match(line)
        if match is None:
            continue
        value = match.group('value')
        quote = value[0] if value[:1] in ('"', "'") else ''
        bare = value.strip('"\'')
        for known in SIGNAL_TYPE_SUFFIX.values():
            if bare.endswith(known):
                bare = bare[: -len(known)]
                break
        bare = bare + name_suffix + suffix
        lines[index] = f"{match.group('indent')}video_path: {quote}{bare}{quote}"
        return '\n'.join(lines)
    raise ValueError('project declares no output.video_path')


def set_signal_type(text: str, signal_type: str) -> str:
    """Set output.signal_type, inserting the key after video_path when absent."""
    lines = text.split('\n')
    for index, line in enumerate(lines):
        match = SIGNAL_TYPE_RE.match(line)
        if match is not None:
            lines[index] = f"{match.group('indent')}signal_type: {signal_type}"
            return '\n'.join(lines)

    for index, line in enumerate(lines):
        match = VIDEO_PATH_RE.match(line)
        if match is not None:
            lines.insert(index + 1,
                         f"{match.group('indent')}signal_type: {signal_type}")
            return '\n'.join(lines)
    raise ValueError('project declares no output block to set signal_type in')


def derive(text: str, variant: dict) -> str:
    """Apply one variant's transformations to a project's YAML text."""
    blocks = set(variant.get('remove_section_blocks', []))
    if blocks:
        text = remove_section_blocks(text, blocks)

    signal_type = variant.get('signal_type', 'composite')
    if signal_type not in SIGNAL_TYPE_SUFFIX:
        raise ValueError(f"unknown signal_type '{signal_type}'")

    name_suffix = variant.get('name_suffix', '')
    text = retarget_video_path(text, SIGNAL_TYPE_SUFFIX[signal_type],
                               name_suffix)

    if 'signal_type' in variant:
        text = set_signal_type(text, signal_type)

    return text


def collect_sources(patterns: list[str], search_roots: list[Path]) -> list[Path]:
    """Resolve globs against each search root in turn, de-duplicated by name."""
    found: dict[str, Path] = {}
    for root in search_roots:
        for pattern in patterns:
            for path in sorted(root.glob(pattern)):
                found.setdefault(str(path), path)
    return [found[key] for key in sorted(found)]


def generate(source_root: Path, output_root: Path, config_path: Path,
             verbose: bool) -> int:
    config = json.loads(config_path.read_text())
    variants = config.get('variants', [])
    if not variants:
        raise ValueError(f'{config_path} declares no variants')

    written = 0
    for variant in variants:
        name = variant['name']
        variant_dir = output_root / name
        variant_dir.mkdir(parents=True, exist_ok=True)

        # Later variants may build on the output of earlier ones.
        sources = collect_sources(variant['sources'],
                                  [source_root, output_root])
        if not sources:
            raise ValueError(f"variant '{name}' matched no source projects")

        expected: set[Path] = set()
        for source in sources:
            suffix = variant.get('name_suffix', '')
            destination = variant_dir / f'{source.stem}{suffix}.yaml'
            try:
                derived = derive(source.read_text(), variant)
            except ValueError as error:
                raise ValueError(f'{source}: {error}') from error
            # Only rewrite on change so the build stamp stays meaningful.
            if not destination.exists() or destination.read_text() != derived:
                destination.write_text(derived)
            expected.add(destination)
            written += 1

        # Drop derived files whose source has since been renamed or deleted.
        for stale in variant_dir.glob('*.yaml'):
            if stale not in expected:
                stale.unlink()

        if verbose:
            print(f'{name}: {len(sources)} project(s) -> {variant_dir}')

    return written


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True,
                        help='committed project tree (projects/)')
    parser.add_argument('--output', type=Path, required=True,
                        help='directory to write the derived projects into')
    parser.add_argument('--config', type=Path, default=None,
                        help='rules file (default: <source>/variants.json)')
    parser.add_argument('--stamp', type=Path, default=None,
                        help='file to touch on success, for build systems')
    parser.add_argument('--quiet', action='store_true')
    args = parser.parse_args(argv)

    config_path = args.config or (args.source / 'variants.json')
    if not config_path.is_file():
        print(f'Error: rules file not found: {config_path}', file=sys.stderr)
        return 1

    try:
        written = generate(args.source, args.output, config_path,
                           not args.quiet)
    except (ValueError, OSError) as error:
        print(f'Error: {error}', file=sys.stderr)
        return 1

    if not args.quiet:
        print(f'Generated {written} project(s) in {args.output}')
    if args.stamp is not None:
        args.stamp.parent.mkdir(parents=True, exist_ok=True)
        args.stamp.write_text('')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
