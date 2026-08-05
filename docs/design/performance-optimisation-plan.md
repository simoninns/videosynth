# Generation Performance Optimisation Plan

## Purpose

Long-form projects (for example
[projects/long-form/pal_three_hour.yaml](../../projects/long-form/pal_three_hour.yaml),
270,000 frames / ~383 GB) currently take many CPU-hours to render. This plan
describes a phased set of optimisations for the generation pipeline, ordered
from low-risk build/hoisting fixes to an architectural frame-template cache
that exploits the exact periodicity of the PAL and NTSC colour sequences.

Related documents:

- [High-level design](high-level-design.md)
- [TESTING.md](../../TESTING.md)
- Analogue standards: [docs/analogue-video-specifications](../analogue-video-specifications/README.md)
- CVBS format: [docs/cvbs-file-format-specification](../cvbs-file-format-specification/README.md)

## Measured baseline

Profiled with `perf` (754-frame PAL project: one 500-frame EXR still section
plus one MKV section, six project-wide VITS, one analogue audio pair),
single-threaded. Shares below are from the **Release (-O3)** build; the
ranking on the default unoptimised build is the same:

| Cost centre | Share | Notes |
| --- | --- | --- |
| `ApplyFirFilterFixed` (chroma FIR) | ~21 % | 33-tap int64 FIR, 2 axes × 576 active lines/frame ≈ 35 M MAC/frame (PAL) |
| ffmpeg/ffprobe (MKV sections) | ~35 % | `ffprobe -count_frames` decodes the whole clip, then `ffmpeg` decodes it again |
| Output writing (`OutputStage::AppendSamples` + iostream) | ~16 % | one `ostream::write` per 2-byte sample; stream sentry/xsputn overhead |
| `GenerateFrameBatch` per-sample loops | ~7 % | pixel mapping, sync shaping, per-sample invariants |
| `__llround` (libm) | ~5.6 % | `MillivoltsToSampleFixed` on every sample write |

Wall-clock, 16-core machine:

| Run | Build | 1 thread | 16 threads | Scaling |
| --- | --- | --- | --- | --- |
| Still + MKV, 754 frames | default (no `-O`) | 67.4 s | 43.5 s | 1.55× |
| Still + MKV, 754 frames | Release `-O3` | 33.8 s | 16.8 s | 2.0× |
| Still only, 500 frames | default (no `-O`) | 39.6 s | 19.9 s | 2.0× |
| Still only, 500 frames | Release `-O3` | 16.5 s | 5.8 s | 2.8× |

Extrapolated to the three-hour PAL project (270,000 frames), the current
best case (Release, 16 threads, mostly stills) is on the order of 1.7–2.5
hours of wall-clock, excluding the ~383 GB of disk writes.

Two structural findings frame the whole plan:

1. **The default local build is unoptimised.** `CMakeLists.txt` never sets
   `CMAKE_BUILD_TYPE`; only the Nix package build passes
   `-DCMAKE_BUILD_TYPE=Release` (`flake.nix`). A plain
   `cmake -S . -B build` therefore compiles with no `-O` flag at all — and
   simply rebuilding with `-O3` is already a 2.0–2.8× win (table above).
2. **Thread scaling is poor.** User CPU time is essentially unchanged
   between 1 and 16 threads; the wall-clock gain is capped by serial work —
   source probing and decode at section start, the single consumer thread
   doing per-sample output encoding and 2-byte writes, and a per-frame
   source-image deep copy performed under a global mutex.

## Exploitable structure

The synthesised signal is far more repetitive than the current code assumes:

- **Subcarrier lattice.** The carrier advances exactly π/2 per sample (4fsc),
  so its phase depends only on `absolute_sample_index mod 4`. Samples per
  frame: PAL 709,379 (mod 4 = 3), NTSC 477,750 (mod 4 = 2), PAL-M 477,225
  (mod 4 = 1).
- **Colour sequence period.** Combining the lattice with PAL V-switch/burst
  meander (period 2 frames): a fully synthesised frame with identical source
  content repeats **exactly every 4 frames (8 fields) for PAL/PAL-M and every
  2 frames (4 fields) for NTSC**.
- **Pilot burst is period-1.** 17,734,475 = 25 × 709,379, so the PAL
  LaserDisc pilot burst phase advance per frame is an integer number of
  cycles; the waveform is identical on every frame.
- **Frame-invariant content.** Sync/blanking skeleton, burst envelopes, VITS
  lines, and the white flag are pure functions of `(standard, line)` or
  `(standard, line, frame mod period)`; only the source image, VBI code
  words, OSD text, noise, and dropouts genuinely vary per frame.
- **Repetitive sources.** Still sections repeat one source frame for
  thousands of output frames; `duration_repeat` MKV sections cycle a short
  clip. Per-section source frames are immutable once decoded.

## Constraints

- Output must remain **byte-identical across thread counts and runs**
  (`tests/functional/test_deterministic_output.cpp`). Each phase states
  whether it is bit-exact against the previous baseline; changes that alter
  output bits (noise draw order, phase-argument reduction) must be called out
  and re-baselined deliberately, never silently.
- Signal correctness is governed by the specs in
  [docs/analogue-video-specifications](../analogue-video-specifications/README.md);
  no optimisation may trade spec fidelity for speed.
- Follow [AGENTS.md](../../AGENTS.md) §5: profile before and after every
  phase; flag any >10 % memory or >5 % runtime regression.
- Unit tests per [TESTING.md](../../TESTING.md); whole-pipeline comparisons
  are functional tests.

---

## Phase 1 — Build configuration and measurement harness

Fixing the build type is likely the single largest win and costs nothing;
the benchmark harness makes every later phase measurable.

### Task 1.1 — Default to an optimised build

Make `CMakeLists.txt` default `CMAKE_BUILD_TYPE` to `Release` when the user
has not set it (standard guard: only when `CMAKE_BUILD_TYPE` is empty and no
multi-config generator is in use), and print the chosen type at configure
time. Document the override for debug work in `README.md`.

*Acceptance criteria*
- A fresh `cmake -S . -B build` with no extra flags compiles with
  `-O3 -DNDEBUG`.
- `cmake -DCMAKE_BUILD_TYPE=Debug` still produces a debug build.
- Nix package build behaviour is unchanged.

*Measured*: a fresh configure reports `Build type: Release` and every
compile command carries `-O3 -DNDEBUG`; `-DCMAKE_BUILD_TYPE=Debug` reports
`Build type: Debug` with no `-O` flag. The Nix package build already passes
`-DCMAKE_BUILD_TYPE=Release` explicitly (`flake.nix`), so the default never
applies there. Multi-config generators are left to choose per build.

### Task 1.2 — Benchmark script and reference projects

[scripts/benchmark.sh](../../scripts/benchmark.sh) runs the fixed-length
projects in [projects/benchmark/](../../projects/benchmark/) — PAL still, PAL
still with noise, PAL moving source (MKV) and NTSC still — once per thread
configuration, and prints wall-clock and frames/second. Frame counts are read
from the CLI's own log line, and all media goes to
`build/project-output/benchmark/` through the `{output}` root.

*Acceptance criteria*
- One command produces a frames/second table for both thread configurations.
- Outputs are written via `{output}`, never into `projects/` or `tests/`.

*Measured* (16-core machine, Release `-O3`, one run per configuration on a
cold output tree):

| Project | 1 thread | auto (16) | Scaling |
| --- | --- | --- | --- |
| pal_still (250 frames) | 12.06 f/s | 30.01 f/s | 2.5× |
| pal_still_noise (250 frames) | 8.04 f/s | 22.70 f/s | 2.8× |
| pal_mkv (254 frames) | 7.67 f/s | 10.18 f/s | 1.3× |
| ntsc_still (250 frames) | 20.44 f/s | 33.77 f/s | 1.7× |

Repeated runs over a warm page cache and pre-allocated output files are
roughly 2.5× faster again (pal_still: 30.6 f/s single-threaded), so compare
like with like — `--repeat` reports the best run of N.

### Task 1.3 — Output-hash comparison tool

[scripts/output-hashes.sh](../../scripts/output-hashes.sh) records and compares
SHA-256 manifests of every generated `.cvbs`/`.cvbsy`/`.cvbsc`, audio `.wav`
and `.meta` sidecar across the `general` and `stacking` suites, so
"byte-identical to baseline" can be asserted before and after each phase. The
sidecar is hashed as its canonical `sqlite3 .dump` rather than as raw bytes:
the recorded rows are what the format specifies, and page layout can differ
between SQLite builds.

*Acceptance criteria*
- Running the tool twice on the same build reports identical hashes.
- The tool flags any file whose hash differs from a recorded baseline.

*Measured*: 43 (general) + 228 (stacking) artefacts reproduce byte for byte
across independent runs of the same build.

### Task 1.4 — Evaluate compiler flags for the Release build

Measure `-fno-math-errno` (makes `llround`/`sqrt` inlinable), LTO/IPO, and
(optionally, behind a CMake option) `-march=` tuning on the benchmark suite.
Adopt only flags that are output-hash-neutral on the full project suite.

*Acceptance criteria*
- Flag set adopted or rejected with measured numbers recorded in the task.
- Output hashes unchanged for every adopted flag.

*Measured* (16-core machine, `scripts/benchmark.sh pal_still ntsc_still
pal_still_noise --threads "1" --repeat 2`, best of two, frames/second):

| Flag set on top of Release | pal_still | ntsc_still | pal_still_noise |
| --- | --- | --- | --- |
| none (baseline) | 30.57 | 52.15 | 18.28 |
| `-fno-math-errno` | 30.43 | 52.04 | 18.33 |
| IPO/LTO | 30.93 | 52.95 | 17.73 |
| `-march=native` | 30.37 | 51.74 | 18.23 |
| `-fno-math-errno` + IPO/LTO | 30.83 | 52.21 | 17.56 |

*Outcome*: **none adopted**. Every variant lands within ±2 % of the baseline —
below the 5 % review threshold in [AGENTS.md](../../AGENTS.md) §5 and inside
run-to-run variance — while LTO costs ~3 % on the noise path and adds link
time, and `-march=native` produces non-portable binaries that would have to be
kept out of the Nix package build anyway. `-fno-math-errno` does not pay off
because GCC at `-O3` already keeps the `llround` calls out of the critical
dependency chain; the remaining cost is better removed in Task 4.2/4.3 by not
calling it per sample at all. No CMake option is added for flags that earned
no measured gain.

The `-fno-math-errno` + IPO/LTO build reproduces the recorded `general` and
`stacking` baselines byte for byte (271 artefacts), so the flags stay available
as ordinary configure-time overrides
(`-DCMAKE_CXX_FLAGS=-fno-math-errno -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`)
should a later phase change the balance.

---

## Phase 2 — Remove frame-invariant recomputation

Pure hoisting: every task here produces bit-identical output and is
verifiable with the Phase 1 hash tool. Findings reference the current code.

### Task 2.1 — Quantise chroma filter kernels once

`QuantizeKernelToFixed` re-quantises the constant filter taps on **every
active line** (`src/chroma_encoder.cpp` — PAL, NTSC, and PAL-M encoders),
costing ~66 `llround` calls and two heap allocations per line. Quantise once
in each encoder constructor and store the fixed-point taps as members. Note
the U and V kernels are designed identically — design once, share.

*Acceptance criteria*
- No kernel quantisation inside `EncodeLineFromPhaseStart`.
- Output hashes unchanged; unit tests pass.

### Task 2.2 — Reuse per-line workspace buffers in the chroma encoders

`EncodeLineFromPhaseStart` allocates five `std::vector<int64_t>` per line
(~4,000 heap allocations per PAL frame). Extend the existing
`filtered_*_workspace_` mutable-member pattern to all per-line buffers.
Document the resulting thread-safety guarantee (one encoder per worker).

*Acceptance criteria*
- No per-line heap allocation in the encode path (verified by review and a
  heap profile of the benchmark run).
- Output hashes unchanged.

### Task 2.3 — Hoist per-frame setup out of the frame and batch loops

The pool and skip paths call `GenerateFrameBatch` with `frame_count == 1`,
so all "per batch" setup is per frame:

- `BuildSampledSynthesisContext` and `CreateChromaEncoder`
  (`src/generation_stage.cpp`) — pure functions of the video standard; build
  once per run (or per worker) and reuse.
- `BuildProjectVitsState` — currently rebuilt **inside the per-frame loop**
  despite being project-invariant; build once per run.
- `GetTimingConstants` / `GetSignalLevels` / `FindSectionIndex` lookups in
  `NoiseInjectionStage`, `DropoutInjectionStage`, and
  `BiphaseInjectionManager`; `BiphaseEncoder`/`FmEncoder` construction per
  frame in `InjectResolvedVbiLines`.

*Acceptance criteria*
- The listed functions run O(1) times per run (or per worker), not per frame.
- Output hashes unchanged across `projects/general` and `projects/stacking`.

### Task 2.4 — Cache rendered VITS lines

A rendered VITS line is a pure function of `(vits_type, line_samples,
standard)` yet `VitsGenerator::RenderLine` re-synthesises it (including
per-sample `std::sin` with a division per sample) for every targeted line of
every frame. Render each configured VITS line once per run and reuse the
sample buffer.

*Acceptance criteria*
- Per-frame cost of VITS injection reduces to buffer copies.
- Output hashes unchanged.

### Task 2.5 — Cache frame-invariant biphase artefacts

The white flag waveform and the constant portions of biphase/FM encoding
(encoder construction runs two 48-iteration inverse-S-curve searches;
baseline fill loops) are frame-invariant. Construct encoders once per run
and cache the white flag waveform per standard.

*Acceptance criteria*
- Encoders constructed once per run; white flag rendered once.
- Output hashes unchanged; laserdisc stacking projects verified.

---

## Phase 3 — Source delivery and output I/O

Removes the dominant serial-path costs that cap thread scaling. All tasks
are bit-exact.

### Task 3.1 — Share decoded source frames instead of copying

`ProgressiveFrameSource::GenerateFrame` deep-copies the cached image
(~2.4 MiB) **while holding `cache_mutex_`**, once per frame per worker
(`src/progressive_frame_source.cpp`). Return
`std::shared_ptr<const FrameSourceImage>` (decoded frames are immutable), so
delivery is a refcount increment. Widen the one-slot cache to two entries so
workers straddling a section boundary do not thrash re-decodes.

*Acceptance criteria*
- No per-frame image copy; no decode work under the lock on cache hits.
- Output hashes unchanged; measured multi-thread scaling improves on the
  still benchmark.

### Task 3.2 — Buffer output writes

`OutputStage::AppendSamples` issues one 2-byte `ostream::write` per sample
(~709 k calls per PAL frame) on the single consumer thread, and re-resolves
the quantisation profile and output encoding per call. Encode each frame
into a reusable `int16_t` buffer and issue one write per frame (or larger);
resolve profile/encoding once at `BeginWrite`.

*Acceptance criteria*
- One (or few) `write` calls per frame; encoding resolved once per run.
- Output hashes unchanged; consumer-thread share of the profile drops
  measurably.

### Task 3.3 — Eliminate redundant MKV decodes

MKV sections currently decode the file twice (`ffprobe -count_frames` to
count frames, then `ffmpeg` for pixels) plus additional probe invocations.
Count frames from the single decode pass (or container metadata with a
decode-pass fallback) and reuse probe results across the run.

*Acceptance criteria*
- Exactly one full decode per MKV source per run.
- Frame counts, output hashes, and validation behaviour unchanged, including
  for `duration_frames: all`.

### Task 3.4 — Stop copying frames into the disc-skip cache

The skip path stores full Y/C buffer copies in its frame cache and forces
single-threaded synthesis (`src/pipeline.cpp`). Store `shared_ptr` buffers to
halve memory traffic. (Parallelising the skip path itself is Phase 6.)

*Acceptance criteria*
- No full-buffer copies on cache insert/hit.
- Output hashes unchanged for all `projects/stacking` skip projects.

---

## Phase 4 — Inner-loop optimisation

Targets the ~27 % chroma FIR and the per-sample loops. Tasks 4.1–4.3 are
bit-exact; Task 4.4 changes output bits and is gated behind explicit
re-baselining.

### Task 4.1 — Vectorisable fixed-point FIR

`ApplyFirFilterFixed` multiplies int64×int64 through vector-of-vector
indirection, which neither auto-vectorises (no AVX2 64-bit multiply) nor
allows alias analysis. Coefficients are Q30 and chroma axis samples fit
int32; convert to `int32 × int32 → int64` accumulation over `__restrict`
raw pointers, with `static_assert`/range tests proving no overflow across
the legal mV range. Also remove the fixed→double→fixed round trip between
filtering and modulation (a single integer rescale), keeping rounding
semantics identical.

*Acceptance criteria*
- Compiler reports vectorisation of the FIR loop (or intrinsics measured
  faster); benchmark shows a substantial drop in `ApplyFirFilterFixed` share.
- Output hashes unchanged (rounding proven equivalent by unit test over the
  full input range).

### Task 4.2 — Hoist invariants out of the per-sample picture loop

In the active-picture loop (`src/generation_stage.cpp`): the
`section->type == "progressive"` string comparison, `source_row`
computation, and two always-false bounds checks execute per sample, and
`MapActiveSampleToSourcePixel` performs an integer division per sample.
Precompute a per-standard `x_sample → pixel_x` table (922/745 entries) once
per run, and lift the line-invariant values out of the loop.

*Acceptance criteria*
- No division, string comparison, or redundant bounds check in the
  per-sample loop.
- Output hashes unchanged.

### Task 4.3 — Table-driven sync, burst, and pilot waveforms

`ShapedPulseLevel` is called per sample of every pulse (~52 k calls/frame)
though pulses are pure functions of `(standard, pulse kind)`;
`ShapedGateEnvelope` likewise per burst sample; the pilot burst computes
`floor`-based triangle phase per sample although its waveform is identical
every frame (17,734,475 = 25 × 709,379). Precompute per-standard pulse
tables, the burst envelope table, and the per-line pilot burst segments once
per run, then copy/add. Remove the dead `pilot_cos_delta`/`pilot_sin_delta`
locals.

*Acceptance criteria*
- Sync/burst/pilot inner loops reduce to table reads.
- Output hashes unchanged.

### Task 4.4 — Exact subcarrier phase reduction (output re-baseline)

Burst and chroma phases are computed as `(π/2) · absolute_sample_index` with
an unbounded index, losing precision over long runs (≈10⁻⁵ rad by frame
100,000). Reduce the index mod 4 before the multiply: faster, and exact for
the life of the render. This changes low-order output bits relative to the
current implementation; it is an accuracy improvement, not a regression.

*Acceptance criteria*
- Phase arguments bounded to [0, 2π) regardless of render length.
- Decoded output verified against decode-orc on PAL and NTSC reference
  projects; hash baselines regenerated in the same change with the
  difference explicitly documented.

### Task 4.5 — Noise stage draw efficiency (output re-baseline for noise projects)

`InjectNoise` constructs a fresh `std::normal_distribution` per sample,
discarding the Box–Muller pair and re-deriving σ via `sqrt` per sample. Draw
a unit normal from a persistent distribution and scale by σ (σ² is cheap to
maintain incrementally). This changes the consumed RNG stream, so noise
bytes differ while remaining deterministic per seed and statistically
identical; noise-enabled baselines are regenerated deliberately.

*Acceptance criteria*
- One distribution per frame region; no per-sample `sqrt` where σ is
  constant along a region.
- Determinism harness still passes (identical output across runs/threads);
  statistical unit test on noise amplitude distribution passes.

---

## Phase 5 — Periodic frame-template cache

The architectural change: for constant source content, a clean synthesised
frame is an exact function of `(section, source_frame_index,
disc_frame_index mod P)` with P = 4 (PAL/PAL-M) or 2 (NTSC). A still section
therefore contains only P distinct clean frames; MKV `duration_repeat`
sections contain `P × clip_length` (bounded by lcm alignment). Everything
that genuinely varies per frame — VBI code words, OSD text, noise, dropouts
— is a localised patch applied after a template copy.

### Task 5.1 — Split synthesis into clean template and per-frame patches

Refactor `GenerateFrameBatch` so the clean frame (sync, burst, pilot,
active picture, VITS) is produced by a `SynthesiseTemplate(section,
source_frame, sequence_phase)` function, with VBI biphase/FM injection and
OSD rendering applied afterwards as patches to a copy. No caching yet; this
is the seam.

*Acceptance criteria*
- Patch application touches only the affected lines/samples.
- Output hashes unchanged across the full project suite.

### Task 5.2 — Template cache keyed on (section, source frame, sequence phase)

Add a bounded cache of clean templates (Y and C buffers held via
`shared_ptr` to const data). Lookup key: section index, source frame
identity,
`disc_frame_index mod P`, where P comes from the standard. Sizing: stills
need P entries per section (~45 MB for PAL); MKV sections need
`P × clip_frames` entries — cap the cache (configurable, default sized for
stills and short clips) and fall back to direct synthesis on miss. Workers
populate on demand with per-key single-flight locking so the pool stays
scalable.

*Acceptance criteria*
- Cache hit delivers a frame via memcpy + patches with no line-loop work.
- Byte-identical output with the cache enabled vs disabled, across
  `projects/general`, `projects/stacking`, and an MKV `duration_repeat`
  project, at 1 and auto threads (functional test toggling the cache).
- Still-section benchmark throughput improves by an order of magnitude;
  memory stays within the configured cap.

### Task 5.3 — Skip-path integration

The disc-skip path already caches per-disc-frame output because skips replay
frames; re-key it on the Task 5.2 template cache (plus per-frame patches) so
skip projects also benefit and the extra full-buffer cache disappears.

*Acceptance criteria*
- All `projects/stacking` skip projects byte-identical to pre-change output.
- Skip-path memory use reduced (no second whole-frame cache).

### Task 5.4 — HLD update

Document the template/patch architecture, cache keys, periodicity rationale
(subcarrier lattice mod 4; PAL V-switch/meander period 2; pilot burst
period 1) and memory bounds in
[high-level-design.md](high-level-design.md), per AGENTS.md §9.2.

*Acceptance criteria*
- HLD describes the implemented behaviour; no code/doc mismatch remains.

---

## Phase 6 — Pipeline concurrency

After Phases 2–5 the consumer thread and serial section work dominate again;
this phase attacks them. All tasks are bit-exact.

### Task 6.1 — Move output encoding onto workers

`OutputStage::AppendSamples` currently converts fixed-point samples to
output codes on the consumer thread. Have workers produce the encoded
`int16_t` frame buffer (order-independent, pure function) so the consumer
only sequences writes and sidecar commits.

*Acceptance criteria*
- Consumer thread work per frame reduces to `write` + sidecar bookkeeping.
- Determinism harness passes; output hashes unchanged.

### Task 6.2 — Pool frame buffers

Each in-flight frame allocates ~11.35 MB (PAL) of fresh `int64_t` buffers;
the reassembly window holds `2 × threads` frames. Recycle buffers through a
free-list sized to the window to eliminate steady-state allocation and
page-faulting.

*Acceptance criteria*
- Steady-state generation performs no frame-buffer allocations (heap
  profile).
- Peak RSS on the 16-thread PAL benchmark does not increase.

### Task 6.3 — Overlap source decode with synthesis

Section-start decode (EXR convert, MKV full decode) stalls all workers.
Prefetch the next section's source asynchronously while the current section
renders.

*Acceptance criteria*
- No measurable worker idle gap at section boundaries in the long-form
  benchmark trace.
- Output hashes unchanged.

### Task 6.4 — Re-enable parallel synthesis for disc-skip projects

The presence of any disc skip forces the whole run single-threaded
(`src/pipeline.cpp`). With the Phase 5 template cache and deterministic
per-frame patches, skip projects can use the pool with ordered reassembly.

*Acceptance criteria*
- Skip projects run multi-threaded with byte-identical output vs
  `--threads 1`.
- Measured wall-clock improvement on `projects/stacking` skip projects.
