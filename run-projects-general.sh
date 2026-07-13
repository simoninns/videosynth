#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
output_dirs=("$repo_root/tests/general-output" "$repo_root/tests/general-yc-output")
binary="$repo_root/build/videosynth"

if [[ -z "${IN_NIX_SHELL:-}" ]] || ! command -v ffprobe >/dev/null 2>&1; then
  exec nix develop "path:$repo_root" --command "$repo_root/run-projects-general.sh" "$@"
fi

if [[ ! -x "$binary" ]]; then
  echo "Error: expected executable not found: $binary" >&2
  echo "Build first with: nix develop \"path:$repo_root\" --command cmake --build \"$repo_root/build\"" >&2
  exit 1
fi

cd "$repo_root"
mkdir -p "${output_dirs[@]}"

shopt -s nullglob
projects=(
  "$repo_root/tests/general"/*.yaml
  "$repo_root/tests/general-yc"/*.yaml
)
shopt -u nullglob

if (( ${#projects[@]} == 0 )); then
  echo "No project fixtures found in tests/general or tests/general-yc" >&2
  exit 1
fi

failures=0
for project in "${projects[@]}"; do
  rel_project="${project#$repo_root/}"
  echo "Running $rel_project"
  if "$binary" --project "$project" --log-level debug "$@"; then
    echo "PASS  $rel_project"
  else
    echo "FAIL  $rel_project" >&2
    failures=$((failures + 1))
  fi
done

if (( failures > 0 )); then
  echo "$failures project run(s) failed." >&2
  exit 1
fi

echo "All project fixtures ran successfully. Outputs are in tests/general-output/ and tests/general-yc-output/."
