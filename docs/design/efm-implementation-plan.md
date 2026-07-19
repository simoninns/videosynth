# EFM Digital Audio Implementation Plan

**Document ID:** VS-DESIGN-EFM-001
**Related HLD Sections:** 5 (Output Stage), 7 (YAML Project File Specification, `audio:` sub-key), 12 (Implementation Pipeline)
**Related Specifications:** IEC 60856:1986 Amendment 2 (PAL), IEC 60857:1986 Amendment 2 (NTSC), IEC 60908:1999 (CD-DA), ECMA-130, SMPTE 272M-1994

---

## Background

videosynth already synthesises up to eight stereo audio channel pairs per section
(`section.audio.channel_pairs`, pairs 0–7) and writes each pair as a 48 kHz /
24-bit stereo WAV file alongside the video output:

- `AudioSynthesizer` (`src/audio_synthesizer.cpp`) — per-channel tone synthesis at a
  constructor-fixed sample rate.
- `AudioTrackGenerator` (`src/audio_track_generator.cpp`) — per-pair orchestration with a
  streaming `Begin` / `EmitFrame` / `Finalize` / `Abort` contract driven by the pipeline in
  output-frame order.
- `AudioWavWriter` (`src/audio_wav_writer.cpp`) — RIFF/WAVE writer;
  output path derived from the video path as `<basename>_audio_<pair>.wav`.

This plan adds LaserDisc **digital audio (EFM)** output as an *addition* to that
subsystem: the user selects **one** existing audio channel pair, and that pair is
additionally encoded as an IEC 60908-1999 EFM channel stream with LaserDisc q-mode 4
subcode, written next to the WAV file. Audio parameter handling (waveform,
frequency, amplitude, ramps, per-section configuration) is unchanged and shared
between the WAV and EFM paths.

Per the project requirement, all EFM signal processing is implemented as a
**separate module** — a self-contained static library with a narrow public
interface — so that later functionality (decoder, data modes, RF modulation)
can be added without disturbing the core audio subsystem.

---

## Referenced Specifications

Local documents (in the analogue video specifications sub-repo):

| Specification | Location | Governs |
|---|---|---|
| IEC 60856:1986 Amendment 2, clause 13 | [IEC-60856-1986-Laservision-PAL-Amendment-2.md](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL-Amendment-2/IEC-60856-1986-Laservision-PAL-Amendment-2.md) | PAL LD digital audio: EFM signal usage (13.1), sample frequency Fs = 1764/625 × F_H, 44.1 kHz nominal (13.2), decoder delay compensation (13.3), subcode with mode-4 DATA-Q modification, ADR = 4 = 0100 (13.5.1), TOC placement (13.5.2), track/chapter relation (13.5.3) |
| IEC 60857:1986 Amendment 2, clause 13 | [IEC-60857-1986-Laservision-NTSC-Amendment-2.md](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC-Amendment-2/IEC-60857-1986-Laservision-NTSC-Amendment-2.md) | NTSC LD digital audio: EFM signal usage (13.1), Fs = 7007/2500 × F_H, 44.1 kHz nominal (13.2), modulation polarity (13.5), subcode mode-4 modification (13.6.1), TOC (13.6.2), track/chapter relation (13.6.3) |
| IEC 60856:1986 (base), 10.1.1 / 10.1.2 | [IEC-60856-1986-Laservision-PAL.md](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) | LV lead-in/lead-out codes whose disc positions the CD lead-in/lead-out subcode areas must match (13.5.2.1 / 13.5.2.2) — realised here by aligning TOC and lead-out subcode with the project's `lead_in` / `lead_out` sections |
| SMPTE 272M-1994, 3.8 / 14.3 Table 1 | [SMPTE-272M-1994.md](../analogue-video-specifications/docs/video_formats/SMPTE-272M-1994/SMPTE-272M-1994.md) | Audio frame sequences for 44.1 kHz against 29.97 Hz video: 147147 samples per 100-frame sequence; odd frames 1472, even frames 1471, exception frames 23, 47, 71 = 1471. PAL/625: 1764 samples per frame, sequence length 1 |
| IEC 60908:1999 (CD-DA, "Red Book") | [IEC-60908-1999.md](../analogue-video-specifications/docs/efm/IEC-60908-1999/IEC-60908-1999.md) | Clause 12: disc areas, 44.1 kHz / 16-bit two's-complement samples; clause 13: EFM code, T_min = 3 / T_max = 11, three merging bits; clause 14: frame format (588 channel bits: 24-bit sync + 34 × 3 merging + 33 × 14 symbol bits); clause 15: EFM modulator; clause 16.2/16.3: CIRC — C1 (32,28) and C2 (28,24) over GF(2⁸), P(x) = x⁸+x⁴+x³+x²+1, primitive element α = 00000010 (LSB right), inverted parity, WmA = high byte / WmB = low byte; clause 17.3: 98-symbol subcode blocks at 75 Hz with S0/S1; 17.4: channel P flag rules; 17.5: Q format, control field, ADR values, CRC-16 P(X) = X¹⁶+X¹²+X⁵+1 (inverted, MSB first); 17.5.1: mode-1 DATA-Q layouts (lead-in TOC with POINT/PMIN/PSEC/PFRAME and A0/A1/A2; audio/lead-out with TNO/X/TIME/ATIME), 4 s minimum track length |
| ECMA-130, clause 19 and annexes C–E | [ECMA-130.md](../analogue-video-specifications/docs/efm/ECMA-130/ECMA-130.md) | Machine-readable cross-check of the CD layer (its Annex C CIRC "is the same as that described in IEC 908", C.1): 19.1: S0 = 00100000000001, S1 = 00000000010010; 19.2: 24-bit sync header; 19.4: 588-bit channel-frame composition; Annex C: CIRC encoder in text form — C.3 two-frame delay on one word group, C.5 0–27 × D (D = 4) interleave, C.8 one-frame alternate delay, C.9 parity inversion and 3/108-frame min/max delays, figure C.4 output byte-sequence table; Annex D: the complete 8-bit → 14-channel-bit table; Annex E: merging-bit selection algorithm with DSV minimisation |

Notes:

- IEC 60856/60857 Amendment 2 define the LaserDisc digital audio system *by
  reference to* IEC 60908, modifying only the Q-channel ADR (mode 4 instead of
  mode 1) and the disc-level placement rules. The mode-4 DATA-Q layout is
  therefore the IEC 60908-1999 17.5.1 mode-1 layout with ADR = 0100. (IEC
  60908-1999's own 17.5 lists ADR 4 by deferring to IEC 61104 (12 cm CD-V),
  which is what the amendments' "IEC 60908-2" reference corresponds to; the
  amendments' renaming is authoritative for LD.) All bit-level encoding
  constants cite IEC 60908-1999 in code comments, per the AGENTS.md
  specification-reference format.
- Transcription caveat: the vendored ECMA-130 markdown prints the 24-bit sync
  header (19.2) with only 22 digits — two zeros were lost in digitisation. The
  canonical pattern is 100000000001000000000010 (two maximum-length 11T runs),
  consistent with IEC 60908-1999 clause 14 and Annex A. Implementations must
  not copy the 22-digit rendering.
- Clause 13.1 of both amendments (low-pass/high-pass filtering, pre-emphasis,
  pulse-width modulation onto the main carrier, −27 dB level) applies to the
  *RF/optical mastering domain*. videosynth generates baseband CVBS, so 13.1 is
  out of scope for this plan; the module boundary is chosen so an RF modulation
  stage can consume the same EFM bitstream later.

---

## Scope and Design Decisions

In scope:

1. **Separate module.** All EFM/CIRC/subcode logic lives in a new static library
   `videosynth_efm` (`src/efm/`, `include/videosynth/efm/`, namespace
   `videosynth::efm`) with **no dependency on `videosynth_core`** — its API
   accepts 16-bit stereo PCM and configuration, and emits an EFM channel stream.
   The core links the module; not vice versa.
2. **One selectable pair.** A project-level output setting selects exactly one
   audio channel pair (0–7) for EFM encoding. That pair still produces its WAV
   file; the EFM file is additional.
3. **Independent 44.1 kHz synthesis.** The EFM path synthesises audio at
   44.1 kHz directly from the same per-section `AudioParameters` used by the
   48 kHz WAV path (no resampling). Both amendments give exactly 44 100 Hz
   against nominal line frequency: PAL 1764/625 × 15 625 Hz; NTSC
   7007/2500 × (4.5 MHz/286). Samples are produced at 24-bit and converted to
   the CD's 16-bit domain by rounding with saturation.
4. **Q-mode 4 audio only (no data).** The subcode Q channel uses ADR = 4 = 0100
   (IEC 60856 Amd 2, 13.5.1.1 / IEC 60857 Amd 2, 13.6.1.1) with the mode-1 data
   layout renamed mode 4 (TNO, INDEX, running time, absolute time, CRC).
   Control nibble fixed at 0000 (2-channel audio without pre-emphasis, copy
   prohibited, per IEC 60908-1999, 17.5). INDEX (X) = 01 within tracks; the
   only pause encoding (X = 00) is the mandatory 2–3 s pause preceding the
   first track (IEC 60908-1999, 17.5.1). No CD-ROM data modes, no R–W graphics
   (zero-filled per IEC 60908-1999, 17.6).
5. **One track per programme-area section, numbered by section sequence.**
   TNO is assigned sequentially (01, 02, …) over the project's `programme_area`
   sections in output order — the numbering source is purely the section
   sequence, independent of any `chapter_number` biphase codes. Running time
   (MIN/SEC/FRAME) resets at each track boundary; absolute time
   (AMIN/ASEC/AFRAME) is zero at programme-area start and continuous across
   tracks. Maximum 79 tracks (IEC 60856 Amd 2, 13.5.3.3 / IEC 60857 Amd 2,
   13.6.3.3). Note: 13.5.3 / 13.6.3 tie TNO to LV chapter numbers; aligning
   section-sequence numbering with chapter codes is deferred (see Future
   Expansion). Track transitions follow IEC 60908-1999, 17.4 / 17.5.1: the
   first 2 s of the first programme-area section are encoded as the track-1
   pause (X = 00, running time counting down, digital silence in the EFM audio;
   the 48 kHz WAV path is unaffected), every track start is announced by a
   channel-P start flag occupying the final 2 s of the preceding track (or the
   pause, for track 1), and later tracks have no pause encoding (a pause is
   optional for tracks after the first, 17.5.1).
6. **TOC and lead-out subcode mapped from section types.** The EFM stream spans
   the whole generated output (silence where the selected pair is absent from a
   section), matching the WAV behaviour. `lead_in` sections carry the
   repetitive mode-4 TOC (IEC 60856 Amd 2, 13.5.2 / IEC 60857 Amd 2, 13.6.2):
   one POINT entry per track with its start time (PMIN/PSEC/PFRAME), plus the
   A0/A1/A2 entries (first track, last track, lead-out start) per IEC
   60908-1999, 17.5.1, with the video system identification carried in the
   P FRAME field of the POINT = A0 entry (a field IEC 60908-1999, 17.5.1
   otherwise sets to zero): 22 (PAL, digital stereo) or 12 (NTSC, digital
   stereo) per 13.5.2 / 13.6.2. `lead_out`
   sections carry lead-out subcode (TNO = AA). The full track table is
   computable before encoding starts because `AudioTrackGenerator::Begin`
   receives the resolved section layout. Projects without `lead_in` /
   `lead_out` sections produce a programme-area-only stream (with a validation
   warning that no TOC is emitted).
7. **Output format: T-value stream.** The EFM channel stream is written as one
   unsigned byte per pit/land run length (values 3–11, i.e. T3–T11), starting at
   the first frame sync — directly consumable by existing EFM decoding tooling.
   Path derived as `<basename>_audio_<pair>.efm` beside the WAV.
8. **Standards: PAL and NTSC only.** Validation rejects EFM output for other
   video standard presets (e.g. PAL-M), which have no LD digital audio
   specification.

Out of scope (enabled by the module boundary; see Future Expansion):

- EFM decoder, chapter-code-driven TNO alignment (13.5.3 / 13.6.3), bilingual
  mode, the 15.3 ms audio advance recommendation (13.3), analogue subcarrier
  status code interaction (13.4), and RF-domain filtering/PWM (13.1).

---

## Module Architecture

New library `videosynth_efm` (no core dependencies):

| Component | Files | Responsibility |
|---|---|---|
| Frame assembly | `include/videosynth/efm/audio_frame_assembler.h`, `src/efm/audio_frame_assembler.cpp` | Groups six 16-bit stereo samples into 24-byte frames, byte order per IEC 60908-1999, 16.2 |
| CIRC encoder | `include/videosynth/efm/circ_encoder.h`, `src/efm/circ_encoder.cpp` | GF(2⁸) Reed–Solomon C2 (28,24) and C1 (32,28) encoding with the delay/interleave structure of IEC 60908-1999, 16.3 / ECMA-130 Annex C, and parity inversion; explicit priming/flush with digital silence |
| Subcode generator | `include/videosynth/efm/subcode_generator.h`, `src/efm/subcode_generator.cpp` | 98-frame subcode sections with S0/S1 sync patterns; P channel; Q channel mode 4 (ADR = 4) with BCD time fields and inverted CRC-16 (x¹⁶+x¹²+x⁵+1). Driven by a precomputed track table (`{TNO, start, duration, area}`): repetitive TOC in lead-in, per-track Q data in the programme area, lead-out (TNO = AA) |
| EFM modulator | `include/videosynth/efm/efm_modulator.h`, `src/efm/efm_modulator.cpp` | Eight-to-fourteen lookup, merging-bit selection (run-length 3–11 preserved, DSV minimised), 24-bit frame sync, 588-channel-bit frame serialisation, channel bits → T-values |
| Stream encoder facade | `include/videosynth/efm/efm_stream_encoder.h`, `src/efm/efm_stream_encoder.cpp` | The module's public API: `Begin(config)` → `PushSamples(...)` → `Flush()`; internally chains assembler → CIRC → subcode → modulator and exposes encoded T-values for the caller to drain |

Module conventions (established with the CIRC encoder):

- Frames are fixed-size value types: `F1Frame` = `std::array<std::uint8_t, 24>`
  (audio symbols in WmA/WmB order, IEC 60908-1999, 16.2) and `F2Frame` =
  `std::array<std::uint8_t, 32>` (24 data plus 8 inverted parity symbols,
  ECMA-130, figure C.4). `kSilentF1Frame` is the digital-silence frame.
- The subcode stage is index-addressed rather than streaming: `SubcodeGenerator`
  takes the whole `TrackTable` (`{area, TNO, start_section, section_count}`
  entries tiling the stream, plus the `VideoSystem` used for the POINT = A0
  identification) in `Begin`, after which `GenerateSection(index, …)` is a pure
  function and is safe to call concurrently. A `SubcodeSection` carries the 96 P
  and 96 Q channel bits of one 98-frame block; frames 0 and 1 carry the
  out-of-table sync patterns `kSubcodeSyncS0` / `kSubcodeSyncS1` instead of a
  control symbol, and `ControlByte(frame)` composes the P/Q/R–W symbol the
  modulator serialises.
- Stages are streaming and single-threaded: repeated push calls followed by an
  explicit `Flush`, with `Reset` restoring the constructed state. Each stage
  documents its thread-safety in its header; none is thread-safe.
- Failures are reported by return value (`false`) and no exception crosses the
  module's public API. Invalid arguments (null output, mismatched channel
  lengths) leave the stage unchanged.

Integration points in `videosynth_core` (thin, mirroring existing patterns):

- `AudioEfmWriter` (`include/videosynth/audio_efm_writer.h`, `src/audio_efm_writer.cpp`) —
  file writer with the same streaming contract as `AudioWavWriter`
  (`BeginWrite` / `AppendFrameAudio` / `FinalizeWrite` / `AbortWrite`), owning an
  `efm::EfmStreamEncoder`. This is the only core component that touches the module.
- `AudioTrackGenerator` — for the selected pair only, adds a second
  `AudioSynthesizer` pair constructed at 44 100 Hz and feeds the `AudioEfmWriter`
  from `EmitFrame` in output-frame order.
- `include/videosynth/audio_sample_conversion.h` — 24-bit → 16-bit sample
  conversion (round to nearest, saturate) taking synthesised samples into the
  IEC 60908-1999 clause 12 sample domain before they reach the module.
- Model / parser / validator / emitter — a project-level `output.efm_audio`
  setting (see Phase 1).
- Qt GUI — EFM enable and pair selection in the project settings editor.

Derived timing facts used throughout (all cited in code where used):

- 44 100 samples/s → 7350 EFM frames/s (6 stereo samples per frame, IEC
  60908-1999, clause 14 / 16.2) → 75 subcode sections/s (98 frames per
  section, IEC 60908-1999, 17.3).
- PAL: 1764 samples per video frame (exact); 294 EFM frames and exactly 3
  subcode sections per video frame.
- NTSC: 147147 samples per 100 video frames (SMPTE 272M-1994 Table 1: odd
  frames 1472, even frames 1471, frames 23/47/71 = 1471); EFM frame and subcode
  boundaries do not align with video frames, so the stream encoder buffers
  samples and emits completed frames as they fill.

### Timing Alignment Contract

The EFM output must stay time-aligned with the WAV written for the same pair.
The contract is:

- **Shared datum.** t = 0 of the output timeline is common to video frame 0,
  WAV sample 0 (48 kHz grid), EFM source sample 0 (44.1 kHz grid), and Q
  absolute time 00:00:00. The CIRC delay registers are initialised to digital
  silence, which is exactly equivalent to silence having preceded t = 0.
- **Delay sources.** There is no scrambler in the CD-DA path (the ECMA-130
  Annex B scrambler belongs to the CD-ROM sector layer only), and EFM
  modulation and merging bits are timing-neutral bit-domain transforms. The
  only structural delay is the CIRC interleave: each input frame's bytes are
  spread over output frames n+3 to n+108 (ECMA-130, C.9).
- **Documented decode invariant.** A complementary de-interleaving CIRC
  decoder emits exactly 108 F1 frames — 648 stereo samples, ≈14.69 ms at
  44.1 kHz — of pipeline warm-up silence before source sample 0. After
  discarding that constant warm-up, decoded PCM is sample-exact against the
  44.1 kHz source from t = 0, and therefore time-aligned with the WAV, since
  both sample grids share the datum. The symmetric flush at end of stream
  pushes the final in-flight bytes out with silence padding.
- **Named constant.** The module exports `kCircPipelineLatencyFrames = 108`
  (ECMA-130, C.9) so consumers discover the offset from the API rather than
  measuring it empirically.
- **Documented, not pre-compensated.** The stream does not shift audio early
  to hide the warm-up: pre-compensation would break the shared datum against
  the subcode timeline and the WAV, and would make any decoder that trims its
  own pipeline see audio ≈14.7 ms early. For a signal-generation tool whose
  outputs are decoded against known ground truth, the documented constant is
  the correct convention.
- **Known content divergences** (timeline-aligned, content-different): the
  first 2 s of track 1 are the mandatory pause — digital silence in the EFM
  audio while the WAV carries the section's tone (IEC 60908-1999, 17.5.1);
  and the 15.3 ms player decode-delay advance of IEC 60856/60857 Amd 2, 13.3
  is a player-side A/V compensation, out of scope for file output (see Future
  Expansion).

---

## Phase 1: Configuration Model and Validation

**Dependencies:** None

**Purpose:** Allow a project to select one audio channel pair for EFM output, with
full parse/emit/validate round-trip.

| ID | Task | Acceptance criteria |
|----|------|---------------------|
| 1.1 | Add an `EfmAudioOutput` structure (`enabled`, `pair`) to `OutputTargets` in `include/videosynth/model.h`, including equality operators | Compiles warning-free; equality covers new fields |
| 1.2 | Parse `output.efm_audio` (`pair:` key) in `src/yaml_project_parser.cpp`, extending the output-section allowed-keys guard; emit it in `src/yaml_project_parser.cpp`'s counterpart `src/yaml_project_emitter.cpp` | A project with `efm_audio` parses; parse → emit → parse yields an identical model |
| 1.3 | Validate in `src/project_validator.cpp`: `pair` in [0, 7]; video standard preset is PAL or NTSC; warning (not error) when the selected pair is not declared in any section | Invalid pair and non-PAL/NTSC presets produce errors naming the offending value; undeclared pair produces a warning |
| 1.4 | Validate the track layout when EFM is enabled: error when the project has more than 79 `programme_area` sections (IEC 60856 Amd 2, 13.5.3.3 / IEC 60857 Amd 2, 13.6.3.3); warning for tracks shorter than 4 s excluding any pause (IEC 60908-1999, 17.5.1) — for the first track this means shorter than 6 s, since its first 2 s are the mandatory pause; warning when no `lead_in` section exists (no TOC will be emitted) | Each condition produces the stated diagnostic naming the offending section |
| 1.5 | Unit tests for parser, emitter, and validator behaviour; register the new suite names in the `videosynth_unit_test_patterns` list in `CMakeLists.txt` | New tests pass and are labelled `unit`; all existing tests pass |

**Validation:**
- `output.efm_audio` absent → behaviour identical to current builds.
- CMake test-label generation succeeds (no unmatched suites).

---

## Phase 2: 44.1 kHz Sample Timing Foundations

**Dependencies:** Phase 1

**Purpose:** Provide exact, standard-cited 44.1 kHz sample counts per video frame
and a second synthesis path at that rate.

| ID | Task | Acceptance criteria |
|----|------|---------------------|
| 2.1 | Add `EfmAudioSampleRateHz()` (44 100) and `EfmAudioSamplesForFrame(Standard, frame_index)` to `include/videosynth/timing_constants.h`: PAL constant 1764; NTSC 100-frame sequence per SMPTE 272M-1994 Table 1 (odd 1472, even 1471, frames 23/47/71 = 1471). Cite IEC 60856 Amd 2 13.2, IEC 60857 Amd 2 13.2, and SMPTE 272M 14.3 in comments | Unit test proves any 100 consecutive NTSC frames sum to 147147 and any PAL frame returns 1764 |
| 2.2 | Extend `AudioTrackGenerator` pair state: when a pair is EFM-selected, construct an additional left/right `AudioSynthesizer` at 44 100 Hz, reconfigured at the same section-run boundaries (`BeginRun`) as the 48 kHz pair | For identical `AudioParameters`, 44.1 kHz synthesis is deterministic across runs and independent of thread count |
| 2.3 | Add 24-bit → 16-bit sample conversion (round to nearest, saturate) used by the EFM path | Unit tests cover rounding, symmetry, and ±full-scale saturation |

**Validation:**
- WAV output remains byte-identical for existing projects (48 kHz path untouched).

---

## Phase 3: EFM Module Scaffolding and CIRC Encoder

**Dependencies:** None (module is standalone; parallel to Phases 1–2)

**Purpose:** Create the `videosynth_efm` library and implement CIRC
error-correction encoding.

| ID | Task | Acceptance criteria |
|----|------|---------------------|
| 3.1 | Create the `videosynth_efm` static library target in `CMakeLists.txt` with `src/efm/` and `include/videosynth/efm/` layout, namespace `videosynth::efm`; link it into `videosynth_core`. The module must not include any `videosynth_core` header | Builds stand-alone; a dependency check (no `#include <videosynth/...>` outside `videosynth/efm/...` within the module) holds |
| 3.2 | Implement `audio_frame_assembler`: six 16-bit stereo samples → one 24-byte frame, WmA = upper 8 bits / WmB = lower 8 bits per IEC 60908-1999, 16.2 | Unit tests verify byte order against hand-computed frames |
| 3.3 | Implement `circ_encoder`: GF(2⁸) arithmetic (P(x) = x⁸+x⁴+x³+x²+1, α = 00000010 per IEC 60908-1999, 16.2), C2 (28,24) and C1 (32,28) parity generation (16.3), and the delay/interleave structure per ECMA-130 Annex C: two-frame delay on one word group (C.3), 0–27 × 4-frame interleave (C.5), one-frame alternate delay (C.8), parity inversion and 3/108-frame min/max delays (C.9). Encoder exposes priming/flush so stream start and end are padded with digital silence frames | Unit tests: all-zero input yields the known all-zero-with-inverted-parity pattern; single-impulse input lands at the byte positions given by the ECMA-130 figure C.4 output-sequence table; output is a pure function of input |
| 3.4 | Define the module's error/reporting conventions (return values, no exceptions across the API boundary) consistent with core style, plus a `docs/design` note in this file kept current if the API shape changes | API documented in headers; clang-tidy clean |
| 3.5 | Implement the Timing Alignment Contract in the encoder: delay registers initialised to digital silence, symmetric end-of-stream flush, and the exported constant `kCircPipelineLatencyFrames = 108` (ECMA-130, C.9) | Round-trip unit test: a complementary de-interleaving decoder emits exactly 108 F1 frames (648 stereo samples) of warm-up silence, and decoded PCM after the warm-up is sample-exact against the input from sample 0 through the final input sample |

**Validation:**
- Module compiles and tests run with no dependency on core; suites registered
  as `unit` in `CMakeLists.txt`.

---

## Phase 4: Subcode Generation (Q-Mode 4)

**Dependencies:** Phase 3 (module scaffolding)

**Purpose:** Generate the P/Q subcode stream with the LaserDisc mode-4
modification, driven by a precomputed track table.

| ID | Task | Acceptance criteria |
|----|------|---------------------|
| 4.1 | Implement 98-frame subcode sectioning at 75 sections/s (IEC 60908-1999, 17.3) with S0/S1 sync patterns replacing the control bytes of frames 0 and 1 (out-of-table patterns S0 = 00100000000001, S1 = 00000000010010 per ECMA-130, 19.1) and zero-filled R–W channels (IEC 60908-1999, 17.6) | Section structure verified by unit test (98 frames, sync placement) |
| 4.2 | Define the module-side track table type (`{TNO, start, duration, area}` with areas lead-in / programme / lead-out) consumed by `subcode_generator`; TNO values are the caller-supplied sequence (assigned from section order by the core in Phase 6) | Track table drives all Q content; module performs no numbering decisions of its own |
| 4.3 | Implement programme-area Q channel mode 4 using the IEC 60908-1999, 17.5.1 audio-track layout with ADR = 0100 (IEC 60856 Amd 2 13.5.1 / IEC 60857 Amd 2 13.6.1): control = 0000, TNO from the track table (BCD), X = 01 within tracks and X = 00 with running time counting down during the track-1 pause, running time (MIN/SEC/FRAME) zeroed at each track start, absolute time (AMIN/ASEC/AFRAME) zeroed at programme-area start and continuous, FRAME/AFRAME running 00–74, ZERO field eight zero bits | Unit tests verify BCD rollover (74 → next second, 59 s → next minute), pause countdown reaching zero at the track-1 audio start, running-time reset at a track boundary, absolute-time continuity, and ADR/control nibbles |
| 4.4 | Implement lead-in TOC emission using the IEC 60908-1999, 17.5.1 lead-in layout (TNO = 00, MIN/SEC/FRAME running, no X field): one POINT entry per track with its PMIN/PSEC/PFRAME start position on the absolute time scale (±1 s), plus POINT A0 (PMIN = first TNO), A1 (PMIN = last TNO), and A2 (lead-out start), each item repeated three times per cycle (17.5.1), cycling for the whole lead-in; A0 P FRAME carries the video system identification 22 (PAL) or 12 (NTSC) per IEC 60856 Amd 2 13.5.2 / IEC 60857 Amd 2 13.6.2; the cycle may end on any POINT value at the end of the lead-in (13.5.2 / 13.6.2) | Unit tests verify the TOC entry set matches the track table, three-fold item repetition, cycling across the lead-in duration, and the PAL/NTSC identification values |
| 4.5 | Implement lead-out Q data (TNO = AA, X = 01, running time increasing from lead-out start, encoded as audio, per IEC 60908-1999, 17.5.1) and the P channel per IEC 60908-1999, 17.4: P = 0 in lead-in and within tracks; start flag P = 1 for the 2–3 s preceding each track start (coinciding with the track-1 pause; occupying the final 2 s of the preceding track otherwise); in the lead-out, P = 0 for 2–3 s then switching at 2 Hz ± 2 % (duty cycle 50 % ± 10 %); P changes only at subcode section boundaries, delayed one section relative to Q | Unit tests verify lead-out TNO/timing, start-flag placement and duration at every track boundary, and the lead-out 2 Hz P rhythm |
| 4.6 | Implement the Q CRC-16 on CONTROL + ADR + DATA-Q (P(X) = X¹⁶ + X¹² + X⁵ + 1, parity bits inverted on disc, MSB first) per IEC 60908-1999, 17.5 | CRC verified against independently computed Q-frame vectors; a corrupted bit fails the check |

**Validation:**
- Given a track table, every emitted Q frame is a pure function of the table
  and the section index; deterministic across runs.
- Absolute time is zero at programme-area start and advances 1/75 s per
  subcode section regardless of track boundaries.

---

## Phase 5: EFM Modulation and Stream Writer

**Dependencies:** Phases 3–4

**Purpose:** Serialise CIRC + subcode frames to the EFM channel stream and write
the T-value file.

| ID | Task | Acceptance criteria |
|----|------|---------------------|
| 5.1 | Implement the eight-to-fourteen code table (ECMA-130 Annex D; IEC 60908-1999 clause 13 figures 6/7) and the 588-channel-bit frame layout per IEC 60908-1999 clause 14 / ECMA-130 19.4: 24-bit sync header 100000000001000000000010 (NOT the truncated 22-digit rendering in the vendored ECMA-130 markdown — see Referenced Specifications), then subcode byte, 24 audio bytes, and 8 parity bytes at 14 channel bits each, every element followed by 3 merging bits (24 + 34 × 3 + 33 × 14 = 588) | Full 256-entry table validated against ECMA-130 Annex D (every entry: 14 bits, ≥2 and ≤10 zeros between ones); frame length exactly 588 channel bits in tests |
| 5.2 | Implement merging-bit selection per ECMA-130 Annex E (IEC 60908-1999 clause 13, annex A): candidate bits filtered by run-length rules (min 3, max 11 channel bits, including across symbol boundaries and sync), tie-broken by minimising absolute accumulated DSV | Property tests over long random payloads: no run < 3 or > 11 anywhere in the stream; |DSV| remains bounded |
| 5.3 | Implement channel-bit → T-value conversion and the `EfmStreamEncoder` facade (`Begin`/`PushSamples`/`Flush`), including CIRC priming/flush so all pushed samples are fully represented in the output and the Timing Alignment Contract holds end to end | Round-trip test: an independent minimal decoder (test-only helper) recovers sync positions every 588 bits and the original subcode and audio bytes, with the audio warm-up matching `kCircPipelineLatencyFrames` exactly |
| 5.4 | Implement `AudioEfmWriter` in `videosynth_core` mirroring the `AudioWavWriter` contract (`BeginWrite`/`AppendFrameAudio`/`FinalizeWrite`/`AbortWrite`), with path derivation `<basename>_audio_<pair>.efm` alongside `AudioWavWriter::DeriveAudioPath` | Path-derivation unit tests mirror the WAV equivalents; `AbortWrite` removes partial files |

**Validation:**
- Encoding a known tone yields a byte-identical `.efm` file across runs and
  platforms; T-value histogram spans only 3–11.

---

## Phase 6: Pipeline Integration and Functional Verification

**Dependencies:** Phases 1–5

**Purpose:** Wire EFM output into the generation pipeline and prove end-to-end
behaviour.

| ID | Task | Acceptance criteria |
|----|------|---------------------|
| 6.1 | Build the track table in `AudioTrackGenerator::Begin` from the resolved section layout: TNO assigned sequentially (01, 02, …) over `programme_area` sections in output order; `lead_in` / `lead_out` sections mapped to lead-in and lead-out areas; start times and durations computed from `EfmAudioSamplesForFrame` counts | Track table matches the section layout for PAL and NTSC fixtures, including projects without lead-in/lead-out sections |
| 6.2 | `AudioTrackGenerator`: when `output.efm_audio` selects a pair, drive the `AudioEfmWriter` from `Begin`/`EmitFrame`/`Finalize`/`Abort` using `EfmAudioSamplesForFrame` counts and the track table, alongside the existing WAV writer for the same pair | Both `.wav` and `.efm` are produced for the selected pair; other pairs unchanged; `Abort` leaves no partial `.efm` |
| 6.3 | Record the EFM output in the SQLite metadata sidecar written by `src/output_stage.cpp` (extend the `audio_channel_pair` table or add an `efm_audio` table with pair and path) | Sidecar row present with correct pair/path; schema change covered by output-stage tests |
| 6.4 | Functional tests: generate a short PAL project and a short NTSC project with EFM enabled, each with lead-in, multiple programme-area sections, and lead-out; assert `.efm` size matches the expected frame count, sync spacing every 588 channel bits, T-value range, Q-time progression, TNO incrementing at section boundaries, TOC entries matching the section layout, and lead-out TNO = AA. Decode the `.efm` with the test decoder and, after discarding the `kCircPipelineLatencyFrames` warm-up, verify sample-exact alignment (zero net offset) against a 44.1 kHz reference synthesis of the same `AudioParameters`, with the track-1 pause window silent. Register suites in `videosynth_functional_test_patterns` | Functional tests pass; unit-test lane unaffected |
| 6.5 | Add EFM audio to one PAL and one NTSC example project in `docs/examples/` (multi-section, so the examples demonstrate per-section tracks and TOC) | Examples validate and generate cleanly via `run-examples.sh` |

**Validation:**
- Multi-threaded runs (`threads > 1`) produce byte-identical `.efm` output to
  single-threaded runs (audio stays on the run thread in frame order).

---

## Phase 7: GUI and Documentation Alignment

**Dependencies:** Phase 6

**Purpose:** Expose EFM selection in the Qt GUI and keep design documentation
consistent (AGENTS.md §9.2 living-document rule).

| ID | Task | Acceptance criteria |
|----|------|---------------------|
| 7.1 | Add EFM controls to the project settings editor (`src/gui/project_settings_editor.cpp`): enable checkbox and pair selector (0–7), bound to `OutputTargets::efm_audio` | Editing round-trips through the working model; disabled state greys the selector |
| 7.2 | Surface validation feedback in the GUI (non-PAL/NTSC preset, undeclared pair warning) consistent with existing validator-message display | Messages appear where other output validation messages appear |
| 7.3 | GUI tests in `tests/gui/` for the new controls, registered per the existing GUI test patterns | GUI tests pass |
| 7.4 | Update `docs/design/high-level-design.md`: `output:` key documentation (§7), audio subsystem description, and a new EFM module section referencing this plan and the specifications above; update user docs/examples README | HLD matches implemented behaviour; links resolve |

**Validation:**
- A project authored entirely in the GUI with EFM enabled generates valid
  `.wav` + `.efm` output.

---

## Future Expansion (Enabled by the Module Boundary)

Not planned here; listed to justify the module's public API shape:

- EFM **decoder** in the same module (shares tables, CRC, CIRC structures) for
  verification and analysis tooling.
- Aligning TNO with `chapter_number` biphase codes (13.5.3 / 13.6.3) where a
  project's chapters differ from its section sequence — the track table type
  already carries caller-assigned TNO values, so only the Phase 6 numbering
  rule changes.
- Bilingual sound (P frame 23 PAL / 13 NTSC identification) and configurable
  control-nibble flags (copy bit, pre-emphasis).
- The 15.3 ms audio advance recommendation (13.3) as a configurable offset.
- RF-domain processing per clause 13.1 (low-pass/high-pass, pre-emphasis,
  symmetrical double-edge PWM onto the main carrier at −27 dB) consuming the
  module's channel-bit output.
