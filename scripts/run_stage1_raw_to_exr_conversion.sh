#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

nix shell \
  nixpkgs#ffmpeg \
  nixpkgs#openexr \
  nixpkgs#python3 \
  nixpkgs#python3Packages.numpy \
  --command python3 "${repo_root}/scripts/convert_progressive_raw_to_exr.py" "$@"
