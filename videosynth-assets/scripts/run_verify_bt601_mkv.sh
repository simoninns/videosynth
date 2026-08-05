#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v nix >/dev/null 2>&1; then
  echo "Error: nix is required but not found in PATH." >&2
  exit 1
fi

exec nix shell nixpkgs#python3 nixpkgs#ffmpeg \
  -c python3 "$ROOT_DIR/scripts/verify_bt601_mkv.py" "$@"
