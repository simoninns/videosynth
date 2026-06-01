# VideoSynth BT.601 Asset Migration Plan

## Purpose

This plan defines a staged migration from legacy fixtures in `resources/assets` to the BT.601-compliant assets in `videosynth-assets/assets`, including code and test updates required to ingest strict MKV/EXR formats without unintended rescaling, cropping, or other avoidable processing.

## Scope

- Replace fixture and project source references from `resources/assets` to `videosynth-assets/assets`.
- Extend progressive-source ingestion and validation from `EXR + MOV` to `EXR + MKV`.
- Add strict BT.601 compliance checks based on:
	- `videosynth-assets/docs/mkv-bt601-compliance-requirements.md`
	- `videosynth-assets/docs/exr-bt601-compliance-requirements.md`
- Remake all fixture projects under `tests/projects` to target the new assets.
- Remove legacy assets from `resources/assets` after migration completion.
- Update `docs/design/high-level-design.md` and any affected sub-spec references to match implemented behavior.

## Compatibility Policy (Normative)

- Backward compatibility with legacy progressive source families is explicitly out of scope.
- Legacy asset support is explicitly out of scope once migration is complete.
- The only supported progressive asset families are those defined by the `videosynth-assets` submodule requirements:
	- MKV assets that satisfy `videosynth-assets/docs/mkv-bt601-compliance-requirements.md`.
	- EXR assets that satisfy `videosynth-assets/docs/exr-bt601-compliance-requirements.md`.
- Any source outside those constrained profiles must be rejected with a validation error.

## Ingestion Validation Policy (Normative)

- Ingestion must perform strong pre-decode and decode-adjacent validation to confirm an input asset is supported before generation proceeds.
- Validation should be strict wherever the property is observable from container metadata, stream metadata, EXR headers, or decoded sample-domain evidence.
- Required observable checks should include, as applicable: container family, codec, pixel format/channel types, bit depth, raster, frame rate, field order, color metadata, and declared code-range semantics.
- For properties that are not fully provable from the asset alone (for example, some source-chain or filter-history constraints), ingestion must:
	- fail when project policy marks the property as mandatory and unprovable, or
	- emit an explicit warning/diagnostic when policy allows source-coupled verification outside ingestion.
- Unknown, ambiguous, or partially matching profiles must be rejected by default unless there is an explicit documented allowance in the submodule requirements.

## Non-Goals

- Introducing new output encodings unrelated to this migration.
- Changing analogue synthesis behavior unrelated to progressive source ingestion.
- Performing speculative performance optimization before correctness is proven.
- Preserving legacy MOV-based or legacy-asset fallback ingestion paths.

## Authoritative References

- `docs/cvbs-file-format-specification/README.md`
- `docs/cvbs-file-format-specification/docs/index.md`
- `docs/analogue-video-specifications/README.md`
- `docs/analogue-video-specifications/docs/index.md`
- `videosynth-assets/docs/mkv-bt601-compliance-requirements.md`
- `videosynth-assets/docs/exr-bt601-compliance-requirements.md`

## Current-State Gaps

- Progressive source family validation currently allows only `.exr` and `.mov`.
- Probe/profile validation currently enforces MOV/v210 constraints, not MKV/FFV1 constraints.
- Runtime decode path currently targets MOV decoding workflows and assumes legacy raster families.
- Fixture projects under `tests/projects` still reference `resources/assets`.
- Existing HLD sections still describe legacy source profiles and accepted dimensions.

## Migration Principles

- Preserve pixel-domain fidelity end-to-end.
- Prefer metadata-driven validation over implicit assumptions.
- Reject unsupported or ambiguous source profiles with explicit errors.
- Validate inputs as early and as strictly as possible in the ingestion path (where technically observable).
- Keep unit tests deterministic and isolated; classify filesystem/media-decoding tests as functional.
- Do not remove old assets until new fixtures, tests, and docs are all green.

## Phase 1: Baseline and Safety Net

### Objectives

- Freeze current behavior and establish regression observability before format changes.

### Work

- Capture baseline outputs for fixture projects that will be replaced.
- Add or update focused unit tests around:
	- source-family gatekeeping
	- raster/rate validation
	- profile validation error messaging
- Ensure current functional fixture tests are passing and classified correctly in `CMakeLists.txt`.

### Exit Criteria

- Baseline hashes/metadata snapshots archived for comparison.
- All existing unit and functional tests pass in the Nix shell.

### Execution Record (2026-06-01)

- Baseline snapshot captured in `docs/design/phase1-baseline-fixture-outputs.md`.
- Validator safety-net coverage expanded in `tests/test_project_validator.cpp` for:
	- source-family gatekeeping error messaging
	- raster mismatch rejection
	- frame-rate mismatch rejection
	- probe error propagation
- Fixture functional test expectations reconciled to current fixtures in `tests/test_fixture_projects.cpp`.
- Verification completed via:
	- `nix develop "path:$PWD" --command cmake --build build`
	- `nix develop "path:$PWD" --command ctest --test-dir build -R ProjectValidatorTest --output-on-failure`
	- `nix develop "path:$PWD" --command ctest --test-dir build -R ProjectFixturesTest --output-on-failure`

## Phase 2: Asset Inventory and Mapping Design

### Objectives

- Define a deterministic mapping between old fixture intent and new submodule assets.

### Work

- Build an asset manifest for `videosynth-assets/assets` covering:
	- format (`mkv` or `exr`)
	- raster (`720x576` or `720x486`)
	- frame rate and field order metadata
	- color metadata and BT.601 tags
- Map each existing fixture section to a replacement asset preserving test intent (bars, ramps, moving zone, etc.).
- Define path policy for tests and runtime examples:
	- canonical relative paths rooted at repository root
	- no fallback to `resources/assets`

### Exit Criteria

- Reviewed manifest exists and every current fixture section has a mapped replacement.
- Any gaps in the submodule corpus are identified with explicit follow-up tasks.

### Execution Record (2026-06-01)

- Asset manifest and section-by-section mapping captured in `docs/design/phase2-asset-manifest-and-mapping.md`.
- Canonical path policy established to enforce repository-relative submodule paths rooted at `videosynth-assets/assets` with no fallback paths.
- Current submodule corpus gaps identified:
	- missing PAL testcard still equivalent
	- missing PAL MKV motion assets for PLUGE and pt5300 fixture intents

## Phase 3: Progressive Source Profile Expansion (MKV + Strict EXR)

### Objectives

- Upgrade source probing and validation to enforce BT.601-aligned MKV/EXR profiles.

### Work

- Extend source-family validation from `EXR/MOV` to `EXR/MKV`.
- Update probe logic to validate MKV against required profile constraints, including:
	- container: Matroska
	- codec: FFV1
	- pixel format: `yuv422p10le`
	- raster: `720x576` (PAL) or `720x486` (NTSC)
	- frame rate: `25/1` or `30000/1001`
	- declared SD color metadata expectations
- Tighten EXR checks to match the submodule requirements (channels, type, compression, raster, metadata).
- Add explicit negative-path tests for unsupported/ambiguous inputs to prove fail-closed ingestion behavior.
- Replace MOV-specific validator/probe branches with MKV equivalents.
- Update validator test coverage for positive and negative MKV/EXR profile cases.

### Exit Criteria

- Validator and probe accept only approved MKV/EXR source profiles.
- Unit tests cover acceptance and rejection boundaries for each required field.

### Execution Record (2026-06-01)

- Progressive source family support switched from EXR+MOV to EXR+MKV in:
	- `src/project_validator.cpp`
	- `src/progressive_frame_source_probe.cpp`
	- `src/progressive_frame_source.cpp`
- MOV/v210 profile validation replaced with MKV/FFV1 profile validation including field-order and SD color metadata checks.
- EXR profile checks replaced with strict OpenEXR header requirements used by submodule assets (FLOAT RGB, no compression, full-raster windows, frame-rate metadata, gamma, pixel aspect tolerance).
- NTSC progressive raster handling updated to `720x486` source-domain geometry with active-window offset preserved for 480 active lines.
- Test fixtures and functional/unit tests updated to use submodule EXR/MKV assets for this phase's validation path.
- Verification completed via:
	- `nix develop "path:$PWD" --command cmake --build build`
	- `nix develop "path:$PWD" --command ctest --test-dir build -R "ProjectValidatorTest|FrameSourceTest|GenerationStageProgressiveTest|ProjectFixturesTest|ComplianceHarnessTest" --output-on-failure`

## Phase 4: Ingestion and Conversion Path Hardening (No Unnecessary Processing)

### Objectives

- Guarantee decode and conversion paths do not apply accidental geometric or color-domain transformations.

### Work

- Refactor progressive decode path to support MKV frame extraction directly.
- Codify geometry invariants:
	- no decode-time scale filters
	- no crop filters unless explicitly required by normative rule
	- no implicit aspect-ratio remap during sampling
- For NTSC `720x486` inputs, define and implement explicit mapping to generator-active lines using a documented BT.601/SMPTE-aligned rule set (not ad-hoc trimming).
- Ensure RGB/YCbCr conversion and code-range handling remain deterministic and bounded.
- Add compliance-oriented functional checks for:
	- active window placement
	- code legality ranges
	- field-order/temporal consistency

### Exit Criteria

- Functional tests prove no unintended rescale/crop in ingestion.
- NTSC `720x486` handling is deterministic, documented, and validated.

### Execution Record (2026-06-01)

- Decode-path ingestion hardening added in `src/progressive_frame_source.cpp` so MKV sources are revalidated with ffprobe immediately before decode.
- Decode-adjacent checks now fail closed on unsupported/ambiguous MKV metadata for:
	- container family (Matroska)
	- codec (`ffv1`)
	- pixel format (`yuv422p10le`)
	- bit depth (10-bit when declared)
	- raster and frame-rate per standard (`720x576@25` PAL, `720x486@30000/1001` NTSC)
	- field-order metadata (`tb` PAL, `bt` NTSC)
	- BT.601 SD color metadata expectations and SAR tolerance
- Functional checks added in `tests/test_progressive_frame_source.cpp` for:
	- NTSC active-window placement invariants (`active_y=3`, `active_height=480` on `720x486` input)
	- studio-range code legality bounds in decoded PAL/NTSC MKV active windows
- Test classification policy updated in `CMakeLists.txt` so all `FrameSourceTest.*` cases are explicitly labeled functional.
- Verification status:
	- Build succeeded with `nix develop "path:$PWD" --command cmake --build build`.
	- Targeted verification rerun succeeded with:
		- `nix develop "path:$PWD" --command ctest --test-dir build -R "ProjectValidatorTest|FrameSourceTest|GenerationStageProgressiveTest|ProjectFixturesTest|ComplianceHarnessTest" --output-on-failure`
	- Result: `47/47` tests passed in the phase-1-through-phase-4 scope.

## Phase 5: Fixture Project Remake and Test Migration

### Objectives

- Rebuild all test projects to use submodule assets and updated accepted profiles.

### Work

- Replace fixture YAML source paths in `tests/projects/*.yaml` with `videosynth-assets/assets/...` references.
- Remake PAL and NTSC fixture variants for both EXR and MKV source families.
- Update fixture expectations:
	- section counts and duration semantics where source lengths differ
	- frame-count assertions for `duration_frames: all`
	- hash baselines and metadata assertions
- Update relevant tests in:
	- `tests/test_fixture_projects.cpp`
	- `tests/test_project_validator.cpp`
	- `tests/test_progressive_frame_source.cpp`
	- `tests/test_yaml_project_parser.cpp`
- Keep classification accurate (`unit` vs `functional`) in `CMakeLists.txt`.

### Exit Criteria

- All fixture tests pass using only `videosynth-assets/assets` inputs.
- No test references to `resources/assets` remain.

### Execution Record (2026-06-01)

- Fixture project YAMLs were finalized for EXR and MKV families under `tests/projects`:
	- `pal_progressive_exr.yaml`
	- `ntsc_progressive_exr.yaml`
	- `pal_progressive_mkv.yaml`
	- `ntsc_progressive_mkv.yaml`
- PAL/NTSC MKV fixture naming and output artifact names were aligned to MKV terminology (project names and output filenames no longer use MOV labels).
- Fixture-consuming tests were updated to target MKV fixture files and naming in:
	- `tests/test_fixture_projects.cpp`
	- `tests/test_yaml_project_parser.cpp`
- Remaining test references to legacy `resources/assets` paths were removed from:
	- `tests/test_generation_stage.cpp`
	- `tests/test_project_validator.cpp`
- Verification completed via:
	- `nix develop "path:$PWD" --command cmake --build build`
	- `nix develop "path:$PWD" --command ctest --test-dir build -R "ProjectFixturesTest|YamlProjectParserTest|ProjectValidatorTest|FrameSourceTest|GenerationStage" --output-on-failure`
- Result: `65/65` tests passed in the phase-5 impacted scope.

## Phase 6: Legacy Asset Removal and Documentation Alignment

### Objectives

- Complete cutover by removing old assets and aligning design documents to implementation.

### Work

- Remove obsolete files under `resources/assets` that are superseded by submodule assets.
- Update repository docs that still reference legacy assets.
- Update HLD (`docs/design/high-level-design.md`) to reflect:
	- supported progressive source families (`EXR + MKV`)
	- accepted raster/fps combinations (`720x576` and `720x486` as implemented)
	- ingestion guarantees around no unnecessary geometric processing
	- updated fixture-source strategy
- Update any HLD-linked sub-spec sections affected by the migration.

### Exit Criteria

- `resources/assets` legacy corpus removed (or explicitly archived outside runtime/test paths).
- HLD and linked design docs match real runtime behavior and test corpus.

## Phase 7: Final Verification and Release Gate

### Objectives

- Confirm migration completeness with reproducible, policy-compliant verification.

### Work

- Run full build and test matrix in Nix shell.
- Execute targeted BT.601 compliance checks from submodule requirements docs against fixture assets.
- Perform repo-wide search to confirm:
	- no runtime/test references to `resources/assets`
	- no `.mov` assumptions in progressive-source validation paths
- Capture migration report including known limitations and follow-up tasks.

### Exit Criteria

- All tests pass.
- Compliance checks pass for designated fixtures.
- Documentation and code references are consistent.

### Execution Record (2026-06-01)

- Full release-gate build and test matrix passed in the Nix shell:
	- `nix develop "path:$PWD" --command cmake --build build`
	- `nix develop "path:$PWD" --command ctest --test-dir build --output-on-failure`
	- Result: `106/106` tests passed (`58 functional`, `48 unit`).
- Targeted BT.601 compliance and progressive-ingestion checks passed:
	- `nix develop "path:$PWD" --command ctest --test-dir build -R "ComplianceHarnessTest|FrameSourceTest" --output-on-failure`
	- Result: `14/14` tests passed (`ComplianceHarnessTest` and `FrameSourceTest` coverage for timing/code-space and MKV/EXR ingestion constraints).
- Repo-wide implementation-path checks confirmed no legacy references in runtime/test code:
	- `rg -n "resources/assets|\\.mov\\b" src tests include`
	- Result: no matches.
- Documentation consistency check outcome:
	- `docs/design/high-level-design.md` reflects current implementation contract for strict MKV/EXR profiles and `720x576`/`720x486` ingestion behavior.
	- Legacy literals remain in migration-history documents (this plan and phase-mapping artifacts) as historical traceability records, not as runtime/test source requirements.
- Phase 7 release gate status: complete.

## Implementation Order and Dependency Notes

- Phase 3 must land before Phase 5 to avoid fixture churn against unsupported profiles.
- Phase 4 and Phase 5 can run in parallel after initial probe/validator support lands, but final merge requires both.
- Phase 6 must happen after successful Phase 5 validation to avoid breaking CI during transition.

## Risk Register

- NTSC raster transition risk (`480` vs `486`) may expose hidden assumptions in timing and row mapping.
- Strict metadata enforcement may reject valid-but-under-tagged historical assets; policy for warnings vs hard errors must be explicit.
- Hash-based fixture baselines will change; expected-output management must be deliberate and reviewable.
- ffprobe/ffmpeg field-order and color-tag reporting differences across versions may require normalized checks.

## Validation Commands (Nix)

Primary form:

```bash
nix develop "path:$PWD" --command cmake -S . -B build
nix develop "path:$PWD" --command cmake --build build
nix develop "path:$PWD" --command ctest --test-dir build --output-on-failure
```

Optional focused runs during migration:

```bash
nix develop "path:$PWD" --command ctest --test-dir build -R ProjectValidatorTest --output-on-failure
nix develop "path:$PWD" --command ctest --test-dir build -R ProjectFixturesTest --output-on-failure
```

## Deliverables Checklist

- New MKV/EXR ingestion and validation support implemented.
- All fixture projects remade for `videosynth-assets/assets`.
- Legacy `resources/assets` removed from runtime/test usage.
- HLD and linked design docs updated to match implementation.
- Unit/functional test coverage updated and passing.
