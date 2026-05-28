# VideoSynth Progressive Source Implementation Plan

## 1. Objective

Implement frame-based progressive source support, aligned to the HLD, for the following source families:

- MOV
- MP4
- PNG
- RAW

The delivery sequence is phased, with exactly one source family introduced per phase. Each phase must also add and validate both NTSC and PAL fixture projects:

- Static source phases (PNG, RAW): each fixture section must render at least `8` frames.
- Video source phases (MP4, MOV): each fixture section must use `duration_frames: "all"` and process the complete source.

Range-preservation intent:

- MOV and RAW phases must preserve 10-bit studio-domain sub-black and over-white excursions when present in source material.
- PNG and MP4 phases are in-range-content paths and are not required to preserve sub-black/over-white excursions end-to-end.

## 2. HLD Anchors

This plan is derived from and constrained by:

- `docs/design/high-level-design.md` Section 2 (Core Requirements)
- `docs/design/high-level-design.md` Section 4 (Generation Stage)
- `docs/design/high-level-design.md` Section 7 (YAML Project File Specification)
- `docs/design/high-level-design.md` Section 8.1 (Frame-Based Sections)
- `docs/design/high-level-design.md` Section 9 (Field and Line Handling)
- `docs/design/high-level-design.md` Section 12 (Implementation Pipeline)
- `docs/design/high-level-design.md` Section 13 (Error Handling and Validation)

Normative behavior that must remain fixed across all phases:

- Progressive input dimensions:
  - PAL: `720x576` or `704x576`
  - NTSC: `720x480` or `704x480`
- Scaling/resampling of progressive sources is not supported; source dimensions must already match accepted PAL/NTSC dimensions.
- `704`-wide inputs normalized into internal `720` width with `8` nominal-black pixels per side.
- Source color conversion to `10-bit 4:4:4 YCbCr BT.601 studio swing` before generation-stage use.
- Progressive-to-field mapping preserves standard-specific active-line geometry and field ordering.
- Progressive-source validation is profile-based (container + codec + chroma format + bit depth), not extension-only.

Supported profiles to implement:

- MOV: ProRes 422 family (`yuv422p10le`) and ProRes 4444 (`yuv444p10le`) in MOV, with source frame rate matching selected output standard (`25 fps` PAL, `30000/1001 fps` NTSC).
- MP4: H.264/AVC (`yuv420p`) in MP4, with source frame rate matching selected output standard (`25 fps` PAL, `30000/1001 fps` NTSC).
- PNG: Single-frame PNG truecolour (RGB/RGBA, 8-bit or 16-bit integer channels).
- RAW: Headerless raw still frame with explicit external format declaration using:
  - `yuv422p10le`: packed `Y0 Cb Y1 Cr` component order; each component stored in a 16-bit little-endian word carrying a 10-bit code value.

## 3. Current Gap and Implementation Strategy

Current implementation behavior is still software-generated-pattern only (`section.type == "software_generated"`) and does not yet satisfy HLD progressive-section behavior.

Strategy:

1. Introduce a generalized frame-source abstraction that can supply either generated patterns or decoded progressive frames.
2. Extend project model/parser/validator to support `type: progressive` with `source` and `duration_frames` (`integer` or `"all"`).
3. Add deterministic decoder adapters and conversion pipeline for each source family in phases.
4. Add PAL/NTSC fixture projects and tests per phase.
5. Keep unit tests deterministic by mocking decode/conversion seams; use fixture-level integration tests for file decoding.

## 4. Phase 0: Shared Foundation

Phase 0 is a required delivery phase that establishes shared architecture used by all source-family phases.

### Goals

- Establish parser, model, validation, and frame-source integration support for `type: progressive`.
- Ensure subsequent source-family phases can focus only on format-specific decoding behavior.

### Work Items

#### 4.1 Model and Parsing

- Extend `Section` in `include/videosynth/model.h` to represent progressive sections without breaking software-generated compatibility.
- Update `src/yaml_project_parser.cpp` to parse progressive fields:
  - `type: progressive`
  - `source`
  - `source_pixel_format` (required for RAW)
  - `duration_frames` as positive integer or `"all"`
- Preserve strict error messages for malformed YAML and unsupported combinations.

#### 4.2 Validation

- Update `src/project_validator.cpp` to allow `software_generated` and `progressive`.
- Enforce HLD constraints for progressive dimensions, source readability, and duration semantics.
- Enforce profile-based validation (container + codec + chroma format + bit depth) for progressive sources.
- Reject any progressive source that would require scaling/resizing.
- Validate standard compatibility of source raster and frame rate.

#### 4.3 Frame Source Integration

- Add a progressive frame-source path beside test-pattern generation in `frame_source` domain.
- Keep `GenerationStage` consuming normalized frame-source buffers, independent of source origin.
- Ensure out-of-aperture pixels remain nominal black.

#### 4.4 Test Infrastructure

- Add decoder/conversion interfaces in `include/videosynth/interfaces.h` to support mock-based unit tests.
- Add reusable fixture helpers for source asset resolution under `tests/projects`.
- Update `run-projects.sh` compatibility if additional fixture files are introduced.

### Exit Criteria

- `type: progressive` sections parse successfully when structurally valid.
- Validator supports progressive constraints and rejects invalid progressive inputs with clear errors.
- Generation pipeline accepts normalized frame-source buffers from non-pattern origins.
- Test scaffolding is in place for format-specific phases.

## 5. Source-Family Phases

Phase ordering is strict:

1. Phase 0 (Shared Foundation)
2. Phase 1 (PNG)
3. Phase 2 (RAW)
4. Phase 3 (MP4)
5. Phase 4 (MOV)

## Phase 1: PNG Progressive Sources (Static Image)

### Goals

- Support `type: progressive` for PNG image inputs.
- Validate RGB-to-BT.601 conversion and raster normalization path.
- Support only in-range-content behavior; no requirement to preserve sub-black/over-white excursions from PNG source content.

### Work Items

- Implement PNG decode adapter in progressive ingest path.
- Restrict support to single-frame truecolour PNG (RGB/RGBA, 8-bit or 16-bit integer channels).
- Support static-frame repeat semantics:
  - single decoded image reused for output frame count.
- Add fixture assets:
  - PAL PNG: one `720x576` or `704x576` image.
  - NTSC PNG: one `720x480` or `704x480` image.
- Add fixture projects:
  - `tests/projects/pal_progressive_png.yaml`
  - `tests/projects/ntsc_progressive_png.yaml`
- Set progressive section `duration_frames: 8` (or greater).

### Verification

- Unit tests:
  - parser/validator acceptance for PNG progressive sections.
  - rejection of unsupported PNG variants outside the profile.
  - color conversion and nominal-black aperture protection.
- Integration tests:
  - full pipeline generation for both PAL and NTSC PNG fixtures.
  - metadata frame count equals configured static duration.

### Exit Criteria

- PNG progressive fixtures pass for both standards.
- Output is deterministic across repeated runs.

## Phase 2: RAW Progressive Sources (Static Frame)

### Goals

- Add RAW still-frame ingest with explicit format declaration in metadata or sidecar config.
- Preserve 10-bit studio-domain sub-black and over-white excursions when present in source data.

### Work Items

- Implement the supported RAW input profiles:
  - `yuv422p10le` (packed `Y0 Cb Y1 Cr`, 16-bit little-endian words with 10-bit code values)
- Implement RAW reader and mapping to normalized frame-source buffer.
- Add fixture assets:
  - PAL RAW sample.
  - NTSC RAW sample.
- Add fixture projects:
  - `tests/projects/pal_progressive_raw.yaml`
  - `tests/projects/ntsc_progressive_raw.yaml`
- Set progressive section `duration_frames: 8` (or greater).

### Verification

- Unit tests:
  - RAW parse/decode boundary checks.
  - sub-black/over-white preservation through normalization pipeline.
  - invalid size and stride rejection.
- Integration tests:
  - full pipeline generation from RAW fixtures for PAL and NTSC.
  - stable output-hash regression baselines.

### Exit Criteria

- RAW static-frame path is stable and validated in both standards.
- Invalid RAW descriptors fail with explicit diagnostics.

## Phase 3: MP4 Progressive Sources (Video)

### Goals

- Add MP4 video ingest and frame-sequence decode.
- Support only H.264/AVC `yuv420p` profile in MP4 for this phase.

### Work Items

- Implement MP4 demux/decode via the selected decoder backend.
- Reject non-H.264 MP4 streams and unsupported pixel formats/bit depths.
- Enforce standard frame-rate compatibility and source-dimension validation.
- Apply container crop metadata before 704/720 normalization.
- Keep no-scaling policy strict after crop/display-aperture resolution.
- Add short deterministic fixture videos:
  - PAL MP4 clip (25 fps).
  - NTSC MP4 clip (~29.97 fps).
- Add fixture projects:
  - `tests/projects/pal_progressive_mp4.yaml`
  - `tests/projects/ntsc_progressive_mp4.yaml`
- Use `duration_frames: "all"` in both fixtures.

### Verification

- Unit tests:
  - frame iteration behavior and EOF handling.
  - unsupported MP4 codec/pixel-format rejection.
  - frame-rate mismatch rejection.
- Integration tests:
  - both fixtures decode full clip and generate all frames.
  - metadata frame count equals decoded frame count.

### Exit Criteria

- MP4 video path processes complete source clips for PAL and NTSC.
- All-frame decode is deterministic for fixture media.

## Phase 4: MOV Progressive Sources (Video)

### Goals

- Add MOV container support with MOV-specific profile constraints aligned to the HLD.
- Support ProRes 422 and ProRes 4444 in MOV, with 10-bit studio-range preservation including sub-black/over-white excursions.

### Work Items

- Implement MOV demux/decode and metadata handling.
- Reject unsupported MOV video codecs and unsupported pixel formats/bit depths.
- Confirm MOV-specific color metadata and crop metadata mapping into normalization pipeline.
- Add deterministic fixture videos:
  - PAL MOV clip (25 fps).
  - NTSC MOV clip (~29.97 fps).
- Add fixture projects:
  - `tests/projects/pal_progressive_mov.yaml`
  - `tests/projects/ntsc_progressive_mov.yaml`
- Use `duration_frames: "all"` in both fixtures.

### Verification

- Unit tests:
  - MOV-specific metadata handling and fallback behavior.
  - sub-black/over-white preservation verification for ProRes source fixtures.
- Integration tests:
  - full decode and generation for PAL/NTSC MOV fixtures.
  - generated frame count equals full source length.

### Exit Criteria

- MOV full-source processing is validated for both standards.
- MOV and MP4 behavior is consistent for equivalent content.

## 6. Fixture Project Matrix (Mandatory)

By completion, include at least these progressive fixture projects:

- `tests/projects/pal_progressive_png.yaml` (`duration_frames >= 8`)
- `tests/projects/ntsc_progressive_png.yaml` (`duration_frames >= 8`)
- `tests/projects/pal_progressive_raw.yaml` (`duration_frames >= 8`)
- `tests/projects/ntsc_progressive_raw.yaml` (`duration_frames >= 8`)
- `tests/projects/pal_progressive_mp4.yaml` (`duration_frames: "all"`)
- `tests/projects/ntsc_progressive_mp4.yaml` (`duration_frames: "all"`)
- `tests/projects/pal_progressive_mov.yaml` (`duration_frames: "all"`)
- `tests/projects/ntsc_progressive_mov.yaml` (`duration_frames: "all"`)

All listed fixtures must be runnable through `run-projects.sh` and included in fixture test coverage.

## 7. Test Plan by Layer

### Unit Tests (Deterministic, Mock-Based)

- Parser and model mapping for progressive sections.
- Validator rules for progressive dimensions, frame rate, duration, and file resolution.
- Progressive-to-field row mapping:
  - field 1 uses source rows `2n`
  - field 2 uses source rows `2n+1`
- 704-to-720 normalization and aperture masking.
- Color conversion into 10-bit 4:4:4 BT.601 studio-swing domain.

### Integration Tests (Fixture Media)

- End-to-end generation for each phase fixture pair (PAL + NTSC).
- Metadata frame-count assertions:
  - static sources: configured fixed frame count (`>=8`)
  - video sources: full decoded source count (`"all"`)
- Output payload hash stability per fixture.

## 8. Risks and Mitigations

- Decoder nondeterminism across library versions:
  - pin decoder dependencies in Nix and use deterministic fixture codecs.
- Profile drift from broad container assumptions:
  - enforce validator checks against the explicit supported profile list before generation starts.
- Metadata variability in MOV/MP4:
  - define explicit fallback policy for missing primaries/transfer/matrix metadata.
- Test runtime growth:
  - keep fixture video clips short while still validating `"all"` semantics.
- Progressive/HLD mismatch drift during implementation:
  - update HLD and this plan together when behavior changes are intentional.

## 9. Completion Criteria

The progressive-source implementation is complete when:

1. All four source families (PNG, RAW, MP4, MOV) are supported under `type: progressive`.
2. All eight PAL/NTSC progressive fixture projects listed in Section 6 pass.
3. Static-source fixtures render at least `8` frames each.
4. Video-source fixtures use and process the complete source (`duration_frames: "all"`).
5. Parser, validator, generation, and output tests pass in the Nix environment.
6. Documentation remains aligned with implemented behavior in `docs/design/high-level-design.md`.
