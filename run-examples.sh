#!/usr/bin/env bash
set -euo pipefail

# Generates every demonstration project in docs/examples/ through the CLI.
# Each demo writes its video, metadata sidecar and audio into a colocated
# docs/examples/output/ directory (resolved from the {project} asset root).

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
output_dir="$repo_root/docs/examples/output"
binary="$repo_root/build/videosynth"

if [[ -z "${IN_NIX_SHELL:-}" ]] || ! command -v ffprobe >/dev/null 2>&1; then
  exec nix develop "path:$repo_root" --command "$repo_root/run-examples.sh" "$@"
fi

if [[ ! -x "$binary" ]]; then
  echo "Error: expected executable not found: $binary" >&2
  echo "Build first with: nix develop \"path:$repo_root\" --command cmake --build \"$repo_root/build\"" >&2
  exit 1
fi

cd "$repo_root"
mkdir -p "$output_dir"

shopt -s nullglob
projects=("$repo_root/docs/examples"/*.yaml)
shopt -u nullglob

if (( ${#projects[@]} == 0 )); then
  echo "No demonstration projects found in docs/examples" >&2
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
  echo "$failures demonstration project(s) failed." >&2
  exit 1
fi

echo "All demonstration projects ran successfully. Outputs are in docs/examples/output/."
