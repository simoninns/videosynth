# VideoSynth Qt6 GUI — Phased Implementation Plan

## Scope and Goals

Add a Qt6 Widgets GUI (`videosynth-gui`) that provides a one-stop workflow for
creating, editing, validating, and generating VideoSynth projects, while the
existing CLI (`videosynth`) remains fully functional and unchanged in
behaviour. The GUI is a **project YAML authoring tool plus generation
front-end**:

- Edit overall project settings (video standard, sample encoding, output
  targets) and an ordered list of sections (video source, audio, VITS,
  laserdisc biphase, noise, dropouts, OSD, disc skips).
- Generate CVBS output in-process on a worker thread with progress and
  cancellation.
- Preview both the **source image/video** and the **encoded signal**
  (composite and Y/C) as field/frame views and per-line waveform views.
- Match [Decode-Orc](https://github.com/simoninns/decode-orc) in theme
  handling (auto/light/dark), overall layout conventions, and the About
  dialog (git commit hash + GPLv3 notice).

## GUI Workflow (as built)

The window flow supersedes the central-tab layout originally sketched in
Phase 5 (project settings tab) and Phase 7 (preview tab):

- **Empty start.** The application opens with no project loaded and shows a
  welcome surface (`WelcomePage`: logo, New/Open, recent files). All
  project-dependent actions are disabled until a project is created or opened.
- **New Project dialog.** `File > New` opens a modal `NewProjectDialog` that
  flows the user through creation — the **video standard** (the only static
  setting, locked afterwards) plus name, description, sample encoding, signal
  type, output targets, bursts, and optional asset base paths. It edits a
  private scratch `ProjectDocument` and returns the configured `Project`. On
  accept the project is **saved to disk as part of the create flow** (a save
  location is chosen immediately) so its relative paths always have a project
  directory to resolve against.
- **Path resolution via logical asset roots.** Section-source and output paths
  resolve through the shared core `ResolveProjectPaths`/`ResolveAssetPath`,
  which expand `{bundled}`/`{user}`/`{project}` brace tokens to real
  directories at runtime (GUI via `QStandardPaths`, CLI via env/`--asset-root`),
  with plain relative paths anchored to the saved project directory. The source
  editor exposes a root selector and a "resolves to …" hint; the source probe,
  preview, and generation share the one resolver so they never disagree. See
  HLD §7 "Asset Roots & Path Resolution".
- **Section view is the main window.** Once a project is open the central area
  is the per-section editor (`SectionEditor`), driven by the Sections dock on
  the left; there are no central tabs.
- **Edit Project dialog.** `Project > Edit Project…` opens a modal
  `EditProjectDialog` for the dynamic project-level settings (everything except
  the standard, shown read-only) plus the disc-skips table. OK applies the
  changes to the document; Cancel discards them.
- **Preview is a separate window.** `View > Preview` toggles a detached
  top-level `PreviewWindow` hosting the `PreviewPane`, with its own persisted
  geometry; "preview this section" from the Sections dock opens/raises it.

## Architectural Decisions

- **In-process backend, not CLI shell-out.** The GUI links `videosynth_core`
  directly and drives the same stage classes the CLI uses
  ([pipeline.h](../../include/videosynth/pipeline.h),
  [interfaces.h](../../include/videosynth/interfaces.h)). The CLI entry point
  ([main.cpp](../../src/main.cpp)) is untouched.
- **Qt6 Widgets** (not QML), matching Decode-Orc's `orc/gui` approach, with a
  ported `ThemeManager` (auto/light/dark via `Qt::ColorScheme` +
  `QStyleHints::colorSchemeChanged`) and shared theme colour tokens for
  custom-painted widgets.
- **The YAML project file remains the single source of truth.** The GUI edits
  an in-memory `videosynth::Project` and serialises it via a new YAML emitter;
  a GUI-saved project must be loadable by the CLI and vice versa. The YAML
  schema is defined by
  [high-level-design.md §7](../design/high-level-design.md#7-yaml-project-file-specification).
- **Generation must be bit-identical regardless of threading.** Multi-threaded
  generation must produce byte-identical output files to the current
  sequential path. Noise and dropout stages already derive per-frame seeds
  (`FrameSeed` in
  [dropout_injection_stage.cpp](../../src/dropout_injection_stage.cpp)), which
  is compatible with out-of-order frame synthesis.
- **GUI code lives under `src/gui/`** in a separate CMake target guarded by a
  `VIDEOSYNTH_BUILD_GUI` option (default `ON` when Qt6 is found), so the core
  library and CLI still build in environments without Qt.
- Testing follows [TESTING.md](../../TESTING.md) and
  [AGENTS.md §3](../../AGENTS.md): presenter/serialisation logic is covered by
  mocked, deterministic **unit** tests; anything requiring a `QApplication`,
  real files, or full pipelines is classified **functional** in
  `CMakeLists.txt`.

---

## Phase 1 — Pipeline Concurrency Foundations

Make the existing pipeline runnable from a non-main thread with progress
reporting and cooperative cancellation. This is a prerequisite for a
responsive GUI and is useful to the CLI as well.

### Task 1.1 — In-memory project execution path

- Add a pipeline entry point that accepts an already-parsed
  `videosynth::Project` (plus resolved base directory for relative source
  paths) instead of only a YAML file path, e.g.
  `VideoSynthPipeline::RunProject(const Project&, const RunOptions&)`.
- `Run(const RunOptions&)` becomes a thin wrapper: parse file → `RunProject`.
- **Acceptance criteria:**
  - CLI behaviour and exit codes unchanged (existing tests pass).
  - A unit test drives `RunProject` with mocked stages and verifies the same
    stage-call sequence as the file-based path.

### Task 1.2 — Progress observer and cancellation token

- Define `IPipelineObserver` in `include/videosynth/interfaces.h` with
  callbacks for stage transitions, per-batch frame progress
  (`frames_completed / frames_total`), warnings, and completion status.
- Define a `CancellationToken` (atomic flag, thread-safe, documented per
  [AGENTS.md §4.3.3](../../AGENTS.md)) checked between frame batches in the
  pipeline loop and inside long-running stage loops.
- Cancellation must leave no partially-written output behind: `OutputStage`
  gains an abort path that closes and removes in-progress video/metadata/WAV
  and dropout-sidecar files.
- **Acceptance criteria:**
  - Unit tests: observer receives monotonic progress; cancellation between
    batches stops the run and reports a cancelled (not error) status.
  - Unit test: abort path requests removal of all in-progress artefacts
    (filesystem interaction mocked behind the existing stage interfaces).
  - Observer and token are optional (null-safe); CLI passes none and behaves
    as before.

### Task 1.3 — Worker-thread execution contract

- Document and enforce the threading contract: a whole pipeline run executes
  on a single worker thread; `IGenerationStage`/`IOutputStage` stay
  single-owner (their headers already state NOT thread-safe); `ILogger`,
  `IProjectParser`, `IProjectValidator` remain thread-safe.
- Audit `SpdlogLogger` and stage constructors for hidden main-thread or
  global-state assumptions (static initialisation, `std::random_device` reuse,
  working-directory dependence) and fix any found.
- **Acceptance criteria:**
  - A unit test runs two sequential pipeline runs from a `std::thread` (not
    the test main thread) with mocked I/O and verifies identical results.
  - Thread-safety comments updated on all touched public classes.

### Task 1.4 — Deterministic-output regression harness

- Add a functional test that generates a short multi-section project (noise +
  dropouts + biphase + VITS + audio) twice and asserts byte-identical
  `.composite`/`.meta`/WAV/sidecar output (with fixed seeds). This baseline
  guards Phase 2.
- **Acceptance criteria:**
  - Test labelled `functional` in `CMakeLists.txt` per
    [TESTING.md](../../TESTING.md).
  - Baseline documented in the test so Phase 2 can reuse it.

---

## Phase 2 — Multi-Threaded Frame Generation

Parallelise sample synthesis across frames for throughput, without changing
output bytes. Frame generation is frame-scoped by design
([high-level-design.md §4](../design/high-level-design.md#4-generation-stage)),
but per-frame VBI code state (biphase picture numbers, chapter stop-bits,
white-flag cadence) is inherently sequential.

### Task 2.1 — Sequential per-frame context precomputation

- Split `GenerationStage` frame work into two passes:
  1. **Schedule enrichment (sequential, cheap):** extend
     `FrameScheduleItem` with the fully-resolved per-frame VBI payload
     (biphase/FM code words, white flag, VITC values), colour-sequence index,
     and OSD-resolved token strings, by advancing `BiphaseInjectionManager`
     and friends once over the whole schedule.
  2. **Sample synthesis (parallel-ready, expensive):** a pure function of
     `(Project, enriched FrameScheduleItem) → (Y, C)` with no mutable
     stage state.
- **Acceptance criteria:**
  - Sequential path refactored through the two passes with all existing unit
    tests passing unmodified (or with mechanical updates only).
  - Phase 1.4 regression harness still byte-identical.
  - New unit tests assert the enriched schedule matches the values previously
    produced frame-by-frame (picture numbers, timecodes, phase indices).

### Task 2.2 — Worker-pool frame synthesis

- Add a `FrameSynthesisPool` (std::thread pool, size from
  `std::thread::hardware_concurrency()`, overridable) that synthesises frames
  out of order and hands them to an ordered reassembly buffer with bounded
  memory (limit in-flight frames so RAM stays proportional to pool size, not
  project length).
- Noise and dropout injection run inside the per-frame job using the existing
  per-frame seed derivation, so results are order-independent.
- Dropout sidecar rows and audio samples must still be emitted in frame
  order; route them through the same ordered reassembly point.
- **Acceptance criteria:**
  - Functional test: 1-thread vs N-thread runs produce byte-identical
    video/metadata/WAV/sidecar output (reuses Phase 1.4 harness).
  - Unit tests for the reassembly buffer: ordering, bounded capacity,
    cancellation drains cleanly, worker exception propagates as a pipeline
    error.
  - Thread-safety guarantees documented on every new public class.

### Task 2.3 — CLI thread-count option and benchmark check

- Add `--threads <n>` to the CLI (default: auto) and a `threads` field to
  `RunOptions`; `--threads 1` selects the pure sequential path.
- Record a before/after throughput measurement (frames/s on a representative
  PAL and NTSC project) in the PR description per
  [AGENTS.md §5](../../AGENTS.md); flag any memory increase >10 %.
- **Acceptance criteria:**
  - `--validate` and default runs behave identically apart from speed.
  - Usage text and [high-level-design.md §14](../design/high-level-design.md#14-cli-interface)
    updated in the same task (HLD consistency rule,
    [AGENTS.md §7.2](../../AGENTS.md)).

---

## Phase 3 — GUI Scaffolding, Theming, Branding

Stand up the `videosynth-gui` application shell with Decode-Orc-style
theming, versioning, and the About dialog.

### Task 3.1 — Build system and Nix integration

- Add `find_package(Qt6 COMPONENTS Widgets Svg)` guarded by
  `VIDEOSYNTH_BUILD_GUI`; new target `videosynth-gui` under `src/gui/`
  linking `videosynth_core`; enable `CMAKE_AUTOMOC`/`AUTORCC` for that target
  only.
- Add Qt6 (`qt6.qtbase`, `qt6.qtsvg`, `qt6.wrapQtAppsHook`) to
  [flake.nix](../../flake.nix) build inputs and dev shell.
- **Acceptance criteria:**
  - `nix develop "path:$PWD" --command cmake --build build` produces both
    `videosynth` and `videosynth-gui`.
  - Configuring with `-DVIDEOSYNTH_BUILD_GUI=OFF` (or without Qt6 present)
    still builds core, CLI, and all existing tests.

### Task 3.2 — Version header from git commit hash

- Generate `version.h` at configure time via `configure_file`, mirroring
  Decode-Orc's scheme (`git rev-parse --short HEAD`, `-dirty` suffix via
  `git diff-index`, `PROJECT_VERSION_OVERRIDE` for Nix builds; reference:
  `orc/CMakeLists.txt` lines 33–60 in the Decode-Orc repository).
- Version string is used by both the GUI About dialog and a new CLI
  `--version` flag.
- **Acceptance criteria:**
  - `videosynth --version` prints the short hash; Nix build embeds the
    override string instead of "unknown".

### Task 3.3 — Placeholder logo and application icon

- Create `assets/videosynth-logo.svg` — a simple placeholder (e.g. stylised
  waveform/raster motif) usable as both logotype and square icon; export
  `assets/videosynth-icon-256.png` (plus 128/64/48/32/16 px sizes) from the
  SVG with a repeatable script `scripts/render-icons.sh` (rsvg-convert or
  Inkscape via `nix shell`).
- Add a Qt resource file `src/gui/videosynth_gui_resources.qrc` embedding the
  256 px PNG, and a freedesktop `.desktop` entry alongside the GUI sources
  (installation wiring can wait for Phase 8).
- **Acceptance criteria:**
  - PNGs regenerate bit-stably from the SVG via the script.
  - GUI window and About dialog display the icon from the resource system.

### Task 3.4 — Theme management

- Port Decode-Orc's `ThemeManager` (modes auto/light/dark; resolution via
  `QStyleHints::colorScheme` with palette-luminance fallback; live tracking of
  `colorSchemeChanged` in auto mode) and a `theme_color_tokens.h` for
  custom-painted preview widgets (luma/chroma/composite trace colours, region
  highlights, markers).
- Theme mode selectable from a View menu and persisted via `QSettings`.
- **Acceptance criteria:**
  - Unit tests (no `QApplication` where possible) for mode parsing and
    persistence round-trip.
  - Manual check: light/dark/auto switching restyles the main window without
    restart.

### Task 3.5 — Main window shell and About dialog

- `MainWindow` skeleton: menu bar (File / Edit / Project / Generate / View /
  Help), status bar, central placeholder; window geometry persisted with
  `QSettings`.
- About dialog matching Decode-Orc's (`QMessageBox`-based, rich text): logo
  pixmap, version (git hash) from Task 3.2, © Simon Inns, GPLv3-or-later
  notice with gnu.org link (reference: `MainWindow::onAbout` in Decode-Orc's
  `orc/gui/mainwindow.cpp`).
- **Acceptance criteria:**
  - `videosynth-gui` launches, shows themed shell, Help → About shows the
    current commit hash and GPLv3 text, and exits cleanly.

---

## Phase 4 — Project Document Model and YAML Round-Trip

Give the GUI a durable, validated in-memory representation of a project and
full YAML load/save. This phase has no editor widgets yet; it is the data
layer the editors bind to.

### Task 4.1 — YAML project emitter

- Implement `YamlProjectEmitter` in `videosynth_core`
  (`src/yaml_project_emitter.cpp`): serialises a `videosynth::Project` to the
  schema in
  [high-level-design.md §7](../design/high-level-design.md#7-yaml-project-file-specification),
  emitting only explicitly-set optional blocks (noise, dropouts, audio, osd,
  line_injections, disc_skips) so files stay minimal.
- **Acceptance criteria:**
  - Unit tests: parse → emit → parse round-trip yields an equal `Project`
    for every YAML in [docs/examples/](../examples/) and the test fixtures.
  - Emitted files validate cleanly through `ProjectValidator`.
  - Field ordering is stable/canonical so diffs of GUI-saved files are
    readable.

### Task 4.2 — GUI project document

- `ProjectDocument` (QObject): owns a `videosynth::Project`, file path, dirty
  flag; emits granular change signals (project settings changed, section
  added/removed/moved/edited); provides command-pattern mutations to enable
  a QUndoStack later without API churn.
- **Acceptance criteria:**
  - Unit tests for mutation + signal emission + dirty tracking (no
    `QApplication` needed for QObject-based tests with a `QCoreApplication`
    fixture; keep them deterministic and label `unit`).

### Task 4.3 — File lifecycle in the main window

- File → New (minimal valid PAL project from a template), Open, Save,
  Save As, Recent Files (QSettings), unsaved-changes prompt on close/replace;
  window title shows `name[*] — videosynth`.
- **Acceptance criteria:**
  - Open on every YAML in [docs/examples/](../examples/) succeeds and
    re-saving produces a file the CLI `--validate` accepts
    (functional test driving `ProjectDocument` + emitter, no widgets).

### Task 4.4 — Continuous validation surface

- Debounced background validation: on every document change, run
  `ProjectValidator` (thread-safe per
  [interfaces.h](../../include/videosynth/interfaces.h)) on a worker thread;
  publish results as a model of issues (severity, message, section index /
  field hint).
- Issues dock panel listing errors/warnings; double-click will later navigate
  to the offending editor (hook added now, navigation lands in Phase 5).
- **Acceptance criteria:**
  - Unit test: mutating a document to an invalid state produces the expected
    issue entries; fixing it clears them.
  - Validation never blocks the UI thread (verified by running it through the
    worker abstraction with a mocked slow validator in a unit test).

---

## Phase 5 — Project Settings and Section Editors

The authoring UI: overall settings plus per-section editing of video, audio,
and additional capabilities (VITS, biphase, noise, dropouts, OSD).

### Task 5.1 — Project settings editor

- Form for `project:` (name, version, description), `cvbs_presets:`
  (video_standard_preset, sample_encoding_preset, signal_state_preset,
  `pal_laserdisc_pilot_burst`, `ntsc_laserdisc_vbi_burst`,
  `ntsc_black_setup_ire`, field order/dominance, endianness) and `output:`
  (video path with file dialog, metadata path auto-derivation, signal_type
  composite/yc with `.y` suffix enforcement).
- Standard-dependent enablement (e.g. pilot burst PAL-only, setup IRE
  NTSC-only) driven by the same rules `ProjectValidator` applies.
- **Acceptance criteria:**
  - Every widget maps 1:1 to a `Project` field through `ProjectDocument`
    commands; unit tests cover the mapping layer (widget-free
    presenter/binding functions).
  - Invalid combinations are prevented or flagged inline consistent with
    validator messages.

### Task 5.2 — Section list management

- Sections dock: ordered list showing name, type, source, frame span; add
  (typed templates: progressive, laserdisc lead-in/programme/lead-out),
  remove, duplicate, reorder; automatic `start_frame` recalculation display.
- **Acceptance criteria:**
  - List stays in sync with `ProjectDocument` signals both ways.
  - Reorder/duplicate emit YAML identical to hand-written equivalents
    (unit test at the document level).

### Task 5.3 — Section editor: video source and span

- Per-section form: name, `type`, `source` (file picker for MKV/EXR), span
  (`start_frame`, `duration_frames` / `all`), with source probing via
  `IProgressiveFrameSourceProbe` on a worker thread showing the resolved
  profile (raster, bit depth, colour metadata) and pass/fail against the
  profiles in
  [high-level-design.md §8.1](../design/high-level-design.md#8-section-types).
- **Acceptance criteria:**
  - Probe results (or failure reasons) display without blocking the UI.
  - Unit tests for the probe-presenter with a mocked probe.

### Task 5.4 — Section editor: audio, noise, dropouts, OSD

- Collapsible sub-editors mapping to the optional YAML blocks:
  - `audio:` waveform/frequency/amplitude (+ ramp mode fields from
    `AudioParameters`),
  - `noise:` noise_db / noise_spread_db / optional seed,
  - `dropouts:` random and scratch parameter groups with seeds,
  - `osd:` overlay table (text with token help for `{picture_number}`,
    `{biphase_hex}`, `{phase_id}`, `{section_name}`; x/y/scale/fg/bg).
- **Acceptance criteria:**
  - Enabling/disabling a block adds/removes it from emitted YAML (never
    emits defaults for disabled blocks).
  - Range limits mirror validator rules (e.g. scale ∈ [1,4], amplitude
    ∈ [0,1]); presenter unit tests cover each block.

### Task 5.5 — Section editor: line injections and disc skips

- Line-injection editor per section: VITS (standard-filtered `vits_type`
  catalogue + `target_lines`), laserdisc biphase (`disc_type` CAV/CLV,
  `codes` with code_type/start_value/chapter/programme_status/users_code),
  VITC; plus a project-level `disc_skips` table (at_frame, direction, count).
- Issues-panel navigation from Task 4.4 wired: double-clicking an issue
  focuses the relevant editor/field.
- **Acceptance criteria:**
  - Editors constrain choices by section_type/standard using the validator's
    compatibility matrix (see
    [project_validator.cpp](../../src/project_validator.cpp)) rather than
    duplicating rules ad hoc.
  - Round-trip: examples in [docs/examples/](../examples/) opened, edited
    trivially, and re-saved remain CLI-valid (functional test).

---

## Phase 6 — In-GUI Generation

Run the full pipeline from the GUI with progress, logs, and cancellation,
using the Phase 1/2 infrastructure.

### Task 6.1 — Generation controller and worker

- `GenerationController` (QObject): starts `VideoSynthPipeline::RunProject`
  on a dedicated worker thread (QThread-owned worker object, per Decode-Orc's
  dropout-editor pattern), bridges `IPipelineObserver` callbacks to queued Qt
  signals, exposes cancel via the Phase 1 token; prompts to save (or
  generates from the in-memory project) when the document is dirty.
- **Acceptance criteria:**
  - Unit tests with a mocked pipeline: start/progress/finish/cancel/error
    signal sequences; only one run at a time; UI thread never executes
    pipeline code.

### Task 6.2 — Generation UI: progress, logging, results

- Generate menu/toolbar actions (Validate, Generate, Cancel); progress bar
  with frames-completed and current stage in the status bar; log dock backed
  by a thread-safe spdlog sink feeding a bounded `QAbstractListModel`
  (severity-coloured, theme-aware); completion summary with output file
  paths and an "open containing folder" affordance.
- **Acceptance criteria:**
  - Generating a real example project from the GUI produces output identical
    to the CLI run of the same YAML (functional test may drive the
    controller headlessly).
  - Cancel mid-run leaves no partial output files (relies on Task 1.2) and
    returns the UI to an idle state.

### Task 6.3 — Generation preferences

- Settings dialog page: thread count (auto/N, from Task 2.3), default log
  level, log-file toggle; persisted via `QSettings` and passed through
  `RunOptions`.
- **Acceptance criteria:**
  - Preferences survive restart and demonstrably alter the run (log level
    reflected in log dock; thread count reflected in pipeline debug log).

---

## Phase 7 — Signal Preview

Visualise both the source material and the encoded signal. Rendering reuses
the deterministic backend: previews are generated on demand through the
Phase 2 pure per-frame synthesis function, so what is previewed is exactly
what will be written.

### Task 7.1 — Preview frame service

- `PreviewFrameService`: given the current document and an absolute frame
  index, builds the enriched schedule (Task 2.1), synthesises that single
  frame (Y/C fixed-point mV buffers) on a worker thread, optionally applies
  noise/dropouts (toggle), and caches a small LRU of recent frames;
  invalidates on document change.
- Also exposes the decoded **source** image (10-bit YCbCr converted to
  display RGB via BT.601) for the frame's section when the section is
  progressive.
- **Acceptance criteria:**
  - Unit tests: cache invalidation on document change; frame indexing across
    section boundaries and disc skips matches `BuildFrameSchedule`.
  - Preview generation never blocks the UI thread; rapid scrubbing coalesces
    requests (latest-wins).

### Task 7.2 — Source and encoded picture views

- Tabbed central preview area with a frame/field navigator (slider +
  spinbox, field 1/2 selection):
  - **Source view:** the section's decoded progressive frame.
  - **Encoded picture view:** the synthesised frame rendered as a raster
    image — composite (Y+C combined, quantised to the 10-bit code space then
    mapped to grayscale) and Y/C mode (luma image + chroma image), honouring
    the project's `signal_type` while allowing manual composite/Y/C
    selection.
- Theme-aware painting via the Phase 3 colour tokens; zoom and aspect
  handling consistent with Decode-Orc's field preview widget conventions.
- **Acceptance criteria:**
  - Known test content (e.g. colour-bar source) renders with visually
    correct geometry for PAL (625) and NTSC (525), including VBI region
    visibility.
  - Switching composite ↔ Y/C and field 1 ↔ 2 updates without re-running
    full generation (uses cached Y/C buffers).

### Task 7.3 — Line waveform scope

- Waveform widget (Decode-Orc `waveformmonitorwidget` as stylistic
  reference): plots a selected line's samples in mV against the signal-level
  anchors from
  [high-level-design.md §6.1](../design/high-level-design.md#61-signal-levels)
  (sync tip, blanking, black, white gridlines); traces selectable as
  composite, Y, C, or Y+C overlay with token-based colours; line selection
  by clicking the picture view or via spinbox; cursor readout of
  sample index ↔ µs ↔ mV (and IRE for NTSC).
- **Acceptance criteria:**
  - Gridline values match the standard-specific tables (PAL −300/0/700 mV;
    NTSC −285.7/0/53.6/714.3 mV, IRE readout using 7.143 mV/IRE).
  - Unit tests for the sample↔time↔level mapping helpers (pure functions,
    no widgets).

### Task 7.4 — Preview/document integration polish

- Live re-preview (debounced) when the edited section affects the currently
  shown frame; "preview this section" action in the section list jumps to
  the section's first frame; indication when the preview is stale or the
  project is invalid (falls back to last-good frame with a banner).
- **Acceptance criteria:**
  - Editing e.g. noise level or an OSD overlay updates the visible frame
    within one debounce interval without UI stalls.
  - Invalid projects show the stale/invalid banner instead of crashing or
    blanking.

---

## Phase 8 — Documentation, Packaging, and HLD Alignment

### Task 8.1 — High-level design update

- Update [high-level-design.md](../design/high-level-design.md): architecture
  overview (GUI front-end + concurrency model), CLI section (`--version`,
  `--threads`), build & packaging (Qt6, `VIDEOSYNTH_BUILD_GUI`), directory
  structure (`src/gui/`, `assets/`, `docs/technical/`), per
  [AGENTS.md §7.2](../../AGENTS.md).
- **Acceptance criteria:** no code/HLD mismatches remain for GUI-affected
  behaviour; sub-specification links resolve.

### Task 8.2 — User documentation

- `docs/user/gui-guide.md`: workflow walkthrough (new project → sections →
  validate → preview → generate), theme modes, preferences, and a screenshot
  set; cross-link from [README.md](../../README.md).
- **Acceptance criteria:** a new user can produce a PAL colour-bar
  `.composite` file following only the guide.

### Task 8.3 — Desktop integration and install targets

- CMake install rules for `videosynth-gui`, icon hicolor tree from Task 3.3,
  `.desktop` file, and (optional) AppStream metainfo; Nix package builds and
  wraps the GUI (`wrapQtAppsHook`) alongside the CLI.
- **Acceptance criteria:**
  - `nix build` output contains working CLI and GUI binaries; the GUI
    launches from the Nix store path with icons and theme detection intact.
  - `ctest` full suite (unit + functional) passes in the flake check.
