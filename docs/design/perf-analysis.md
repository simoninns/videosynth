# videosynth Performance Analysis (Single-Threaded)

## Purpose and scope

This document analyzes current single-threaded performance in videosynth with focus on:

- Chroma synthesis and modulation path
- Output quantization and file write path

It also proposes optimizations that preserve output quality.

Out of scope:

- Multithreading and parallel execution
- Any behavior change that alters legal waveform timing, burst phase behavior, or encoded 10-bit output expectations

## Measurement setup

Date: 2026-05-28

Workloads profiled:

- tests/projects/pal_32f_bars_ramp.yaml
- tests/projects/ntsc_32f_bars_ramp.yaml

Profiler:

- gprof using a temporary profiling build with -pg

Command sequence used:

1. Configure profiling build:
   - nix develop "path:$PWD" --command cmake -S . -B ./build-gprof -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-pg
2. Build profiling target:
   - nix develop "path:$PWD" --command cmake --build ./build-gprof -j
3. Run PAL and NTSC fixtures and capture gmon profiles
4. Generate reports:
   - nix develop "path:$PWD" --command gprof ./build-gprof/videosynth ./build/gmon.pal.out > ./build/gprof.pal.txt
   - nix develop "path:$PWD" --command gprof ./build-gprof/videosynth ./build/gmon.ntsc.out > ./build/gprof.ntsc.txt

Temporary-shell note:

- Attempted valgrind and callgrind in Nix shell, but valgrind was not present in the current development shell and this Nix version rejected multi-installable develop syntax for an ad-hoc overlay.
- gprof still provides sufficient hotspot ranking for this optimization plan.

## Baseline observations

### End-to-end profile highlights (PAL synthetic fixture)

From build/gprof.pal.txt flat profile:

- ApplyFirFilter: 52.31% self time
- OutputStage::Write: 13.17% self time
- GenerationStage::Generate: 12.81% self time
- std::vector<double>::_M_fill_assign: 12.46% self time
- PalChromaEncoder::EncodeLine: 3.91% self time (71.2% inclusive in call graph)

Interpretation:

- FIR chroma filtering dominates runtime.
- Memory churn and repeated vector fill/reset operations are material.
- Output stage quantization and encoding loop is the second major domain after chroma filtering.

### End-to-end profile highlights (NTSC synthetic fixture)

From build/gprof.ntsc.txt flat profile:

- ApplyFirFilter: 35.29% self time
- GenerationStage::Generate: 19.12% self time
- std::vector<double>::_M_fill_assign: 17.65% self time
- OutputStage::Write: 14.71% self time
- NtscChromaEncoder::EncodeLine: 3.68% self time (58.8% inclusive in call graph)

Interpretation:

- Same dominant pattern as PAL, with lower FIR share due to fewer taps.
- Output and vector fill overhead remain significant.

### Structure-driven reasons for these costs

Relevant implementation points:

- src/chroma_encoder.cpp:
  - Per-line extraction of Cb and Cr into temporary vectors
  - Per-line FIR filtering creating full temporary filtered vectors
  - Per-sample trigonometric modulation using sin and cos
- src/generation_stage.cpp:
  - Per-line creation/fill of temporary vectors for source samples, phase arrays, and active sample indices
  - Carrier phase computed per sample from time value
- src/output_stage.cpp:
  - Per-sample floating-point quantization via lround
  - Per-sample format conversion branch
  - Second full pass to recompute nonstandard/clip condition after data already written

## Optimization opportunities (single-threaded only)

## 1) Reduce chroma FIR cost (highest impact)

### Current

- Generic FIR loop does index clamp per tap per output sample.
- PAL uses 33 taps and NTSC uses 17 taps.
- Two FIR passes per active line (U and V, or Cb and Cr).

### Improvements

1. Replace generic edge-clamped FIR with a fixed-tap, fixed-standard kernel path:
   - Pre-pad line buffers once (replicated edges), then run a branch-free inner loop.
   - Remove per-tap clamp and min/max in inner loop.
2. Store taps in contiguous aligned arrays and unroll inner loops for 17 and 33 taps.
3. Reuse per-line scratch buffers instead of allocating fresh vectors each line.

Why quality is preserved:

- Kernel coefficients and arithmetic order can remain equivalent (or numerically tighter if using deterministic fixed-point design).
- No timing or bandwidth target changes are required.

## 2) Collapse temporary vector churn in chroma path (high impact)

### Current

- ExtractCbAxis and ExtractCrAxis build new vectors every line.
- ApplyFirFilter returns new vectors every call.
- EncodeLine then fill-assigns output vector.

### Improvements

1. Introduce reusable line workspaces:
   - cb_axis
   - cr_axis
   - filtered_cb
   - filtered_cr
   - encoded_line
2. Allocate once per frame or once per stage instance, resize without reallocation.
3. Fuse stages where practical:
   - Convert source samples to normalized Cb/Cr directly into filter input workspace.
   - Optionally fuse filter output and modulation step to avoid materializing both filtered vectors simultaneously.

Why quality is preserved:

- This is primarily memory-lifecycle optimization with unchanged math.

## 3) Precompute carrier phase components for locked 4fsc pipeline (high impact)

### Current

- Per-sample phase is computed from sample index and fed into sin/cos in the chroma loop.
- Similar trigonometric work appears in burst generation path.

### Improvements

1. Precompute a 4-sample periodic sin/cos sequence for the nominal subcarrier steps at 4fsc.
2. For NTSC/PAL line-specific phase offsets, apply a per-line complex rotation once, then use recurrence or table lookup in the inner loop.
3. Precompute burst sin sequence for the burst window length and apply only envelope scaling in loop.

Why quality is preserved:

- The same waveform phase model is used, with mathematically equivalent evaluation.
- Phase offset application can remain exact within floating-point tolerance or be quantized with far finer granularity than 10-bit output LSB.

## 4) Optimize output quantization and encoding loop (medium to high impact)

### Current

- For each sample:
  - composite = y + c
  - quantization with floating-point division and lround
  - format conversion branch by preset
- After writing all samples, a second full pass recomputes nonstandard condition.

### Improvements

1. Merge nonstandard detection into the primary write loop and remove second full scan.
2. Hoist preset branch outside sample loop by selecting an encoder lambda/function pointer once.
3. Replace per-sample division by multiplication with precomputed reciprocal.
4. Write in blocks using an intermediate encoded buffer rather than one sample write call per iteration.

Why quality is preserved:

- Quantizer formula remains the same.
- Output bitstream and metadata semantics stay unchanged.

## 5) Transition internal sample representation toward integer/fixed-point (strategic high impact)

The current pipeline uses floating-point Y and C sample vectors, then quantizes to 10-bit code space. Given final target encoding is 10-bit at 4fsc, fixed-point is a viable path if headroom is retained for excursions (including PAL line-dependent burst behavior and pilot/burst structures).

### Recommended integer strategy

1. Keep processing in a high-resolution fixed-point domain, not directly 10-bit:
   - Example: signed Q-format in millivolt-like units (for example Q10.6 or Q12.8 equivalent scale)
2. Preserve safety margin for overshoot/headroom:
   - Include sync tip, white, chroma excursions, and additional margin for legal and intentional out-of-range states before final clamp.
3. Perform final quantization once to 10-bit code domain in output stage.

### Why this can be faster

- Integer add/multiply and saturating clamp generally outperform repeated floating-point division/lround in tight loops.
- Fixed-point enables SIMD-friendly inner loops without changing single-thread model.

### Quality guardrails for fixed-point migration

Set acceptance criteria against floating-point reference output:

- Max absolute quantized error <= 0.5 code at the final 10-bit quantization boundary
- RMS error target significantly below 0.5 code
- No increase in clipped sample count for legal pattern fixtures
- Burst phase and amplitude checks remain within current tolerance envelopes

If these are met, output quality is operationally unchanged at 10-bit target resolution.

## 6) Specialize for fixed pipeline format and color space (medium impact)

The processing chain is already specialized to one color space family and 4fsc timing behavior. More specialization can reduce overhead:

1. Split standard-specific hot paths explicitly:
   - PAL and NTSC dedicated inner-loop codepaths
   - Compile-time constants for taps, line widths, burst windows
2. Replace generic dynamic checks in per-sample loops with prevalidated stage configuration objects.
3. Precompute per-line immutable descriptors once per frame:
   - active window bounds
   - burst start/end
   - burst phase mode
   - field-dependent axis inversion flags

Why quality is preserved:

- Behavior remains governed by the same timing model and waveform constraints, but with lower runtime branching.

## Prioritized implementation roadmap

Phase A (low-risk, immediate):

1. Remove output-stage second scan and hoist preset branch.
2. Introduce reusable line buffers to eliminate repeated vector allocations/fill-assign.
3. Precompute reciprocal quantization factor.

Phase B (higher impact, still low algorithmic risk):

1. Specialized branch-free FIR inner loops for PAL and NTSC tap counts.
2. Precompute carrier sequence primitives and reduce per-sample trig calls.

Phase C (strategic):

1. Introduce fixed-point intermediate representation.
2. Validate against floating-point reference using strict waveform and quantization equivalence tests.

## Expected gains (qualitative)

Based on current hotspot distribution:

- FIR plus associated chroma line-workspace overhead is the dominant optimization lever.
- Output stage loop improvements are the next strongest lever.
- Combined single-thread speedup target in realistic synthetic workloads should be substantial, with likely gains concentrated in:
  - 1.4x to 2.2x for generation+output total path after Phases A and B
  - Additional gains possible in Phase C depending on fixed-point implementation and compiler vectorization

These ranges are estimates and should be validated with repeatable benchmark fixtures after each phase.

## Verification plan after each optimization

1. Functional correctness:
   - Existing unit tests
   - Existing fixture project generation checks
2. Signal quality checks:
   - Compare encoded sample streams against baseline reference
   - Measure max code delta, RMS delta, and clipped sample counts
3. Performance checks:
   - Re-run the same PAL and NTSC synthetic fixtures
   - Re-run gprof and compare top-function percentages

## Summary

Current single-threaded performance is dominated by chroma FIR work and line-level temporary vector churn, with output quantization/writing as the second major hotspot. The best quality-preserving path is:

1. Reduce memory churn and redundant passes.
2. Specialize FIR and carrier math for the fixed 4fsc pipeline.
3. Migrate intermediate representation to fixed-point with strict output-equivalence gates.

This aligns with the target 10-bit output format while retaining headroom for PAL waveform structures and preserving observable output quality.
