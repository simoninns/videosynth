# The CLI

`videosynth` is the command-line generator. It runs the same pipeline as the GUI and produces identical output.

```bash
videosynth --project <path> [options]
videosynth --project <path> --validate [options]
```

## Options

| Option | Description | Default |
|--------|-------------|---------|
| `--project <path>` | YAML project file (required) | — |
| `--validate` | Validate only; generate nothing | off |
| `--version` | Print the build version (git commit hash) and exit | — |
| `--threads <n>` | Frame synthesis workers: `auto` or a positive integer | `auto` |
| `--template-cache-mb <n>` | Frame template cache capacity in MiB (`0` disables) | `512` |
| `--log-level <level>` | `info`, `debug` or `trace` | `info` |
| `--log-file <path>` | Also write logs to a file | none |
| `--asset-root <name>=<path>` | Map the `{name}/…` logical asset root (repeatable) | built-ins |
| `--output-root <path>` | Where `{output}/…` resolves | project directory |

The output path is not a command-line option — it comes from `output.video_path` in the project, and the metadata sidecar and audio tracks are colocated with it. What the command line controls is the *root* those paths resolve against.

Full details are in the [CLI reference](../reference/cli-options.md).

## Validating

```bash
videosynth --project projects/general/pal_vits.yaml --validate
```

Validation parses the file, applies every rule and probes the source files. It writes nothing. Errors go to stderr and the process exits non-zero; warnings are reported but do not fail the run.

Validate first whenever you are iterating on a project — it is fast, and it catches everything the generator would have complained about after minutes of work.

## Generating

```bash
videosynth --project projects/general/pal_vits.yaml
```

Progress is logged per batch of frames:

```text
[info] Starting pipeline: parse -> validate -> generate -> output
[info] Pipeline progress: 2438/2438 frame(s) written.
[info] Wrote 2438 frame(s) to output files.
[info] Generation completed successfully.
```

The exit code is zero on success and non-zero on failure.

## Choosing where output goes

By default `{output}` resolves to the project file's own directory, so a project run by hand stays self-contained. `--output-root` redirects it:

```bash
videosynth --project projects/stacking/pal_discsim_A.yaml \
    --output-root /mnt/scratch/videosynth
```

This is how batch runs keep generated media out of the source tree, and it is the reason projects should always name their artefacts through `{output}/…` rather than with a bare relative path.

`--asset-root` does the same for any other logical root, and can register new ones:

```bash
videosynth --project my.yaml \
    --asset-root bundled=/opt/videosynth/assets \
    --asset-root archive=/mnt/media/masters
```

A project can then reference `{archive}/clip.mkv`. See [Asset roots](../reference/asset-roots.md).

## Threads and the template cache

```bash
# Pure sequential path
videosynth --project my.yaml --threads 1

# Disable the frame template cache
videosynth --project my.yaml --template-cache-mb 0
```

Neither changes a byte of the output. `--threads 1` is useful when reasoning about a problem or profiling; the template cache trades memory for speed and can be reduced on a memory-constrained machine.

## Logging

```bash
videosynth --project my.yaml --log-level debug --log-file run.log
```

| Level | What you get |
|-------|--------------|
| `info` | Lifecycle: stage transitions, validation summary, frame progress |
| `debug` | Configuration details, branch decisions, resolved paths |
| `trace` | Per-frame, per-line and per-sample state |

`trace` produces an enormous amount of output on anything but a very short project — use it with `--log-file` and a handful of frames.

## Scripting

The CLI is designed to be driven from scripts: it is deterministic, it exits non-zero on failure, and every path it touches is redirectable.

```bash
#!/usr/bin/env bash
set -euo pipefail

for project in projects/general/*.yaml; do
    echo "== $project"
    videosynth --project "$project" --output-root build/project-output/general
done
```

The repository ships [several such scripts](../misc/example-projects.md) for running whole example suites, benchmarking and comparing output hashes between builds.
