# VideoSynth MVP Phased Implementation Plan

## 1. Objective

Deliver a minimum-viable VideoSynth implementation that can generate software test patterns for both PAL and NTSC and write composite-only CVBS-format output using only 4fsc subcarrier-locked sampling.

The MVP is intentionally narrow:

- Software-generated frame content only.
- PAL 625/50 and NTSC-M 525/59.94 only.
- 4fsc sample rates only:
  - PAL: 17,734,475 Hz.
  - NTSC: 14,318,180 Hz.
- Subcarrier lock enabled and required.
- Frame structure generation limited to core sync, colour burst, and basic VBI structure for each standard.
- Project structure is built as a Nix flake targeting Nixpkgs 25.11.
- Unit testing is required and must follow TESTING.md (dependency inversion, interface-first design, mock-based deterministic unit tests).
- CI/CD scaffolding is required using GitHub Actions to run unit tests and build the application.

## 2. Scope Boundaries

### In Scope (MVP)

- Initial project scaffolding and build wiring using a Nix flake pinned to 25.11.
- GitHub Actions workflow scaffolding for CI/CD (build + unit-test automation).
- CVBS project parsing with a minimal schema.
- Deterministic software test pattern generation in 10-bit 4:4:4 YCbCr BT.601 studio swing.
- Core PAL/NTSC timing model (lines/frame, line period, field order, frame rate).
- Basic non-visible line structure required to produce compliant vertical interval shape:
  - Equalizing pulses.
  - Vertical sync pulse blocks.
  - Horizontal sync on non-vertical-interval lines.
  - Colour burst insertion with PAL/NTSC phase behavior.
- Time-domain Y/C generation and composition pipeline.
- Output stage with 4fsc sampling, subcarrier lock, and 10-bit quantization.
- Composite-only CVBS video and metadata file emission (summed Y + C only).
- Validation and tests for PAL and NTSC golden outputs.
- Unit test architecture and implementation aligned with TESTING.md.

### Explicitly Out of Scope (Post-MVP)

- Progressive source ingest (MOV/MP4/PNG/RAW).
- VITS, VITC, laserdisc, or any line injection framework.
- 20 MSPS, 40 MSPS, or custom sample rates.
- Unlocked clock mode.
- Advanced analog behavior tuning (beyond basic pulse shaping needed for stable output).
- Extended CLI ergonomics beyond core run/validate flow.
- Replacing the Nix flake foundation with non-flake or non-25.11 build systems.
- Replacing GitHub Actions CI/CD with a different CI system in MVP.

## 3. MVP Functional Requirements

1. The tool shall accept only PAL or NTSC standard selection.
2. The tool shall accept only sample_rate = 4fsc and subcarrier_lock = true.
3. The generator shall produce a full frame structure for each standard with:
   - Valid sync tip, blanking, black, and white reference levels.
   - Correct equalizing and vertical sync pulse timing blocks.
   - Correct horizontal sync timing on non-vertical-interval lines.
   - Correct colour burst frequency and line-phase behavior.
4. The frame payload shall be software test patterns only.
5. The output stage shall quantize to legal 10-bit code ranges and avoid excluded values.
6. The generator shall remain internally separated into Y and C signal paths through generation.
7. The tool shall emit only summed composite video samples plus metadata in the CVBS format (no separate Y or C output files).
8. The repository shall include a Nix flake project structure pinned to Nixpkgs 25.11 for reproducible builds and tests.
9. Unit tests shall follow TESTING.md: dependency inversion via interfaces, constructor-injected dependencies, mocks for all external collaborators, and deterministic execution without clock/network side effects.
10. The repository shall include GitHub Actions workflows that automatically build the application and run the unit test suite on pull requests and mainline updates.

## 4. Phased Delivery Plan

## Phase 0: Foundation and Constraints Lock

### Goals

- Freeze MVP scope and reject non-MVP features at validation time.
- Build skeleton project flow: parse -> validate -> generate -> output.

### Work Items

- Create top-level project structure for a C++17 implementation (src, include, tests, tooling) suitable for incremental MVP development.
- Create or update flake.nix pinned to Nixpkgs 25.11 with dev shell and build/test outputs.
- Wire core dependencies needed by MVP in flake outputs (compiler toolchain, CMake or equivalent, yaml-cpp, spdlog, GoogleTest).
- Provide a baseline test target runnable from the flake environment.
- Scaffold GitHub Actions workflows:
  - CI workflow to build and run unit tests.
  - Trigger policy for pull_request and push to main/default branch.
  - Deterministic environment setup compatible with the Nix flake toolchain.
- Define minimal project schema:
  - standard: PAL | NTSC.
  - sample_rate: 4fsc only.
  - subcarrier_lock: required true.
  - section type: software_generated only.
- Implement validator hard-fail rules for all out-of-scope options.
- Create internal canonical timing and level constants for PAL/NTSC.
- Add logging and error reporting baseline.

### Exit Criteria

- Invalid configs are rejected with actionable messages.
- A valid but empty software pattern section can run through the full pipeline scaffold.
- Repository builds and runs tests from the flake-based environment pinned to 25.11.
- GitHub Actions CI workflow succeeds for both build and unit-test jobs.

## Phase 1: Timing Core and Frame Skeleton (No Active Pattern Yet)

### Goals

- Generate frame/field/line timing structure for PAL and NTSC.
- Produce composite waveform skeleton with blanking, sync, and burst.

### Work Items

- Build timing model primitives:
  - samples per line at 4fsc.
  - lines per frame and field sequencing.
  - half-line handling and VBI pulse sequence modeling.
- Implement sync generator:
  - horizontal sync pulse.
  - equalizing pulses.
  - broad vertical sync pulse intervals per standard.
- Implement burst window placement and phase model:
  - NTSC fixed burst phase.
  - PAL alternating burst phase behavior.
- Generate Y and C components in mV-domain doubles.

### Exit Criteria

- PAL and NTSC skeleton frames can be generated repeatedly with deterministic sample counts.
- Waveform inspection confirms expected pulse placement and burst presence.

## Phase 2: Software Test Pattern Engine (MVP Content)

### Goals

- Add active picture generation with software-defined test patterns only.

### Work Items

- Implement a minimal pattern set (recommended):
  - 75% colour bars.
  - grayscale ramp.
  - PLUGE/basic near-black steps.
- Define pattern interface that outputs 10-bit 4:4:4 YCbCr BT.601 studio swing frame buffers.
- Convert frame buffers to line-time Y/C contributions in active video regions.
- Preserve frame skeleton behavior from Phase 1 in all non-active intervals.

### Exit Criteria

- Each pattern renders for PAL and NTSC with stable colour lock and expected luma/chroma structure.
- No non-software content path is required or reachable.

## Phase 3: 4fsc Locked Output and CVBS File Emission

### Goals

- Complete output stage for production of usable CVBS files.

### Work Items

- Implement 4fsc sample clock generation phase-locked to subcarrier (NCO-based or equivalent deterministic lock).
- Map mV-domain waveform to 10-bit integer codes using standard-specific mapping anchors.
- Clamp to legal ranges and forbid excluded codes.
- Sum generated Y and C into composite at the output boundary and write only composite video samples plus metadata sidecar.
- Add deterministic output naming and metadata consistency checks.

### Exit Criteria

- PAL and NTSC jobs produce valid video and metadata files.
- PAL and NTSC jobs produce composite-only video files (no separate Y/C files) and metadata files.
- Metadata accurately reflects standard, 4fsc rate, and locked mode.

## Phase 4: Verification, Compliance Checks, and MVP Hardening

### Goals

- Prove correctness against MVP requirements and prepare for release.

### Work Items

- Implement and harden GitHub Actions workflows for MVP delivery:
  - Separate build and unit-test jobs (or clearly separated steps) with explicit failure reporting.
  - Cache strategy and dependency setup tuned for reproducible, deterministic runs.
  - Branch protection compatibility (status checks exposed for build and unit-test gates).

- Unit tests:
  - structure follows TESTING.md with interface-driven seams and dependency injection.
  - all dependencies mocked for unit tests; no filesystem, network, or clock side effects.
  - deterministic behavior guarantees for repeated runs in CI and local flake environments.
  - timing constants and line/field sequencing.
  - validator rejection cases for non-MVP settings.
  - quantization mapping and range checks.
- Golden/reference tests:
  - short PAL and NTSC clips per pattern.
  - hash or tolerance-based regression checks.
- Waveform compliance checks (automated where possible):
  - sync level and duration envelopes.
  - burst amplitude/frequency/phase behavior.
  - frame sample count invariants.
- CLI acceptance tests:
  - valid PAL/NTSC generation path.
  - invalid option combinations fail fast.

### Exit Criteria

- Test suite passes in CI for PAL and NTSC.
- Unit test suite conforms to TESTING.md strategy and determinism requirements.
- GitHub Actions CI/CD pipeline consistently builds the application and executes unit tests on PRs and mainline pushes.
- MVP acceptance checklist complete.

## 5. Suggested MVP Acceptance Checklist

- Generates PAL and NTSC output from software-generated patterns only.
- Uses 4fsc and locked subcarrier mode only.
- Produces correct basic frame structure:
  - VBI timing blocks are present and ordered correctly.
  - Sync and burst are present on expected line regions.
- Emits CVBS video plus metadata files with valid headers.
- Emits composite-only CVBS video plus metadata files with valid headers.
- Build and test workflow is flake-based on Nixpkgs 25.11.
- GitHub Actions CI/CD is scaffolded and operational for build + unit-test gates.
- Unit tests are deterministic and mock-based per TESTING.md.
- Rejects non-MVP features with clear validation errors.

## 6. Risks and Mitigations

- Timing drift or off-by-one sample errors at line boundaries.
  - Mitigation: integer/rational timing model and per-frame invariants.
- PAL/NTSC phase convention mistakes in burst/chroma.
  - Mitigation: known-vector tests for burst phase per line and per field.
- Quantization/clamping producing excluded digital codes.
  - Mitigation: explicit post-map sanitizer and tests for reserved code ranges.
- Scope creep from advanced features.
  - Mitigation: validator gates and a strict post-MVP backlog.

## 7. Post-MVP Roadmap Hooks

After MVP release, expand incrementally:

1. Progressive source ingest path.
2. Line injection framework (VITS, VITC, laserdisc).
3. Additional sample rates and unlocked mode.
4. Advanced analog shaping and calibration features.