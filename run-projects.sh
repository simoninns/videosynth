#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
projects_dir="$repo_root/tests/projects"
output_dir="$projects_dir/output"
binary="$repo_root/build/videosynth"

if [[ -z "${IN_NIX_SHELL:-}" ]]; then
  exec nix develop "path:$repo_root" --command "$repo_root/run-projects.sh" "$@"
fi

if [[ ! -x "$binary" ]]; then
  echo "Error: expected executable not found: $binary" >&2
  echo "Build first with: nix develop \"path:$repo_root\" --command cmake --build \"$repo_root/build\"" >&2
  exit 1
fi

cd "$repo_root"
mkdir -p "$output_dir"

shopt -s nullglob
projects=("$projects_dir"/*.yaml)
shopt -u nullglob

if (( ${#projects[@]} == 0 )); then
  echo "No project fixtures found in $projects_dir" >&2
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

echo "All project fixtures ran successfully. Outputs are in tests/projects/output/."
