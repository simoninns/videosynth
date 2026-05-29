# Progressive RAW to OpenEXR Migration Plan

## Purpose

Define a staged, low-risk migration from progressive `*.raw` fixtures (`yuv422p10le`) to RGB-based OpenEXR while preserving studio-range behavior (including headroom/footroom) and proving near-parity output.

This plan targets four outcomes:

1. Convert all PAL and NTSC progressive RAW still resources to RGB-based OpenEXR with controlled, documented YCbCr->RGB mapping.
2. Replace progressive RAW loading in runtime paths with OpenEXR loading.
3. Prove that EXR-driven output is extremely close to the existing RAW-driven output using strict numeric tolerances.
4. Decommission RAW fixtures and remove legacy RAW progressive ingest code paths.

## Scope and Non-Goals

In scope:

- Progressive still-image inputs currently sourced from `*.raw` files.
- PAL and NTSC still resources under `resources/assets/*/stills/raw/`.
- Runtime progressive frame probing/decoding paths.
- Unit and functional verification required for migration confidence.

Out of scope for this migration:

- Changes to CVBS synthesis math, timing, or signal shaping.
- Changes to output encoding formats.
- Changes to MOV/MP4/PNG ingest behavior.

## Source and Target Data Contracts

Current source contract:

- Input payload: `yuv422p10le` packed as `Y0 Cb Y1 Cr` 16-bit little-endian words.
- Accepted rasters: `720x576`, `704x576`, `720x480`, `704x480`.
- One frame per file for progressive RAW fixtures.

Target OpenEXR contract:

- Container: OpenEXR scanline image.
- Compression: `ZIP` (lossless).
- Channels: `R`, `G`, `B`.
- Channel type: `HALF` or `FLOAT`.
- Stored value domain: preserve studio-range behavior without clamping so sub-black and super-white excursions remain representable.
- Required metadata attributes:
  - `videosynth.source_pixel_format = yuv422p10le`
  - `videosynth.source_sampling = 422_to_444_expanded`
  - `videosynth.color_model = rgb`
  - `videosynth.color_primaries = bt601`
  - `videosynth.transfer = bt601`
  - `videosynth.matrix = bt601_ycbcr_to_rgb`
  - `videosynth.code_range = studio` (semantic intent)
  - `videosynth.standard_hint = PAL|NTSC` (derived from raster)
  - `videosynth.source_width` and `videosynth.source_height`

Rationale for channel type choice:

- `HALF`/`FLOAT` enables standard RGB interoperability with DCC tools (including GIMP) while retaining out-of-range values when conversion and ingest paths avoid clamping.
- `ZIP` is lossless and broadly supported in OpenEXR implementations.

## Step 1: RAW Fixture Conversion to OpenEXR

### Work items

1. Create a dedicated conversion tool (or one-shot utility) that:
- Enumerates all RAW still fixtures for PAL and NTSC dimensions.
- Infers raster from file size using current accepted width/height rules.
- Decodes packed `yuv422p10le` words exactly as current runtime does.
- Expands to per-pixel YCbCr444 values, then converts to per-pixel RGB using a fixed BT.601 matrix.
- Writes `.exr` files with `R/G/B` channels and lossless `ZIP` compression.
- Explicitly disables conversion clamping/normalization that would discard headroom/footroom.

2. Output placement and naming:
- Create sibling EXR resources under `resources/assets/<raster>/stills/exr/`.
- Preserve basename parity (for example `100_BARS.raw` -> `100_BARS.exr`).

3. Conversion manifest:
- Emit a machine-readable manifest that records:
  - Input path, output path, width, height.
  - Min/max RGB values observed and whether any values are outside nominal display range.
  - Conversion metadata (matrix, range policy, channel type).
  - Checksums (input RAW and output EXR).

### Acceptance criteria

- Every RAW still fixture in PAL and NTSC resource trees has a corresponding EXR fixture.
- Re-decoding written EXR reproduces RGB channel values exactly from the conversion output.
- Conversion is deterministic (repeat runs produce identical channel values and metadata).

## Step 2: Replace Progressive RAW Loading with OpenEXR

### Work items

1. Add OpenEXR decode support via a library-backed implementation.
- Preferred: official OpenEXR C++ library integration in Nix/CMake environment.
- Decoder validates required channels and expected channel type (`HALF` or `FLOAT`).
- Decoder validates raster contract (`720/704` by `576/480`).

2. Progressive source support updates:
- Add `.exr` support in progressive source probing and frame generation.
- Add profile probing branch for EXR (container/codec/pixel format summary).
- Map decoded RGB EXR channels into `FrameSourceImage` and apply the inverse fixed BT.601 RGB->YCbCr transform required by the existing synthesis path.
- Ensure ingest path preserves studio-range excursions and avoids clipping prior to internal shaping.

3. Migration mode:
- Keep RAW decode path temporarily behind compatibility support until parity is proven.
- Update project fixtures to point progressive sections at EXR equivalents.

4. Documentation updates:
- Update design and user docs that list supported progressive source families and pixel-format contracts.

### Acceptance criteria

- Progressive sections using EXR load successfully for both PAL and NTSC fixture sets.
- Existing raster and geometry normalization behavior remains unchanged.
- RAW path can be retained temporarily for fallback; default fixture path uses EXR.

## Step 3: Prove Near-Parity Output

### Verification strategy

1. Fixture parity projects:
- Maintain paired project definitions for each representative fixture set:
  - RAW-backed project.
  - EXR-backed project.
- All non-source settings must be identical.

2. Output comparison:
- Generate outputs for RAW-backed and EXR-backed projects.
- Compare resulting outputs using strict numeric thresholds, not byte-for-byte equality.
- Require thresholds to pass for all paired PAL and NTSC fixtures.
- Suggested minimum acceptance thresholds:
  - Per-sample absolute error percentile: p99 <= 1 code value.
  - Mean absolute error (MAE): <= 0.25 code value.
  - Peak absolute error: <= 2 code values.
  - Optional waveform-domain correlation: >= 0.99999.

3. Test layering:
- Unit tests (fast, isolated):
  - EXR decode value mapping correctness.
  - Channel/raster validation behavior.
  - Matrix conversion and range-preservation behavior (including sub-black/super-white fixtures).
  - Error paths for malformed EXR metadata/channel layouts.
- Functional tests (filesystem-backed):
  - End-to-end RAW-vs-EXR paired project output comparison against tolerance thresholds.

4. Test classification compliance:
- Keep deterministic logic tests in `unit` classification.
- Put filesystem and full-pipeline comparisons in `functional` classification.
- Ensure CMake test registration explicitly classifies each new/modified test.

### Acceptance criteria

- All RAW-vs-EXR paired outputs meet near-parity thresholds for PAL and NTSC fixture suites.
- CI lanes pass with correct unit/functional separation.
- Any threshold failure produces diagnostics including fixture identity, summary metrics (MAE, peak, percentile), and first high-error location.

## Step 4: Decommission RAW Fixtures and Legacy RAW Paths

### Work items

1. Remove RAW progressive fixtures after parity sign-off:
- Delete superseded `resources/assets/*/stills/raw/*.raw` fixtures used for progressive ingest validation.
- Keep only RGB EXR fixture corpus as the canonical still-image source set.

2. Remove legacy RAW progressive ingest code:
- Remove `.raw` support from progressive source probing and section support checks.
- Remove RAW decode helpers and RAW-specific pixel-format validation branches used only for progressive still ingest.

3. Remove RAW-specific progressive project references:
- Retire RAW-backed parity project variants once Step 3 has passed in CI.
- Keep any required audit artifact (manifest/checksum reports) outside runtime fixture paths.

4. Update HLD and design documentation:
- Update the High-Level Design to remove progressive RAW ingest as an implemented source family.
- Add OpenEXR progressive ingest details to the implemented architecture, source support matrix, and validation constraints.
- Ensure any HLD-linked sub-spec sections that referenced RAW progressive ingest are aligned to EXR-first behavior.

5. Guard against regression:
- Add tests that assert `.raw` progressive sources are rejected with clear diagnostics.
- Add a repository check in CI that fails if new progressive RAW fixture files are introduced.

### Acceptance criteria

- No progressive RAW fixtures remain in active runtime resource paths.
- No progressive RAW decode/probe code paths remain in the runtime.
- High-Level Design and linked design sub-specifications no longer describe RAW progressive ingest as implemented behavior and do describe OpenEXR ingest as implemented behavior.
- Progressive source support matrix documents EXR/PNG/MOV/MP4 only (no RAW progressive ingest).
- CI passes with decommission checks enabled.

## Risks and Mitigations

1. Risk: RGB channel type mismatch or inconsistent precision across fixtures
- Mitigation: enforce one accepted channel type policy (`HALF` or `FLOAT`) and fail fast on mismatched type.

2. Risk: Matrix/range drift between conversion and runtime ingest
- Mitigation: centralize conversion constants and add unit tests that assert no-clamp behavior across footroom/headroom cases.

3. Risk: Hidden metadata assumptions across tools
- Mitigation: treat Videosynth metadata attributes as normative for ingest; validate strictly.

4. Risk: Test brittleness from filesystem-dependent fixtures
- Mitigation: keep unit tests data-local and deterministic; reserve fixture file IO for functional tests only.

## Rollout Sequence

1. Complete conversion tooling and generate EXR fixture corpus.
2. Land EXR probe/decode support with unit coverage.
3. Add functional RAW-vs-EXR near-parity tests and paired projects.
4. Switch fixture projects to EXR by default.
5. Remove RAW progressive fixture dependency only after sustained parity confidence.
6. Execute RAW decommission: remove RAW assets, remove legacy runtime RAW paths, update HLD/design docs, and enable regression guardrails.

## Definition of Done

- EXR fixture corpus exists for all current PAL/NTSC RAW still fixtures.
- Progressive runtime supports EXR sources for PAL and NTSC.
- RAW and EXR project outputs are proven extremely close across defined near-parity suite.
- RAW progressive fixtures and legacy RAW runtime paths are removed, with CI checks preventing reintroduction.
- High-Level Design and linked design documents are updated to remove RAW progressive ingest implementation claims and include OpenEXR implementation details.
- Documentation and test classifications are updated and consistent.
