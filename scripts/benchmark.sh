#!/usr/bin/env bash
set -euo pipefail

# Times generation over the fixed benchmark projects in projects/benchmark/.
#
# Each project runs once per thread configuration (default: 1 and auto) and the
# script reports wall-clock and frames/second per run. Frame counts come from
# the CLI's own "Generating ... frame(s)" log line, so the table stays
# correct if a project's duration changes.
#
# Outputs are written into build/project-output/benchmark/ through the {output}
# logical root, never into projects/ or tests/.
#
# Usage: scripts/benchmark.sh [options] [project ...] [-- <extra videosynth args>]
#        scripts/benchmark.sh --list

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${VIDEOSYNTH_BUILD_DIR:-$repo_root/build}"
binary="${VIDEOSYNTH_BINARY:-$build_dir/videosynth}"
project_dir="$repo_root/projects/benchmark"
output_root="${VIDEOSYNTH_BENCHMARK_OUTPUT:-$build_dir/project-output/benchmark}"

all_projects=(pal_still pal_still_noise pal_mkv pal_sections pal_skip ntsc_still)
thread_configs=(1 auto)
repeat=1
csv_path=""

usage() {
  cat >&2 <<EOF
Usage: $(basename "$0") [options] [project ...] [-- <extra videosynth args>]

Projects: ${all_projects[*]} (default: all)

Options:
  --threads "<list>"   Thread configurations to time (default: "1 auto")
  --repeat <n>         Time each configuration n times, report the best
  --output-root <dir>  Where {output} resolves (default: $output_root)
  --csv <path>         Also append the results as CSV
  --list               List the benchmark projects and exit
  --help, -h           Show this help
EOF
}

projects=()
extra_args=()
while (($#)); do
  case "$1" in
    --list)
      for name in "${all_projects[@]}"; do
        echo "$name: projects/benchmark/$name.yaml"
      done
      exit 0
      ;;
    --threads)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      read -r -a thread_configs <<<"$2"
      shift 2
      ;;
    --repeat)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      repeat="$2"
      shift 2
      ;;
    --output-root)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      output_root="$2"
      shift 2
      ;;
    --csv)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      csv_path="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      extra_args=("$@")
      break
      ;;
    -*)
      usage
      exit 2
      ;;
    *)
      if [[ ! -f "$project_dir/$1.yaml" ]]; then
        echo "Error: unknown benchmark project '$1'. Known: ${all_projects[*]}" >&2
        exit 2
      fi
      projects+=("$1")
      shift
      ;;
  esac
done

if ((${#projects[@]} == 0)); then
  projects=("${all_projects[@]}")
fi

if [[ ! "$repeat" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: --repeat expects a positive integer, got '$repeat'" >&2
  exit 2
fi

# The dev shell supplies ffmpeg/ffprobe for the MKV-sourced benchmark.
if [[ -z "${IN_NIX_SHELL:-}" ]] || ! command -v ffprobe >/dev/null 2>&1; then
  exec nix develop "path:$repo_root" --command "${BASH_SOURCE[0]}" "$@"
fi

if [[ ! -x "$binary" ]]; then
  echo "Error: expected executable not found: $binary" >&2
  echo "Build first with: nix develop \"path:$repo_root\" --command cmake --build \"$build_dir\"" >&2
  exit 1
fi

mkdir -p "$output_root"
log_dir="$output_root/logs"
mkdir -p "$log_dir"

cd "$repo_root"

echo "Binary : $binary"
echo "Output : $output_root"
echo "Repeat : $repeat (best of)"
echo

results=()

# Runs one project once and prints "<seconds> <frames> <threads_used>".
time_run() {
  local project_path="$1"
  local threads="$2"
  local log_file="$3"
  local start end

  # Flush the previous run's dirty pages so write-back does not land inside
  # the next measurement.
  sync

  start="$(date +%s.%N)"
  if ! "$binary" --project "$project_path" --output-root "$output_root" \
      --threads "$threads" --log-level info \
      "${extra_args[@]+"${extra_args[@]}"}" >"$log_file" 2>&1; then
    echo "Error: run failed for $project_path (--threads $threads)" >&2
    tail -n 20 "$log_file" >&2
    return 1
  fi
  sync
  end="$(date +%s.%N)"

  local frames threads_used
  # Plain runs log "Generating and writing N frame(s)"; disc-skip runs log
  # "Generating N disc frame(s) with skip plan", where the disc frame count is
  # the work the run actually did.
  frames="$(grep -oE 'Generating and writing [0-9]+ frame' "$log_file" |
    grep -oE '[0-9]+' | head -n 1 || true)"
  if [[ -z "$frames" ]]; then
    frames="$(grep -oE 'Generating [0-9]+ disc frame' "$log_file" |
      grep -oE '[0-9]+' | head -n 1 || true)"
  fi
  threads_used="$(grep -oE 'using [0-9]+ synthesis threads' "$log_file" |
    grep -oE '[0-9]+' | head -n 1 || true)"
  [[ -n "$frames" ]] || frames=0
  [[ -n "$threads_used" ]] || threads_used=1

  awk -v s="$start" -v e="$end" -v f="$frames" -v t="$threads_used" \
    'BEGIN { printf "%.3f %d %d\n", e - s, f, t }'
}

for name in "${projects[@]}"; do
  project_path="$project_dir/$name.yaml"
  for threads in "${thread_configs[@]}"; do
    best_seconds=""
    frames=0
    threads_used=1
    for ((run = 1; run <= repeat; ++run)); do
      log_file="$log_dir/${name}_threads-${threads}_run-${run}.log"
      run_output="$(time_run "$project_path" "$threads" "$log_file")"
      read -r seconds frames threads_used <<<"$run_output"
      if [[ -z "$best_seconds" ]] ||
        awk -v a="$seconds" -v b="$best_seconds" 'BEGIN { exit !(a < b) }'; then
        best_seconds="$seconds"
      fi
      printf '  %-16s threads=%-4s run %d/%d: %ss\n' \
        "$name" "$threads" "$run" "$repeat" "$seconds"
    done
    results+=("$name|$threads|$threads_used|$frames|$best_seconds")
  done
done

echo
printf '%-16s %-8s %-8s %-8s %-10s %-10s\n' \
  Project Threads Workers Frames Seconds Frames/s
printf '%-16s %-8s %-8s %-8s %-10s %-10s\n' \
  ---------------- -------- -------- -------- ---------- ----------
for row in "${results[@]}"; do
  IFS='|' read -r name threads threads_used frames seconds <<<"$row"
  fps="$(awk -v f="$frames" -v s="$seconds" \
    'BEGIN { printf "%.2f", (s > 0 ? f / s : 0) }')"
  printf '%-16s %-8s %-8s %-8s %-10s %-10s\n' \
    "$name" "$threads" "$threads_used" "$frames" "$seconds" "$fps"
done

if [[ -n "$csv_path" ]]; then
  mkdir -p "$(dirname "$csv_path")"
  if [[ ! -s "$csv_path" ]]; then
    echo "project,threads,workers,frames,seconds,frames_per_second" >"$csv_path"
  fi
  for row in "${results[@]}"; do
    IFS='|' read -r name threads threads_used frames seconds <<<"$row"
    fps="$(awk -v f="$frames" -v s="$seconds" \
      'BEGIN { printf "%.2f", (s > 0 ? f / s : 0) }')"
    echo "$name,$threads,$threads_used,$frames,$seconds,$fps" >>"$csv_path"
  done
  echo
  echo "CSV results appended to $csv_path"
fi
