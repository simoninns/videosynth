#!/usr/bin/env python3
"""Substitute release metadata into the Flatpak manifest.

The Flatpak build sandbox has no git metadata, so the version string and the
AppStream release date cannot be derived during the build. They are written
into a generated copy of the manifest instead, leaving the committed manifest
free of build-specific values.

Placeholders substituted:

  @VIDEOSYNTH_RELEASE_VERSION@   release version, e.g. 1.2.0 (a leading "v" is
                                 stripped so AppStream sees a bare version)
  @VIDEOSYNTH_RELEASE_DATE@      release date in ISO 8601 form, e.g. 2026-08-05

Usage:
  scripts/prepare-flatpak-manifest.py \
      --version v1.2.0 --date 2026-08-05 \
      --output flatpak-build/io.github.decode_orc.VideoSynth.yml
"""

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Simon Inns

import argparse
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = (
    REPO_ROOT / "packaging" / "flatpak" / "io.github.decode_orc.VideoSynth.yml"
)

VERSION_PLACEHOLDER = "@VIDEOSYNTH_RELEASE_VERSION@"
DATE_PLACEHOLDER = "@VIDEOSYNTH_RELEASE_DATE@"

DATE_PATTERN = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def normalise_version(version: str) -> str:
    """Strip a leading "v" from a git tag so AppStream sees a bare version."""
    stripped = version.strip()
    if stripped.startswith("v") and stripped[1:2].isdigit():
        return stripped[1:]
    return stripped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=pathlib.Path,
        default=DEFAULT_MANIFEST,
        help="source manifest (default: packaging/flatpak manifest)",
    )
    parser.add_argument(
        "--version",
        required=True,
        help="release version or git tag, e.g. v1.2.0",
    )
    parser.add_argument(
        "--date",
        required=True,
        help="release date as YYYY-MM-DD",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        required=True,
        help="path of the generated manifest",
    )
    args = parser.parse_args()

    if not DATE_PATTERN.match(args.date):
        print(f"error: --date must be YYYY-MM-DD, got '{args.date}'",
              file=sys.stderr)
        return 2

    version = normalise_version(args.version)
    if not version:
        print("error: --version must not be empty", file=sys.stderr)
        return 2

    text = args.manifest.read_text(encoding="utf-8")
    if VERSION_PLACEHOLDER not in text:
        print(f"error: {VERSION_PLACEHOLDER} not found in {args.manifest}",
              file=sys.stderr)
        return 1

    # The source manifest lives in packaging/flatpak/ and points its `dir`
    # source at ../.. ; the generated copy usually sits elsewhere, so make the
    # source path absolute rather than relying on the output location.
    text = text.replace(VERSION_PLACEHOLDER, version)
    text = text.replace(DATE_PLACEHOLDER, args.date)
    text = text.replace("path: ../..", f"path: {REPO_ROOT}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")
    print(f"wrote {args.output} (version {version}, date {args.date})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
