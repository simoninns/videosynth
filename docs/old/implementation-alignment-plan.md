# VideoSynth Implementation Alignment Plan

## Purpose

This document records the current implementation boundary, identifies the main gaps against the HLD and the referenced analogue and digital interface specifications, and breaks the follow-up work into reviewable delivery stages.

It is intended to be used with the HLD after the HLD has been aligned to the current runtime behavior.

## Current Baseline

The current runtime is a locked-`4fsc`, sampled-domain composite generator.

- Generation produces frame-batched Y and C sample buffers directly on the `4fsc` lattice.
- Internal storage between generation and output uses fixed-point millivolt samples.
- Chroma filtering, carrier-phase generation, and several shaping operations still use floating-point math internally.
- Output combines Y and C, then either quantises into the supported CVBS code spaces or writes a resampled raw signed-16-bit stream, depending on the selected output encoding.
- The runtime still rejects unlocked modes, but it now accepts the supported non-`4fsc` output presets in the output stage.
- The YAML/runtime model does not yet represent line injections, laserdisc overlays, VITS, or VITC.

Progressive horizontal mapping baseline:

- The current generation path maps active-line sample index to source-pixel index using integer division of the form `floor((s * W) / N_a)` plus source active offset.
- For deterministic preservation of source detail on the `4fsc` lattice, this mapping contract must be explicitly documented and verified for PAL/NTSC and both `704` and `720` source widths.

## Gap Matrix

| **Area** | **Current Implementation** | **Gap vs HLD / Spec Target** | **Risk / Impact** | **Planned Stage** |
| --- | --- | --- | --- | --- |
| Architecture split | Direct sampled-domain `4fsc` synthesis plus quantise/write | HLD previously described continuous-time generation followed by separate sampling | Design confusion and incorrect future assumptions | Stage 1 |
| Runtime scope | Locked `4fsc` only | HLD and YAML design describe unlocked plus 20 MSPS / 40 MSPS / custom rates | Users and future code may assume unsupported modes exist | Stage 1 |
| Internal arithmetic | Mixed fixed-point buffers plus floating-point FIR, phase, and modulation math | Target direction is a more deterministic fixed-point signal core | Cross-platform determinism and auditability are weaker than they could be | Stage 2 |
| Subcarrier lock model | Implicit lock via absolute sample index on `4fsc` lattice | No explicit NCO / phase-accumulator abstraction for future unlocked or resampled outputs | Hard to extend cleanly to alternate clocks or output rates | Stage 2 |
| Digital output profiles | `CVBS_U10_4FSC`, `CVBS_U16_4FSC`, `CVBS_TPG21_4FSC`, `RAW_S16_28M`, and `RAW_S16_40M` are accepted in the output stage | The runtime still uses 4fsc generation internally, but output now supports the wider preset matrix | Remaining work is non-4fsc generation/refinement rather than preset acceptance | Stage 3 |
| Analogue feature surface | Sync, burst, blanking, active-picture placement, progressive ingestion, test patterns | No runtime VITS, VITC, laserdisc biphase, PAL pilot burst, or NTSC VBI burst | Major functional/spec coverage gap for VBI use cases; intentionally deferred until the current base runtime is stable | Deferred |
| YAML/project model | Parser and validator cover only the reduced current subset | `line_injections` and laserdisc preset fields are not represented in the runtime model | Design contract cannot be executed end-to-end | Stage 1 |
| Progressive horizontal mapping | Integer-domain sample-to-pixel mapping exists in generation, but contract and test matrix were previously implicit | Need normative, test-backed mapping guarantees for PAL/NTSC and `704`/`720` widths to avoid feature loss in active samples | Potential source-detail loss or accidental remapping regressions | Stage 2 |
| Spec validation | Good unit coverage for timing, quantisation, and sampled chroma behavior | No formal compliance harness for waveform tolerances, filter response, or reserved VBI behavior | Risk of silent drift from BT.1700 / SMPTE 170M / ST 244 / EBU 3280 intent | Stage 4 |

## Progressive-to-Active-Sample Mapping Contract

This contract is normative for implementation and test planning.

Definitions:

- $N_a$: active-line sample count in the selected standard's `4fsc` active window.
- $W$: active source width (`720` or `704`).
- $x_0$: source active left offset (`0` for `720`, `8` for `704` in a normalized `720` raster).
- $s$: active-line sample index in $[0, N_a-1]$.

Required mapping:

$$
p(s) = x_0 + \left\lfloor \frac{s \cdot W}{N_a} \right\rfloor
$$

Required active-line counts in current `4fsc` runtime:

- PAL: $N_a = 922$
- NTSC: $N_a = 745$

Required invariants:

- Every active sample maps to exactly one source active pixel.
- Endpoints map exactly: sample `0` to first active pixel, sample `N_a-1` to last active pixel.
- Every source active pixel receives one or more mapped active samples.
- Mapping must remain integer-domain and deterministic.
- `704` sources must be centered in `720` with `8` nominal-black pixels per side before active mapping.

## Staged Implementation Plan

### Stage 1: Runtime Surface Alignment

Goal: make the runtime model and the public project-file surface accurately represent the currently supported behavior, then add the missing schema needed for later work.

Primary changes:

- Extend the project model in `include/videosynth/model.h` to represent line injections and laserdisc-related preset fields explicitly instead of only tolerating the YAML keys.
- Update `src/yaml_project_parser.cpp` to parse the full implemented subset intentionally, rather than accepting unsupported keys without storing them.
- Split validator behavior in `src/project_validator.cpp` into:
  - current-runtime checks that must pass now
  - structured placeholders for line-injection and laserdisc checks that can be enabled when the data model is present
- Keep the HLD and this plan aligned with the reduced runtime scope until later stages land.

Tests:

- Add parser tests for line-injection object parsing and unsupported-field rejection.
- Add validator tests that prove unsupported runtime features fail explicitly instead of being silently ignored.

Exit criteria:

- No YAML key that affects waveform generation is silently ignored.
- The parser and validator expose the same supported surface that the runtime can actually execute.

### Stage 2: Digital Synthesis Core Hardening

Goal: move the signal core from mixed arithmetic toward a deterministic fixed-point implementation and introduce an explicit timing/phase abstraction suitable for future output modes.

Primary changes:

- Introduce a sampled synthesis context object that owns:
  - standard-specific sample rate
  - frame sample count
  - line sample schedule
  - phase-accumulator state or equivalent fixed-step carrier model
- Replace floating-point carrier-phase arrays in `src/generation_stage.cpp` with a fixed-point phase representation or an integer recurrence-based carrier synthesizer.
- Replace the floating-point FIR path in `src/chroma_encoder.cpp` with a fixed-point coefficient and accumulation path, while preserving the current bandwidth targets as the initial reference.
- Keep `SampleFixed` as the inter-stage exchange type, but define explicit fixed-point ranges and rounding rules for every arithmetic boundary.
- Lift progressive horizontal mapping into an explicit helper/API with a normative contract identical to the equation above so mapping behavior is no longer an implicit expression inside the per-line loop.

Tests:

- Add code-level equivalence tests between the current mixed path and the new fixed-point path during migration.
- Add deterministic golden-vector tests for burst phase, PAL V-axis switching, and NTSC burst-plus-180 active-chroma alignment.
- Add filter-response checks that verify the implemented low-pass kernels against the intended cutoff behavior.
- Add a mapping conformance matrix that validates PAL/NTSC with both `720` and `704` widths:
  - endpoint mapping
  - full source-pixel coverage
  - deterministic sample-span distribution per pixel
  - no out-of-window access for normalized `704` sources.

Exit criteria:

- The generation path no longer depends on floating-point carrier synthesis.
- Fixed-point and reference outputs remain within agreed code-level tolerances.
- The new timing/phase abstraction is sufficient to support later unlocked or non-`4fsc` work.
- Progressive-source horizontal mapping is documented in code and tests as a locked contract for PAL/NTSC `704`/`720` operation.

### Stage 3: Alternate Output Clocks and Encodings

Goal: extend the runtime beyond locked `4fsc` while preserving the correctness of the current baseline.

Primary changes:

- Introduce an explicit sample-clock abstraction that can support:
  - locked `4fsc`
  - unlocked `4fsc`
  - non-`4fsc` output rates such as `20MSPS` and `40MSPS`
- Decide whether non-`4fsc` outputs are produced by:
  - generating directly at the target sample rate, or
  - generating at `4fsc` and resampling with a formally defined conversion path
- Extend `src/output_stage.cpp` to support additional preset families without overloading the current `4fsc`-only assumptions.

Tests:

- Preset-acceptance tests for each supported encoding/state combination.
- Timing-accuracy tests for each clock mode.
- Regression tests that prove the current locked-`4fsc` outputs are unchanged.

Exit criteria:

- Alternate output modes are implemented as real code paths rather than declared-only presets.
- The `4fsc` baseline remains bit-stable unless an intentional format change is approved.

### Stage 4: Formal Compliance Harness

Goal: move from directionally correct behavior to measured conformance against the cited standards and internal design claims.

Implementation status (`2026-05-29`):

- Implemented a dedicated compliance-oriented unit harness in `tests/test_compliance_harness.cpp`.
- Added explicit tolerance checks for:
  - sync pulse widths under shaped-edge synthesis for PAL and NTSC
  - sync 10%-90% edge timing
  - burst gate placement and in-window burst energy
  - NTSC stable burst reference assumption in the current locked-`4fsc` model
  - PAL burst polarity alternation sequence
- Added code-space anchor and legal-range checks for PAL and NTSC quantization profiles.
- Added chroma filter-response attenuation checks in the compliance harness.
- Kept fixture-hash stability checks for frozen baseline fixture outputs in `tests/test_fixture_projects.cpp`.

Primary changes:

- Add a compliance test harness that measures generated samples against:
  - code-space anchors and legal ranges from ST 244 / EBU 3280
  - pulse widths and burst placement from BT.1700 / SMPTE 170M
  - chroma filter response and phase behavior from the design contract
- Define tolerances explicitly for:
  - sync edge timing
  - burst gate timing
  - burst phase
  - PAL line-sequence polarity
  - NTSC SC-H progression assumptions
- Add stable fixture outputs and hashed reference artifacts only where the format contract is intentionally frozen.

Tests:

- Compliance-oriented unit tests for numeric tolerances.
- Functional tests for fixture projects covering the supported output families.

Exit criteria:

- The implementation can be discussed in terms of measured conformance, not only qualitative alignment.
- Future refactors have a standard-oriented regression safety net.

## Recommended Review Order

1. Review the HLD alignment changes first so the current architecture and scope are stated correctly.
2. Review Stage 1 and Stage 2 together, because the parser/runtime surface and the sampled synthesis core define the foundation for all later work.
3. Review Stage 3 and Stage 4 last, because they depend on the earlier architecture and waveform contracts being stable.

## Short Recommendation

If only one follow-up tranche is funded immediately, prioritize **Stage 1 plus Stage 2**. That pair removes the largest architecture ambiguity, prevents YAML/runtime drift, and creates a cleaner base for deferred VBI feature delivery and future alternate-output modes.