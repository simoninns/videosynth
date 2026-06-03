# Copilot Instructions for videosynth

## Git Operations
- Do not add or commit anything to git unless explicitly asked by the user.
- This includes: `git add`, `git commit`, `git push`, `git stash`, and any other git operations that modify repository state.
- Read-only git operations (e.g., `git status`, `git log`, `git diff`) are permitted.

## Naming Requirements
- Do not use planning-phase terminology in source code or tests.
- Avoid names such as `phase0`, `phase1`, `phase2`, `mvp-phase`, `phase-based`, or similar.
- Use descriptive, domain-oriented names that describe behavior or content.
- Prefer names like `signal_timing`, `sync_generator`, `burst_policy`, `frame_structure`, etc.

## Unit Testing Requirements
- Follow `TESTING.md` as the source of truth.
- Unit testing is the primary testing methodology for this repository.
- Prefer unit tests first for all new or modified behavior.
- Unit tests must mock dependencies and stay deterministic.
- Unit tests must not depend on the filesystem, network, system clock, database, or external services.
- Unit tests should remain isolated and fast.
- Prefer interface-based dependency inversion and constructor injection.
- Keep test names behavior-focused and explicit.
- New or modified tests must be explicitly classified in `CMakeLists.txt` as either `unit` or `functional`.
- Use functional tests only when a required test objective cannot be achieved with unit tests alone.
- Tests that touch the filesystem, database, real media assets, or full pipeline rendering must be classified as `functional`, never `unit`.
- The default CI unit-test lane must remain focused on fast mocked tests; do not add functional coverage to that lane.

## Logging Requirements
- Use `info` for normal lifecycle messages, successful stage transitions, validation summaries, and other user-facing status output.
- Use `debug` for configuration details, branch decisions, resolved paths, and other troubleshooting information that is helpful during development but not needed in normal runs.
- Use `trace` for very detailed internal diagnostics such as per-frame, per-line, or per-sample state, tight loops, and intermediate values that are only useful when investigating low-level behavior.
- Prefer the least verbose level that still explains the behavior being logged; do not promote routine status messages to `debug` or `trace`.
- Keep log text concise and action-oriented so the same message can be understood in console output and file logs.

## Source File Header Requirements
- All C++ source and header files must include an SPDX-style header at the top of the file.
- Use this structure:
  ```
  /*
   * File:        source_code_name.cpp
   * Module:      module_name
   * Purpose:     Description of the purpose
   *
   * SPDX-License-Identifier: GPL-3.0-or-later
   * SPDX-FileCopyrightText: 2026 Contributor Name
   */
  ```
- `File` must match the actual filename.
- `Module` and `Purpose` must be descriptive and domain-oriented.
- The SPDX copyright line may vary depending on the contributor and year.

## Nix Environment Requirements
- The development environment is Nix.
- The provided project development shell is the default environment and is expected to contain all tools required to build, lint, format, and test this repository.
- For normal project workflows, run commands in the project shell using:
  - `nix develop "path:$PWD" --command <command>`
- If additional tools are needed for temporary tasks (for example: experimentation, profiling, debugging, or one-off analysis), use a temporary ad-hoc Nix shell that includes those tools instead of installing them into the workspace environment.
- Prefer command styles such as:
  - `nix shell nixpkgs#<tool> --command <tool> <args>`
  - `nix shell nixpkgs#<tool1> nixpkgs#<tool2> --command <command>`
- Do not modify project Nix files (`flake.nix`, `default.nix`) just to add temporary tooling unless the user explicitly requests a persistent dependency change.
- If a plain shell command is suggested, also provide its Nix-based equivalent.

## Definitive Specifications
- For CVBS file format requirements, treat the `docs/cvbs-file-format-specification/` submodule as authoritative.
- Start with:
  - `docs/cvbs-file-format-specification/README.md`
  - `docs/cvbs-file-format-specification/docs/index.md`
- For analogue video timing/standard requirements (PAL, NTSC, SMPTE/ITU/EBU references), treat the `docs/analogue-video-specifications/` submodule as authoritative.
- Start with:
  - `docs/analogue-video-specifications/README.md`
  - `docs/analogue-video-specifications/docs/index.md`
- If implementation details conflict with assumptions, align code and tests to these specification sources first.

## HLD and Code Consistency
- Treat `docs/design/high-level-design.md` as a living design document that must stay aligned with implemented behavior.
- Apply the same alignment requirement to HLD-referenced design sub-specifications (for example `docs/design/software-generated-patterns.md` and other documents linked from the HLD).
- When changing code that affects behavior described by the HLD, update the HLD in the same task whenever practical.
- When changing code that affects behavior described in HLD-linked sub-specs, update those sub-spec documents in the same task whenever practical.
- If you detect that current code behavior differs from the HLD or any HLD-linked sub-spec, explicitly warn the user about the mismatch.
- In that warning, offer to update the affected documentation to match the code (or, if requested, to update the code to match the documentation).
- Do not silently leave known code/documentation mismatches unresolved.

## Phased Implementation Plan Document Style
- Implementation plan documents must be structured as phased documents for agentic coding implementation.
- Break down implementation into manageable steps/phases that minimize context consumption (token usage) per phase.  Aim for less than 10 tasks per phase, ideally 3-5 tasks per phase, with clear task descriptions and acceptance criteria.
- Avoid unnecessary document decoration including table of contents, executive summaries, or similar non-essential sections.
- Do not include estimated man-weeks, timelines, or resource estimates in plan documents.
- Each phase must be self-contained and actionable as an independent implementation task.
- If the implementation plan references design documents, specifications, or other sources, include direct links to those sources in the relevant phase descriptions if available.
- All phases must follow testing requirements as described in `TESTING.md` and the unit testing requirements outlined above.
- There is no need for revision history or change tracking within the implementation plan documents; that is handled by git.

## No advertisements or promotions
- Do not include any advertisements, promotions, or references to commercial products, services, or brands in any generated content.  This includes "generated by" messages, "co-authored by" messages, or similar attributions that reference specific tools, services, or brands.

