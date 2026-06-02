# PAL and NTSC VITS Implementation Plan

## 1. Purpose

This document defines a phased implementation plan for Vertical Interval Test Signals (VITS) in VideoSynth, aligned to section 8.2 (Line Injections) of the high-level design.

The plan covers:
- PAL and NTSC VITS generation behavior.
- All timing and signal data needed to synthesize each supported VITS waveform.
- Unit testing required to verify waveform compliance against documented requirements.

## 2. Scope

In scope:
- VITS injection generation for all vits_type values listed in section 8.2.
- Parser + validator constraints for VITS entries in line_injections.
- Deterministic unit tests that verify waveform structure, timing, level, frequency, phase, and placement behavior.

Out of scope:
- Laserdisc and VITC implementation details beyond conflict validation rules already defined in the HLD.
- Functional/end-to-end media output compliance tests (these can be added later as functional tests).

## 3. Normative Inputs

Primary design contract:
- docs/design/high-level-design.md section 8.2.

Authoritative waveform definitions:
- docs/analogue-video-specifications/docs/video_metadata/VITS/PAL-VITS.md
- docs/analogue-video-specifications/docs/video_metadata/VITS/NTSC-VITS.md
- docs/analogue-video-specifications/resources/definitions/vits/pal/*.yaml
- docs/analogue-video-specifications/resources/definitions/vits/ntsc/*.yaml

Format-level constraints:
- The rendered VITS signals must also satisfy the broader PAL and NTSC video format specifications.
- Where the VITS-specific documents do not repeat a requirement, the applicable PAL or NTSC format standard remains authoritative.

## 4. Cross-Cutting Implementation Contracts

### 4.1 Supported VITS types

PAL:
- vits17
- itu-multiburst
- uk-national
- vits20
- itu-composite
- itu-combination

NTSC:
- ntc7-composite
- ntc7-combination
- fcc-multiburst
- fcc-composite
- virs

### 4.2 Placement and frame-line expectations

VideoSynth line addressing is frame-sequential:
- PAL frame lines: 1-625
- NTSC frame lines: 1-525

When source standards describe a signal as "field 1" or "field 2", this plan treats that as descriptive provenance only. Runtime targeting and validation must use frame-line numbers only.

PAL recommended placements:
- vits17: frame line 17
- itu-multiburst: frame line 18
- uk-national: frame line 19
- vits20: frame line 20
- itu-composite: frame line 330
- itu-combination: frame line 331

NTSC recommended placements:
- ntc7-composite: frame line 17
- ntc7-combination: frame line 280
- fcc-multiburst: frame line 18
- fcc-composite: frame line 281
- virs: assignment dependent per project usage

Validation rules:
- Reject PAL vits_type in NTSC projects and vice versa.
- Enforce target_lines presence for VITS items.
- Enforce line-vs-type compatibility policy (strict or warning mode, choose and document in validator).
- Enforce existing line-allocation conflicts from the HLD (including laserdisc reservations and subtitle interactions where applicable).

### 4.3 Units and conversion

PAL definitions are in mV.
NTSC definitions are in IRE.

Generation-domain contract:
- Convert NTSC IRE to mV before compositing, using:
  - 100 IRE = 714.3 mV
  - 0 IRE = blanking
- Keep all synthesis in the fixed-point mV domain used by generation stage.
- VITS generation follows the pipeline's Y+C generation model: the generation stage synthesizes luma and chroma components separately, and composite summing occurs in the output stage.

### 4.4 Timing reference and shaping

- Timing reference for all VITS templates: sync_edge.
- Time windows are in microseconds relative to sync edge.
- Preserve per-template edge shaping values:
  - y_rise_time_us (typically 0.20)
  - c_rise_time_us (typically 0.40)
  - Primitive-level rise_time_us overrides where specified.

### 4.5 Chroma frequency and phase lock

- PAL color subcarrier: 4.43361875 MHz.
- NTSC color subcarrier: 3.579545 MHz.
- For primitives with subcarrier_lock_multiple, frequency must be:
  - subcarrier_lock_multiple x color_subcarrier_hz
- Respect primitive phase_deg and sequence progression rules already used by field structure/chroma encoding paths.

### 4.6 Primitive composition semantics

Required primitive types:
- colour_bar
- burst
- sin_squared_pulse
- composite_pulse
- staircase

Required composition semantics:
- combine: replace or add
- composites mode: serial or parallel
- continuity_group and transition_out crossfade/join behavior
- baseline_anchor behavior for staircase/burst groups
- Construction joins for staircase and VIRS-style sequences are synthesized automatically in the planner so adjacent elements share a continuous handoff region without baking extra gap or overlap time into the catalog timing windows.

### 4.7 Signal Legality, Bandwidth, and Ramping

The rendered shape of each VITS waveform must remain legal for the active PAL or NTSC standard.

Requirements:
- Use the waveform geometry, edge timing, and amplitude structure defined by the applicable VITS spec document.
- Apply the PAL or NTSC video format specification requirements that govern the signal even when those requirements are not restated in the VITS-specific document.
- Do not introduce transitions, overshoot, ringing, or smoothing artifacts that push the result beyond the standard's intended maximum bandwidth or rise-time behavior.
- Preserve the documented ramping/edge-shaping model for each primitive so the rendered result stays within the analogue compliance envelope for PAL or NTSC.
- Treat rise_time_us, burst envelope shaping, and staircase transitions as part of signal legality, not just cosmetic rendering details.
- If a VITS definition or implementation path cannot satisfy the applicable standard limits, reject it during validation rather than emitting a non-compliant waveform.

## 5. Per-Signal Technical Data Requirements

This section is the minimum required synthesis data for each vits_type.

### 5.1 PAL VITS data

#### vits17 (frame line 17)

Global:
- levels_unit: mV
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- white_reference: colour_bar, y, replace, 700.0 mV, 12.0-22.0 us, rise 0.200
- pulse_2t: sin_squared_pulse, y, replace, amp 700.0 mV, 25.8-26.2 us, rise 0.200
- modulated_y: sin_squared_pulse, y, replace, amp 350.0 mV, 30.0-34.0 us, rise 0.200
- modulated_c: composite_pulse, c, add, dc 0.0 mV, component 350.0 mV, 30.0-34.0 us, rise 0.400, lock 1.0, phase 90.0 deg
- staircase steps (all colour_bar, y, replace, rise 0.235):
  - 140.0 mV, 40.0-44.0 us
  - 280.0 mV, 44.0-48.0 us
  - 420.0 mV, 48.0-52.0 us
  - 560.0 mV, 52.0-56.0 us
  - 700.0 mV, 56.0-62.0 us

Composite tree:
- modulated_pulse = parallel(modulated_y, modulated_c)
- render order: white_reference, pulse_2t, modulated_pulse, staircase_step_1..5

#### itu-multiburst (frame line 18)

Global:
- levels_unit: mV
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- grey_pedestal: colour_bar, y, replace, 350.0 mV, 12.0-62.0 us, rise 0.350
- positive_reference_boost: colour_bar, y, add, +210.0 mV, 12.0-16.0 us, rise 0.350, transition_out linear 0.350
- negative_reference_boost: colour_bar, y, add, -210.0 mV, 16.0-20.0 us, rise 0.350
- burst train (all burst, c, add, amp 210.0 mV, rise 0.200):
  - 0.500 MHz, 24.0-28.0 us, phase 0.0
  - 1.000 MHz, 30.0-35.0 us, phase 0.0
  - 2.000 MHz, 36.0-41.0 us, phase 0.0
  - 4.000 MHz, 42.0-47.0 us, phase 0.0
  - 4.800 MHz, 48.0-53.0 us, phase 144.0
  - 5.800 MHz, 54.0-60.0 us, phase -144.0

Composite tree:
- reference_bar_pair = serial(positive_reference_boost, negative_reference_boost), continuity_group reference_bars, baseline grey_pedestal
- burst_train = serial(burst_0_5, burst_1_0, burst_2_0, burst_4_0, burst_4_8, burst_5_8)
- render order: grey_pedestal, reference_bar_pair, burst_train

#### uk-national (frame line 19)

Global:
- levels_unit: mV
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- white_reference: colour_bar, y, replace, 700.0 mV, 12.0-22.0 us, rise 0.200
- pulse_2t: sin_squared_pulse, y, replace, amp 700.0 mV, 25.8-26.2 us, rise 0.200
- modulated_y: sin_squared_pulse, y, replace, amp 350.0 mV, 29.0-31.0 us, rise 0.200
- modulated_c: composite_pulse, c, add, dc 0.0 mV, component 350.0 mV, 29.0-31.0 us, rise 0.400, lock 1.0, phase 90.0 deg
- chroma_reference: burst, c, add, dc 0.0 mV, component 70.0 mV, 34.0-60.0 us, rise 1.000, lock 1.0, phase 60.660 deg
- staircase: staircase, y, replace, top 700.0 mV, 40.0-60.0 us, steps 5, rise 0.235
- black_reference: colour_bar, y, replace, 700.0 mV, 60.0-62.0 us, rise 0.235

Composite tree:
- modulated_pulse = parallel(modulated_y, modulated_c)
- render order: white_reference, pulse_2t, modulated_pulse, chroma_reference, staircase, black_reference

#### vits20 (frame line 20)

Global:
- levels_unit: mV
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- grey_pedestal: colour_bar, y, replace, 350.0 mV, 12.0-32.0 us, rise 0.235
- chroma_reference_full: burst, c, add, dc 0.0 mV, component 350.0 mV, 14.0-28.0 us, rise 1.000, lock 1.0, phase 60.660 deg
- chroma_reference_low: burst, c, add, dc 0.0 mV, component 150.0 mV, 34.0-62.0 us, rise 1.000, lock 1.0, phase 60.660 deg

Render order:
- grey_pedestal, chroma_reference_full, chroma_reference_low

#### itu-composite (frame line 330)

Global:
- levels_unit: mV
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- white_reference: colour_bar, y, replace, 700.0 mV, 12.0-22.0 us, rise 0.200
- pulse_2t: sin_squared_pulse, y, replace, amp 700.0 mV, 25.8-26.2 us, rise 0.200
- chroma_reference: burst, c, add, dc 0.0 mV, component 140.0 mV, 30.0-60.0 us, rise 1.000, lock 1.0, phase 60.660 deg
- staircase steps (all colour_bar, y, replace, rise 0.235):
  - 140.0 mV, 40.0-44.0 us
  - 280.0 mV, 44.0-48.0 us
  - 420.0 mV, 48.0-52.0 us
  - 560.0 mV, 52.0-56.0 us
  - 700.0 mV, 56.0-62.0 us

Render order:
- white_reference, pulse_2t, chroma_reference, staircase_step_1..5

#### itu-combination (frame line 331)

Global:
- levels_unit: mV
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- grey_pedestal: colour_bar, y, replace, 350.0 mV, 12.0-62.0 us, rise 0.235
- chroma_step_1: burst, c, add, dc 0.0 mV, component 70.0 mV, 14.0-18.0 us, rise 1.000, lock 1.0, phase 60.660 deg, transition_out crossfade 1.000
- chroma_step_2: burst, c, add, dc 0.0 mV, component 210.0 mV, 18.0-22.0 us, rise 1.000, lock 1.0, phase 60.660 deg, transition_out crossfade 1.000
- chroma_step_3: burst, c, add, dc 0.0 mV, component 350.0 mV, 22.0-28.0 us, rise 1.000, lock 1.0, phase 60.660 deg
- sustained_reference: burst, c, add, dc 0.0 mV, component 210.0 mV, 34.0-60.0 us, rise 1.000, lock 1.0, phase 60.660 deg

Composite tree:
- chroma_staircase_group = serial(chroma_step_1, chroma_step_2, chroma_step_3), continuity_group chroma_staircase, baseline grey_pedestal
- render order: grey_pedestal, chroma_staircase_group, sustained_reference

### 5.2 NTSC VITS data

#### ntc7-composite (frame line 17)

Global:
- levels_unit: IRE
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- white_reference: colour_bar, y, replace, 100.0 IRE, 12.000-30.000 us, rise 0.250
- pulse_2t: sin_squared_pulse, y, replace, amp 100.0 IRE, 33.750-34.250 us, rise 0.200
- modulated_y: sin_squared_pulse, y, replace, amp 50.0 IRE, 35.400-38.600 us, rise 0.200
- modulated_c: composite_pulse, c, add, dc 0.0 IRE, component 50.0 IRE, 35.400-38.600 us, rise 0.400, lock 1.0, phase 0.0 deg
- chrominance_reference: burst, c, add, dc 0.0 IRE, component 20.0 IRE, 42.000-60.000 us, rise 0.250, lock 1.0, phase 180.0 deg
- staircase: staircase, y, replace, top 90.0 IRE, 46.000-60.000 us, steps 5, rise 0.250
- staircase_terminus: colour_bar, y, replace, 90.0 IRE, 60.000-62.000 us, rise 0.250

Composite tree:
- modulated_pulse = parallel(modulated_y, modulated_c)
- render order: white_reference, pulse_2t, modulated_pulse, chrominance_reference, staircase, staircase_terminus

#### ntc7-combination (frame line 280)

Global:
- levels_unit: IRE
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- grey_background: colour_bar, y, replace, 50.0 IRE, 12.000-62.000 us, rise 0.250
- grey_reference_boost: colour_bar, y, add, +50.0 IRE, 12.000-16.000 us, rise 0.250
- multiburst packets (all burst, c, add, amp 25.0 IRE, rise 0.200):
  - 0.500 MHz, 18.000-23.000 us, phase 0.0
  - 1.000 MHz, 24.000-27.000 us, phase 0.0
  - 2.000 MHz, 28.000-31.000 us, phase 0.0
  - 3.000 MHz, 32.000-35.000 us, phase 0.0
  - subcarrier lock, 36.000-39.000 us, phase 0.0
  - 4.200 MHz, 40.000-43.000 us, phase 0.0
- chroma staircase zones:
  - zone1: burst, c, add, dc 0.0 IRE, component 10.0 IRE, 46.000-50.000 us, rise 1.000, lock 1.0, phase 90.0 deg, transition_out crossfade 1.000
  - zone2: burst, c, add, dc 0.0 IRE, component 20.0 IRE, 50.000-54.000 us, rise 1.000, lock 1.0, phase 90.0 deg, transition_out crossfade 1.000
  - zone3: burst, c, add, dc 0.0 IRE, component 40.0 IRE, 54.000-60.000 us, rise 1.000, lock 1.0, phase 90.0 deg

Composite tree:
- multiburst_sweep = serial(mb_0_5mhz..mb_4_2mhz)
- chroma_staircase = serial(chroma_zone_1, chroma_zone_2, chroma_zone_3), continuity_group chroma_stair, baseline grey_background
- render order: grey_background, grey_reference_boost, multiburst_sweep, chroma_staircase

#### fcc-multiburst (frame line 18)

Global:
- levels_unit: IRE
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- grey_pedestal: colour_bar, y, replace, 40.0 IRE, 9.200-62.000 us, rise 0.250
- white_reference_boost: colour_bar, y, add, +60.0 IRE, 9.200-15.700 us, rise 0.250
- burst train (all burst, c, add, amp 30.0 IRE, rise 0.200):
  - 0.500 MHz, 18.200-26.700 us
  - 1.250 MHz, 28.200-34.200 us
  - 2.000 MHz, 35.200-40.200 us
  - 3.000 MHz, 41.200-46.200 us
  - 3.580 MHz, 47.200-52.200 us, lock 1.0
  - 4.100 MHz, 53.200-58.200 us

Composite tree:
- burst_train = serial(burst_0_5, burst_1_25, burst_2_0, burst_3_0, burst_3_58, burst_4_1)
- render order: grey_pedestal, white_reference_boost, burst_train

#### fcc-composite (frame line 281)

Global:
- levels_unit: IRE
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- chroma_reference_zone: burst, c, add, dc 0.0 IRE, component 20.0 IRE, 9.500-28.000 us, rise 0.400, lock 1.0, phase 180.0 deg
- staircase: staircase, y, replace, top 80.0 IRE, 13.000-28.000 us, steps 5, rise 0.250
- staircase_terminus: colour_bar, y, replace, 80.0 IRE, 28.000-30.000 us, rise 0.250
- pulse_2t: sin_squared_pulse, y, replace, amp 100.0 IRE, 35.250-35.750 us, rise 0.200
- modulated_y: sin_squared_pulse, y, replace, amp 50.0 IRE, 37.900-41.100 us, rise 0.200
- modulated_c: composite_pulse, c, add, dc 0.0 IRE, component 50.0 IRE, 37.900-41.100 us, rise 0.400, lock 1.0, phase 180.0 deg
- white_reference: colour_bar, y, replace, 100.0 IRE, 43.900-62.000 us, rise 0.200

Composite tree:
- modulated_pulse = parallel(modulated_y, modulated_c)
- render order: chroma_reference_zone, staircase, staircase_terminus, pulse_2t, modulated_pulse, white_reference

#### virs (assignment-dependent line)

Global:
- levels_unit: IRE
- timing_reference: sync_edge
- y_rise_time_us: 0.20
- c_rise_time_us: 0.40

Primitives:
- virs_first_zone: colour_bar, y, replace, 68.0 IRE, 9.150-35.500 us, rise 0.250
- virs_chroma_ref: burst, c, add, dc 0.0 IRE, component 22.0 IRE, 10.100-34.500 us, rise 1.000, lock 1.0, phase 180.0 deg
- virs_second_zone: colour_bar, y, replace, 46.0 IRE, 35.500-48.700 us, rise 0.250
- virs_post_blank: colour_bar, y, replace, 0.0 IRE, 48.700-62.000 us, rise 0.250

Composite tree:
- virs_zone_1 = parallel(virs_first_zone, virs_chroma_ref)
- render order: virs_zone_1, virs_second_zone, virs_post_blank

## 6. Phased Implementation Plan

### Phase A: Data and Interface Foundation

Goals:
- Introduce a VITS definition model independent from YAML parser internals.
- Create interfaces for definition lookup and waveform synthesis.

Deliverables:
- include/videosynth/vits_definition.h
- include/videosynth/vits_definition_provider.h
- include/videosynth/vits_generator.h
- src/vits_definition_provider.cpp
- src/vits_generator.cpp

Design notes:
- Store canonical values in source as constexpr structures generated from checked-in YAML docs data, or load from internal embedded resources.
- Do not depend on filesystem access in unit tests.

Exit criteria:
- VITS definition catalog can be queried by standard + vits_type.
- Unknown vits_type returns deterministic validation error.

### Phase B: Validator and Parser Integration

Goals:
- Wire section 8.2 VITS rules through parsing and validation.

Deliverables:
- Extend parser mapping if needed for vits_type and target_lines semantics.
- Extend project validator to enforce:
  - standard-vs-vits_type compatibility
  - target_lines required and non-empty
  - line conflicts with existing reserved ranges from HLD

Exit criteria:
- Invalid combinations fail before generation with clear diagnostics.

### Phase C: Primitive Waveform Engine

Goals:
- Implement primitive renderers and composition semantics in fixed-point mV domain.
- Keep VITS synthesis in the Y+C generation model so composite summing remains an output-stage responsibility.

Deliverables:
- Primitive synthesis methods for colour_bar, burst, sin_squared_pulse, composite_pulse, staircase.
- Composition executor for replace/add and serial/parallel composites.
- Frequency lock and phase handling implementation for subcarrier_lock_multiple.

Exit criteria:
- Primitive-level unit tests pass for timing windows, amplitudes, and phase/frequency settings.

### Phase D: Section Injection Orchestration

Goals:
- Apply generated VITS lines into frame-sequential output buffers in generation stage.

Deliverables:
- Line injection applicator integration with generation stage.
- Frame-line mapping and validation for PAL and NTSC.
- Coexistence rules with other line injections as validated.

Exit criteria:
- Injection occurs only on requested lines and only in section span.

### Phase E: Full VITS Type Enablement

Goals:
- Enable all 11 VITS types using the data in section 5.

Deliverables:
- Complete vits_type dispatch table.
- Runtime generation for each PAL/NTSC waveform.

Exit criteria:
- Golden unit tests pass for all supported VITS types.

### Phase F: Unit Test Completion and Classification

Goals:
- Add deterministic unit tests that guarantee waveform compliance.

Deliverables:
- tests/test_vits_definition_provider.cpp
- tests/test_vits_generator_primitives.cpp
- tests/test_vits_generator_pal.cpp
- tests/test_vits_generator_ntsc.cpp
- tests/test_line_injection_vits_integration.cpp
- CMakeLists.txt updates classifying these as unit tests.

Exit criteria:
- New tests are in unit lane only.
- No filesystem/network/clock dependencies in unit tests.

### Phase G: Documentation Alignment

Goals:
- Keep HLD and implementation references aligned.

Implementation status:
- Completed on 2026-06-02.

Deliverables:
- Update docs/design/high-level-design.md implementation status notes once VITS is implemented.
- Keep this plan and section 8.2 in sync if VITS definitions change.

Completed alignment updates:
- HLD Section 8 current-status text now explicitly scopes "not yet implemented" to non-VITS line-injection runtime paths.
- HLD Section 12 generation-stage implementation note now states that VITS line injections are applied at runtime and that only non-VITS line-injection paths remain deferred.

Exit criteria:
- No known mismatch between behavior and design docs.

## 7. Unit Testing Requirements and Coverage Matrix

### 7.1 Test architecture requirements

All VITS unit tests must:
- Be deterministic and isolated.
- Use dependency inversion and mocks for collaborators.
- Avoid filesystem, network, system clock, and external tool access.
- Assert behavior-focused outcomes.

### 7.2 Required unit test groups

Definition provider tests:
- Returns expected catalog entry for each vits_type.
- Rejects unsupported vits_type.
- Returns exact primitive parameters (window, level/amplitude, frequency, phase, rise, combine, composition tree).

Primitive renderer tests:
- colour_bar: level and window boundaries.
- burst: frequency/phase/lock behavior and amplitude.
- sin_squared_pulse/composite_pulse: center, width, and amplitude profile.
- staircase: step count, widths, and level progression.
- add vs replace behavior with baseline signal.

Per-signal conformance tests (one suite per vits_type):
- Verify all primitive windows (start/end) match section 5.
- Verify all levels/amplitudes match section 5 (with fixed tolerance policy).
- Verify burst frequencies and phase_deg values.
- Verify lock behavior when subcarrier_lock_multiple is present.
- Verify ordering/composition tree semantics.

Line placement and conflict tests:
- Valid line placements accepted for PAL and NTSC.
- Standard mismatch is rejected.
- Reserved/conflicting line combinations rejected according to HLD section 8.2 and VBI allocation rules.

Sampling-lattice consistency tests:
- Window-to-sample index mapping is deterministic at PAL/NTSC 4fsc rates.
- Transition shaping is deterministic and bounded.
- Rendered waveform shape stays within the legal PAL/NTSC compliance envelope for bandwidth, ramping, and edge behavior.

Regression tests:
- Golden vectors for each vits_type at representative lines.
- Golden vectors compare waveform slices by sample index and amplitude tolerance.

Test project fixtures:
- Create dedicated YAML test projects under tests/projects for PAL and NTSC VITS coverage.
- Provide one positive fixture per supported VITS pattern so every available PAL and NTSC waveform is exercised end to end.
- Include at least one negative fixture per standard to prove rejection of unsupported vits_type values and invalid placements.
- Keep fixture projects deterministic and lightweight so they remain appropriate for the unit-test lane.

Recommended fixture set:
- PAL: one project each for vits17, itu-multiburst, uk-national, vits20, itu-composite, and itu-combination.
- NTSC: one project each for ntc7-composite, ntc7-combination, fcc-multiburst, fcc-composite, and virs.

### 7.3 Minimum per-vits_type assertion checklist

For each of the 11 VITS types, assert:
- Correct frame-line eligibility and standard-appropriate placement intent.
- Correct primitive count.
- Correct primitive type sequence.
- Correct start/end times for every primitive.
- Correct level/amplitude for every primitive.
- Correct rise_time_us per primitive.
- Correct frequency source (explicit freq_mhz vs subcarrier lock).
- Correct phase_deg.
- Correct combine mode and composite tree behavior.

### 7.4 Suggested tolerance policy

Use explicit tolerances for sampled waveform assertions:
- Time boundary tolerance: less than or equal to 1 output sample at current sample rate.
- Level tolerance: fixed-point quantization tolerance based on internal mV resolution.
- Frequency/phase checks: compare configured parameters exactly; compare rendered waveform with bounded error over a known window.

## 8. Delivery Sequence

Recommended order:
1. Implement definition provider + validator rules.
2. Implement primitive renderers and composition engine.
3. Create dedicated PAL and NTSC VITS fixture projects covering every supported pattern.
4. Enable PAL types first (6), then NTSC types (5).
5. Complete per-type conformance unit suites.
6. Complete conflict/placement tests.
7. Update HLD implementation status text.

## 9. Completion Definition

VITS implementation is complete when:
- All section 8.2 VITS types are generation-capable.
- Every signal in section 5 has unit tests proving timing and signal conformance.
- Validator enforces standard/type/line constraints and conflicts.
- New tests are classified as unit tests and pass in the unit test lane.
- Design documentation is aligned with runtime behavior.
