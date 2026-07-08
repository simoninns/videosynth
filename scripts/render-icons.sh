#!/usr/bin/env bash
#
# File:        render-icons.sh
# Purpose:     Render the application icon PNG set from the logo SVG.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Simon Inns
#
# Renders assets/videosynth-icon-<size>.png for each icon size from
# assets/videosynth-logo.svg. Output is bit-stable for a given rsvg-convert
# version, so regenerated icons only change when the SVG changes.
#
# Usage:
#   scripts/render-icons.sh
# or without rsvg-convert installed:
#   nix shell nixpkgs#librsvg --command scripts/render-icons.sh

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
svg_source="${repo_root}/assets/videosynth-logo.svg"
sizes=(256 128 64 48 32 16)

if ! command -v rsvg-convert >/dev/null 2>&1; then
    echo "error: rsvg-convert not found." >&2
    echo "Run via: nix shell nixpkgs#librsvg --command $0" >&2
    exit 1
fi

for size in "${sizes[@]}"; do
    output="${repo_root}/assets/videosynth-icon-${size}.png"
    rsvg-convert --width "${size}" --height "${size}" \
        --output "${output}" "${svg_source}"
    echo "rendered ${output}"
done
