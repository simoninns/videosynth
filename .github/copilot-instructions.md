# Copilot Instructions for videosynth

## Naming Requirements
- Do not use planning-phase terminology in source code or tests.
- Avoid names such as `phase0`, `phase1`, `phase2`, `mvp-phase`, `phase-based`, or similar.
- Use descriptive, domain-oriented names that describe behavior or content.
- Prefer names like `signal_timing`, `sync_generator`, `burst_policy`, `frame_structure`, etc.

## Unit Testing Requirements
- Follow `TESTING.md` as the source of truth.
- Unit tests must mock dependencies and stay deterministic.
- Unit tests must not depend on network, system clock, database, or external services.
- Unit tests should remain isolated and fast.
- Prefer interface-based dependency inversion and constructor injection.
- Keep test names behavior-focused and explicit.

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
- When tools are needed (build, test, lint, format, codegen), run commands inside the Nix shell.
- Preferred command style in this repository:
  - `nix develop "path:$PWD" --command <command>`
- If a plain shell command is suggested, provide its Nix-shell equivalent.

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
