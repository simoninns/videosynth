# **VideoSynth**

**Technical Design Specification**

---

## **Table of Contents**

1. [Overview](#overview)
2. [Core Requirements](#core-requirements)
3. [Architecture Overview](#architecture-overview)
4. [Generation Stage](#generation-stage)
5. [Output Stage](#output-stage)
6. [PAL and NTSC Analogue Specifications](#pal-and-ntsc-analogue-specifications)
  - [6.1. Signal Levels](#61-signal-levels)
  - [6.2. Half-Line Element Definitions](#62-half-line-element-definitions)
  - [6.3. PAL Non-Visible Line Structures](#63-pal-non-visible-line-structures)
  - [6.4. NTSC Non-Visible Line Structures](#64-ntsc-non-visible-line-structures)
7. [YAML Project File Specification](#yaml-project-file-specification)
8. [Section Types](#section-types)
  - [8.1. Frame-Based Sections](#81-frame-based-sections)
  - [8.2. Line Injections](#82-line-injections)
9. [Field and Line Handling](#field-and-line-handling)
10. [4fsc Sampling and Subcarrier Locking](#4fsc-sampling-and-subcarrier-locking)
11. [VBI Line Allocation](#vbi-line-allocation)
12. [Implementation Pipeline](#implementation-pipeline)
13. [Error Handling and Validation](#error-handling-and-validation)
14. [CLI Interface](#cli-interface)
15. [Build and Packaging](#build-and-packaging)
16. [Directory Structure](#directory-structure)
17. [Future Requirements](#future-requirements)
18. [Appendix: References](#appendix-references)

---

---

## **1. Overview**

### **Specification Cross-Check (Section 1)**

- This section is a product-summary section; normative technical detail is expanded and clause-traced in Sections 2-13.
- PAL/NTSC studio-signal scope aligns with [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md) (Scope and Part A/Part B) and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §1.1.
- Laserdisc feature scope aligns with [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §9.1/§10 and [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §9.1/§10.
- VITC scope aligns with [IEC 60461:2010](../analogue-video-specifications/docs/video_metadata/IEC-60461-2010-Time-and-control-code/IEC-60461-2010-Time-and-control-code.md) Clause 9 (vertical interval application).

### **Purpose**

**VideoSynth** is a **C++17 application** designed to generate **PAL or NTSC video signals** in the **CVBS file format**. It adheres closely to the **analogue video standards** and is built for **video engineers and media preservation professionals**.

### **Key Features**

- **Time-based signal generation** for PAL and NTSC, closely following analogue standards.
- Support for multiple sample rates: **4fsc**, **20MSPS**, **40MSPS**, or custom.
- **Frame-based content** from:
  - Software-generated test patterns (e.g., colour bars, grayscale ramps).
  - Progressive sources (e.g., MOV, MP4, PNG, RAW files).
- **Line-based injections** for VBI content:
  - **VITS** (Vertical Interval Test Signals).
  - **Laserdisc biphase encoding** (IEC 60856/60857).
  - **VITC** (Vertical Interval Timecode, SMPTE 12M).

### **Target Users**

- Video engineers.
- Video media preservation developers and engineers.

### **Implementation Details**

- **Language**: C++17.
- **Build System**: Nix.
- **Logging**: [spdlog](https://github.com/gabime/spdlog).
- **Sample Rate Conversion**: [soxr](https://sourceforge.net/p/soxr/).
- **Testing**: [Google Test](https://github.com/google/googletest).

---

---

## **2. Core Requirements**

### **Specification Cross-Check (Section 2)**

- `525`/`625` lines per frame and nominal field rates are sourced from [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Annex 1, Table 1 (items 1-2), and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §1.1 and §11.3.
- NTSC subcarrier `3.579545 MHz` is sourced from [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §11.1 and [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Table 2 item 2.11(a).
- PAL subcarrier `4.43361875 MHz` is sourced from [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Table 2 item 2.11(a).
- `4fsc` sample-rate values are sourced from [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) §3.1/§3.4 (NTSC, 14.31818 MHz) and [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md) §1.1.1 (PAL, 17.734475 MHz).

### **Supported Standards**

All timing, signal levels, and encoding parameters for **PAL** and **NTSC** are defined in the following specifications:

- [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md): Conventional Television Systems.
- [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md): Composite Video Signal Characteristics.
- [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md): Composite Analog Video Signal for NTSC.
- [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md): Bit-Parallel Digital Interface for NTSC.
- [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md): 625-Line Digital PAL Interfaces.

### **PAL/NTSC Variant Scope (Normative for this Specification)**

To avoid ambiguity, this specification uses **PAL** and **NTSC** with the following fixed meanings:

- **PAL** means the **625/50 PAL family with 4.43361875 MHz subcarrier** (PAL-B/G/H/I timing family in baseband composite terms).
- **NTSC** means **NTSC-M 525/59.94 with 3.579545 MHz subcarrier** as defined by SMPTE 170M.

The following are **out of scope** for this specification unless explicitly added in a future revision:

- PAL-M, PAL-N, PAL-60, NTSC-J, NTSC 4.43, SECAM, and other non-625/50-PAL or non-NTSC-M variants.
- RF transmission/channel-plan differences (for example PAL-I RF sound spacing), because this document defines **baseband CVBS generation** rather than broadcast RF modulation.

Practical interpretation:

- If a user asks for "PAL-I", VideoSynth uses the same baseband 625/50 PAL composite timing and levels defined here; PAL-I-specific RF-layer differences are not modeled.


| **Standard** | **Accepted Frame-Based Source Dimensions** | **Frame Rate** | **Field Rate** | **Colour Subcarrier** | **Lines/Frame** | **Reference**   |
| ------------ | ------------------------------------------- | -------------- | -------------- | --------------------- | --------------- | --------------- |
| PAL (625/50 family) | 720x576 or 704x576 | 25 fps         | 50 Hz          | 4.43361875 MHz        | 625             | ITU-R BT.470-6  |
| NTSC-M (525/59.94)  | 720x480 or 704x480 | ~29.97 fps     | ~59.94 Hz      | 3.579545 MHz          | 525             | SMPTE 170M-2004 |

For file-based frame sources, both 720-wide and 704-wide rasters are valid for each standard. 720-wide rasters are common in modern file formats and often include side samples that fall outside the intended visible picture. 704-wide rasters are treated as active-picture-aligned inputs and are normalized to the internal 720-wide working raster during ingestion.


### **Output Modes**

- **Locked**: Timing derived from a reference clock.
- **Unlocked**: Free-running timing.

### **Sample Rates**


| **Sample Rate** | **PAL (Hz)** | **NTSC (Hz)** | **Notes**                        |
| --------------- | ------------ | ------------- | -------------------------------- |
| 4fsc            | 17,734,475   | 14,318,180    | Locked to subcarrier (optional). |
| 20MSPS          | 20,000,000   | 20,000,000    | Oversampling.                    |
| 40MSPS          | 40,000,000   | 40,000,000    | Oversampling.                    |
| Custom          | User-defined | User-defined  | Must be > 4fsc.                  |

At `4fsc`, NTSC frame cadence is orthogonal (`910 x 525 = 477,750` samples/frame). PAL is non-orthogonal at `4fsc` and uses `709,379` samples/frame via a distributed one-sample slip pattern (`1135` nominal samples/line with four lines per frame at `1134`).


---

---

## **3. Architecture Overview**

### **Specification Cross-Check (Section 3)**

- The two-stage split (time-domain generation then sampled digital output) is an implementation architecture; it is consistent with composite-signal decomposition in [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §3, §7-§10 and [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md) Part A/Part B.
- Independent luma/chroma generation with later composition maps to luminance/chrominance model definitions in [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §6-§10 and [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Table 2 item 2.5.

VideoSynth follows a **two-stage architecture** to ensure **separation of concerns** and **flexibility**:

1. **[Generation Stage](#generation-stage)**: Generates **time-based representations** of **luma (Y)** and **chroma (C)** signals.
2. **[Output Stage](#output-stage)**: Handles **sampling, combining, and formatting** into the final CVBS output.

### **Key Principles**

- **Independent Y and C Generation**: Luma and chroma are generated separately and combined only at the output stage.
- **Time-Based Generation**: Signals are generated using **time-based parameters** (e.g., line period, field rate).
- **Analogue Compliance**: Strict adherence to PAL/NTSC standards for timing, sync pulses, and colour encoding.

---

---

## **4. Generation Stage**

### **Specification Cross-Check (Section 4)**

- BT.601 source-domain assumptions (YCbCr construction/quantization and studio swing conventions) are grounded in [BT.601-5](../analogue-video-specifications/docs/video_formats/BT-601-5-1995/BT-601-5-1995.md) §3.5.1-§3.5.4 and Part A tables for 4:4:4 coding.
- NTSC colour encoding/filtering references are grounded in [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §6 (matrices), §7 (filtering), §8 (subcarrier modulation).
- PAL chroma sideband limits used for encoder bandwidth targets are grounded in [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Table 2 item 2.12.
- Sync/burst insertion basis is grounded in [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md), Table 2/Table 3 and Figures 1-9, plus [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §13.
- PAL pilot burst and NTSC VBI-burst behavior are grounded in [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §9.1.2 and [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §9.1.2.

### **Key Principle: Frame-Based Generation**

**All generation is frame-scoped.** A frame (two sequential fields) is the **smallest unit of output** from the generation stage.

However, **line timing and source-row addressing inside that frame are field-aware by definition**. This is required to keep interlaced field sequence and active-picture placement compliant with the NTSC/PAL timing models.

For avoidance of doubt, the generator shall apply the following rules:

- **NTSC active-picture line starts (1-indexed frame lines):** field 1 starts at line **22**; field 2 starts at line **284**. Line 21 (field-1 transition) and line 283 (field-2 transition) are not treated as full active-picture lines.
- **PAL active-picture line starts (1-indexed frame lines):** field 1 starts at line **23**; field 2 starts at line **335**.
- **Progressive-to-interlaced row mapping:** for a field-local active line index $n$ (starting at 0), field 1 samples source row $2n$ and field 2 samples source row $2n+1$.

Frame-source visible aperture contract:

- Progressive file sources accept either `720x576` or `704x576` for PAL, and either `720x480` or `704x480` for NTSC.
- The internal frame-source working raster remains fixed at `720x576` for PAL and `720x480` for NTSC.
- When a progressive source is `704` pixels wide, ingestion must center it in the `720`-sample raster and add `8` nominal-black pixels on the left and `8` nominal-black pixels on the right before field mapping.
- If a decoded source reports coded-frame padding (for example, codec-mandated macroblock or even-pixel constraints), ingestion must apply container/codec crop metadata first, then enforce the 704/720 normalization rules above.
- Only the visible active aperture may carry picture content. Pixels outside that aperture must remain nominal black and must not be modified by test-pattern generation or progressive-source ingestion.
- PAL aperture derivation:
  - ITU-R BT.1700 Table 1 item `1a`: `576` active lines.
  - ITU-R BT.1700 Table 2: `64.0 us` line period and `12.0 us` line blanking, giving `52.0 us` analogue active line duration.
  - Under the shared ITU-R BT.601 `13.5 MHz` digital sampling model, `52.0 us * 13.5 MHz = 702` visible pixels.
  - The `720`-sample frame-source raster therefore has `18` non-visible horizontal samples, which split evenly into `9` samples of left margin and `9` samples of right margin.
  - The PAL frame-source visible aperture is therefore a centered `702x576` region at `x=9..710`, `y=0..575`.
- NTSC aperture derivation:
  - SMPTE 170M-2004 analogue timing yields an active picture interval of approximately `52.666 us`.
  - Under the shared ITU-R BT.601 `13.5 MHz` digital sampling model, `52.666 us * 13.5 MHz = 711` visible pixels.
  - The `720`-sample frame-source raster therefore has `9` non-visible horizontal samples. Because that remainder is odd, the nearest centered integer placement is `4` samples of left margin and `5` samples of right margin.
  - The NTSC frame-source visible aperture is therefore a near-centered `711x480` region at `x=4..714`, `y=0..479`.
  - The analogue standard defines `483` active lines, but VideoSynth frame-based sources expose the `480` full active lines used by the timing model; the three partly active transition lines are not addressable through the frame-source raster.

The progressive source ingestion stage remains responsible for decoding and colour-space normalisation, but the generation stage is responsible for preserving the correct field sequence geometry when mapping normalised frame data into line-timed CVBS-domain Y/C waveforms.

---

### **Source Colour Space Requirement**

**All frame-based sources — test patterns and progressive sources alike — must provide pixel data to the chroma encoder in the following representation:**

> **10-bit 4:4:4 YCbCr, BT.601 studio swing**
> - Y: 64–940 (black–white)
> - Cb, Cr: 64–960 (midpoint 512)

This is a hard interface contract. The chroma encoder assumes this representation unconditionally and does not perform any colour space conversion internally. The rationale for 4:4:4 (rather than 4:2:2) is that the encoder must apply its own standard-compliant bandlimiting filters to the chroma channels before quadrature modulation. In the current implementation, PAL applies a symmetric low-pass filter of approximately 1.3 MHz on U and V before modulation, and NTSC applies a symmetric low-pass filter of approximately 1.2 MHz on Cb and Cr before modulation. Receiving pre-subsampled chroma would prevent deterministic filtering at the encoder boundary and would introduce chroma siting ambiguity that is incompatible with subcarrier-locked sampling at 4fsc.

### **PAL Chroma Encoding Model**

The PAL chroma path in this specification is normative for the implementation and follows [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md), Annex 1 Part B, Table 1 items 10d/10f/10h and Figure 8, plus [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md) §1.1.1/§1.2.

- PAL active chroma is synthesized as:

$$
E_M' = E_Y' + E_U'\sin(\omega t) + E_V'\cos(\omega t)
$$

- The PAL V-switch is applied by inverting the V axis on lines where the burst polarity sequence requires negative burst phase. This is sequence-aware and is not modeled as a simple global odd/even toggle.
- PAL burst phase uses the BT.1700 sequence map (I/II/III/IV across colour fields) with nominal burst phases of $+135^\circ$ and $-135^\circ$ per Table 1 item 10f.
- PAL burst suppression in the vertical interval follows BT.1700 Figure 8 (line windows for I/II/III/IV sequences).
- PAL subcarrier phase progression is continuous across the full frame sample timeline (non-orthogonal 4fsc lattice per EBU Tech. 3280-E), and active-picture chroma uses the same sequence model as burst.

### **NTSC Chroma Encoding Model**

The NTSC chroma path in this specification is normative for the implementation and follows [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §8/§10/§13 together with [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) §3.1/§4.1.1/§4.1.2.

- NTSC active chroma is synthesized as:

$$
E_M' = E_Y' + E_{Cb}'\sin(\theta) + E_{Cr}'\cos(\theta)
$$

- In the implementation, the active-picture NTSC phase reference uses burst plus $180^\circ$ (SMPTE 170M §10 convention), i.e.:

$$
	heta = \omega t + \varphi_{\text{burst,line}} + \varphi_{\text{frame}} + \pi
$$

- Line-to-line NTSC burst phase alternates at 4fsc because 910 samples/line corresponds to a $\pi$ radian subcarrier phase advance per line (SMPTE 244M §4.1.1 interpretation in this model).
- The NTSC colour sequence is modeled as a 2-frame SC-H progression using a frame phase offset of $0$ then $\pi$, repeating (SMPTE 170M field/frame color-sequence behavior).
- In baseline mode, burst is present on horizontal lines and suppressed on equalizing/broad-sync lines; when NTSC laserdisc VBI burst mode is enabled, burst is additionally inserted on equalizing and broad-sync pulses per [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §9.1.2.

**Test patterns** naturally produce values in this space directly.

**Progressive sources** (MOV, MP4, PNG, RAW, etc.) are converted during ingestion by `progressive_source`. Any source colour space — RGB, YUV 4:2:0, YUV 4:2:2, or other — must be converted and upsampled to 10-bit 4:4:4 YCbCr BT.601 studio swing **before** the frame data is passed to the generator. The precise conversion path depends on the source's declared colour primaries, transfer characteristics, and matrix coefficients; these must be read from the source container metadata where available and applied correctly.

---

### **Responsibilities**


| **Responsibility**              | **Description**                                                                          | **Reference**                   |
| ------------------------------- | ---------------------------------------------------------------------------------------- | ------------------------------- |
| **Luma (Y) Generation**         | Generate luma component per frame, including sync pulses and blanking.                   | ITU-R BT.470-6, ITU-R BT.1700   |
| **Chroma (C) Generation**       | Generate chroma component per frame, including colour burst.                             | ITU-R BT.470-6, ITU-R BT.1700   |
| **Progressive Source Ingestion**| Decode progressive frames, convert to 10-bit 4:4:4 YCbCr BT.601 studio swing, split into interlaced fields. | ITU-R BT.470-6, ITU-R BT.601   |
| **Sync and Burst Insertion**    | Insert HSync, VSync, equalizing pulses, and colour burst.                                | ITU-R BT.470-6, ITU-R BT.1700   |
| **PAL Laserdisc Pilot Burst**   | Superimpose a 3.75 MHz pilot burst on all sync pulses when enabled (PAL only).           | IEC 60856 §9.1.2                |
| **NTSC Laserdisc VBI Burst**    | Insert colour burst on equalizing and broad sync pulses when enabled (NTSC only).        | IEC 60857 §9.1.2                |
| **Line Injections**             | Inject VITS, Laserdisc biphase, VITC, or custom line content into VBI.                  | IEC 60856, IEC 60857, SMPTE 12M |
| **Ramping and Smoothing**       | Apply ramping to transitions to simulate analogue behavior.                              | ITU-R BT.470-6, ITU-R BT.1700   |


### **Inputs**

- Frame-based content in **10-bit 4:4:4 YCbCr BT.601 studio swing** (test patterns or normalised progressive sources).
- Line-based injections (VITS, Laserdisc biphase, VITC).
- CVBS presets (video_standard_preset, sample_encoding_preset, signal_state_preset, mode).

### **Outputs**

- Time-based **luma (Y)** and **chroma (C)** signals as **`double`-precision floating-point mV values**, one complete frame at a time. Values are relative to blanking (0.0 mV) and are not yet quantised to integer.
- Metadata (field order, dominance, timing).

---

---

## **5. Output Stage**

### **Specification Cross-Check (Section 5)**

- 10-bit quantization and legal/protected code-space behavior are grounded in [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md) §1.1.1/§1.1.2 and [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) §3.3, §4.2.4, Table 1.
- NTSC 4fsc sample-phase alignment and clock/subcarrier relationship are grounded in [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) §3.1 and §4.1.2.
- PAL 4fsc sampling rate and interface assumptions are grounded in [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md) §1.1.1 and §2.4.

### **Responsibilities**


| **Responsibility**      | **Description**                                                         | **Reference**                     |
| ----------------------- | ----------------------------------------------------------------------- | --------------------------------- |
| **Sampling**            | Sample Y and C signals at the specified rate (e.g., 4fsc, 20MSPS).      | CVBS File Format Specification    |
| **mV to Integer Conversion** | Quantise `double` mV values to 10-bit integers per EBU 3280 (PAL) or SMPTE 244M (NTSC). | EBU Tech. 3280-E, SMPTE 244M-2003 |
| **Combining Y and C**   | Combine quantised luma and chroma into composite signal (CVBS).         | CVBS File Format Specification    |
| **Output Formatting**   | Format the sampled signal into the output files (video and metadata).   | CVBS File Format Specification    |
| **Subcarrier Locking**  | Lock sample clock to colour subcarrier for 4fsc using NCO.              | SMPTE 244M-2003, EBU Tech. 3280-E |
| **Metadata Generation** | Generate CVBS file metadata (magic number, version, sample rate, etc.). | CVBS File Format Specification    |


### **CVBS File Output**

The output stage generates **two files** as per the [CVBS File Format Specification](../cvbs-file-format-specification/docs/index.md):

1. **Video File**: Raw samples of the composite signal (Y + C + sync).
2. **Metadata File**: Header metadata (magic number, version, video standard preset, sample encoding preset, signal state preset, resolution, etc.).

### **Inputs**

- Time-based Y and C signals from the generation stage.
- CVBS presets (sample rate, subcarrier lock, endianness).

### **Outputs**

- **Video File**: Raw samples of the composite signal.
- **Metadata File**: Header metadata.

---

---

## **6. PAL and NTSC Analogue Specifications**

### **Specification Cross-Check (Section 6)**

- PAL/NTSC signal level tables are grounded in [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md), Table 2 (blanking/white/sync), and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §15.4 / Annex B.
- NTSC IRE relationships are grounded in [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md), Annex B (IRE units) and §15.4.
- PAL pilot-burst headroom requirement is grounded in [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §9.1.2.
- 10-bit code anchors and excluded/reserved code ranges are grounded in [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md) §1.1.1/§1.1.2 and [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) §4.2.4, Table 1.
- Equalizing and broad sync pulse durations are grounded in [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md), Table 3 and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §13.3.
- PAL/NTSC non-visible line structures are grounded in [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Annex 1 Tables 1-1/1-2, [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md), and laserdisc overlays in [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §9.1.3-§9.1.4 and [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §9.1.3-§9.1.5.

All specifications for **PAL** and **NTSC** are explicitly referenced from the [Analogue Video Specifications](../analogue-video-specifications/docs/index.md) repository.

---

### **6.1. Signal Levels**

All signal levels within VideoSynth are expressed in **millivolts (mV)**, measured relative to **blanking level (0 mV)**. IRE units are used in NTSC engineering documentation but are **not used internally by the generator**; they are defined here solely for reference when reading NTSC standards.

#### **PAL Signal Levels**

PAL uses mV natively. All PAL standard documents and the VITS definitions in this repository use mV.

| **Level**       | **mV (relative to blanking)** | **Description**                     |
| --------------- | ----------------------------- | ----------------------------------- |
| Sync tip        | −300 mV                       | Bottom of sync pulse.               |
| Blanking        | 0 mV                          | Reference level for active video.   |
| Black           | 0 mV                          | Black level (same as blanking, PAL).|
| White           | 700 mV                        | 100% white (luminance peak).        |
| Composite peak  | 933 mV                        | Approximate peak including chroma.  |

The full PAL composite signal swings from −300 mV (sync tip) to approximately +933 mV (peak chroma), for a total composite amplitude of ~1233 mV. The **luminance range is 0–700 mV**.

#### **NTSC Signal Levels and IRE Conversion**

NTSC documentation traditionally uses **IRE units** (Institute of Radio Engineers). The generator does not use IRE internally; all NTSC levels are converted to mV for processing.

The IRE scale is defined relative to blanking:

| **Level**       | **IRE** | **mV (relative to blanking)** | **Description**                      |
| --------------- | ------- | ----------------------------- | ------------------------------------ |
| Sync tip        | −40 IRE | −285.7 mV                     | Bottom of sync pulse.                |
| Blanking        | 0 IRE   | 0 mV                          | Reference level for active video.    |
| Black           | 7.5 IRE | 53.6 mV                       | Set-up (pedestal), used in NTSC USA. |
| White           | 100 IRE | 714.3 mV                      | 100% white (luminance peak).         |

**Conversion formula** (IRE ↔ mV, both relative to blanking):

$$\text{mV} = \text{IRE} \times 7.143$$
$$\text{IRE} = \frac{\text{mV}}{7.143}$$

This derives from the NTSC definition that 100 IRE = 714.3 mV (the luminance range from blanking to white).

> **Important**: PAL white (700 mV) and NTSC white (100 IRE = 714.3 mV) are **not the same level**. They must not be used interchangeably. The generator applies the correct levels for each standard independently.

#### **Summary Comparison**

| **Level**   | **PAL (mV)** | **NTSC (mV)** | **NTSC (IRE)** |
| ----------- | ------------ | ------------- | -------------- |
| Sync tip    | −300 mV      | −285.7 mV     | −40 IRE        |
| Blanking    | 0 mV         | 0 mV          | 0 IRE          |
| Black       | 0 mV         | 53.6 mV       | 7.5 IRE        |
| White       | 700 mV       | 714.3 mV      | 100 IRE        |

---

#### **Internal Floating-Point Representation**

The generator uses **`double` (64-bit IEEE 754 floating-point)** to represent all signal levels in mV. Accuracy is prioritised over speed throughout the generation stage. Quantisation to integer sample values happens **once**, at the output stage boundary, immediately before writing to the CVBS file.

**Why floating-point is required:**  
Certain signal additions push levels well outside the nominal PAL or NTSC signal range. The most significant case is the **PAL Laserdisc pilot burst** ([IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §9.1.2), which is superimposed on the sync pulse at 3.75 MHz with a peak-to-peak amplitude of $\frac{6}{7} \times 700\ \text{mV} = 600\ \text{mV p-p}$. Centred on sync tip level (−300 mV), this burst swings between **−600 mV and 0 mV** — 300 mV below the standard sync tip. Integer arithmetic over a fixed 10-bit range could not represent this without clipping or bias errors.

**Required internal headroom:**

| **Boundary**       | **Minimum range required** | **Driven by**                                          |
| ------------------ | -------------------------- | ------------------------------------------------------ |
| Negative (below blanking) | ≤ −600 mV           | PAL Laserdisc pilot burst (IEC 60856 §9.1.2)           |
| Positive (above blanking) | ≥ +1000 mV          | Peak chroma excursions on 100% colour bars (~933 mV) plus margin |

`double` provides far more range than needed (exponent range of ±10³⁰⁸), so no clamping or range check is required in the generation stage. All signal components — sync, burst, active video, line injections — are additively composed in floating-point mV before any quantisation occurs.

**Output stage quantisation (float mV → 10-bit integer):**

The output stage performs a single linear mapping from the internal `double` mV value to a 10-bit integer code, using the normative level tables from the relevant digital interface standard. Values outside the legal code range are clamped to the standard's maximum legal value before writing.

*PAL — EBU Tech. 3280-E (10-bit):*

$$\text{code} = \operatorname{round}\!\left(\frac{\text{mV}}{1.1905} + 256\right)$$

Normative anchors: sync tip −300 mV → code 4; blanking 0 mV → code 256; white 700 mV → code 844. Legal range: codes 4–1019.

*NTSC — SMPTE 244M-2003 (10-bit):*

$$\text{code} = \operatorname{round}\!\left(\frac{\text{mV}}{1.2755} + 240\right)$$

Normative anchors: sync tip −285.7 mV → code 16; blanking 0 mV → code 240; white 714.3 mV → code 800. Legal range: codes 16–1019.

Codes 0–3 and 1020–1023 are reserved (excluded values) per both standards and must never appear in output.

---

### **6.2. Half-Line Element Definitions**

Each **line** in the vertical blanking interval (VBI) is composed of **two half-line elements**. The following symbols represent the **sync and blanking pulses**:


| **Symbol** | **Description**     | **PAL Duration**  | **NTSC Duration** | **Reference**                   |
| ---------- | ------------------- | ----------------- | ----------------- | ------------------------------- |
| **BL**     | Blanking Level      | 0.3V              | 0.3V              | ITU-R BT.1700                   |
| **SY**     | Sync Level          | 0.0V              | 0.0V              | ITU-R BT.1700                   |
| **EQ**     | Equalizing Pulse    | 2.3 µs (1/4 line) | 2.3 µs (1/4 line) | ITU-R BT.470-6, SMPTE 170M-2004 |
| **VS**     | Vertical Sync Pulse | 27.3 µs (2.5H)    | 31.778 µs (3H)    | ITU-R BT.470-6, SMPTE 170M-2004 |


**Notes**:

- **EQ**: Equalizing pulses are **2.3 µs** wide (1/4 line).
- **VS**: Vertical sync pulses are **2.5H (PAL)** or **3H (NTSC)** wide.

---

### **6.3. PAL Non-Visible Line Structures**

For **PAL (625-line system)**, the vertical blanking interval (VBI) is defined as follows, with **each line consisting of two half-line elements**:

#### **Field 1 (Odd Field)**


| **Line** | **Half-Line Elements** | **Notes**                                          |
| -------- | ---------------------- | -------------------------------------------------- |
| 1        | EQ, EQ                 | Pre-equalizing pulses (5 pulses total, 2.5H).      |
| 2        | EQ, EQ                 | Pre-equalizing pulses.                             |
| 3        | EQ, EQ                 | Pre-equalizing pulses.                             |
| 4        | EQ, EQ                 | Pre-equalizing pulses.                             |
| 5        | EQ, EQ                 | Pre-equalizing pulses.                             |
| 6        | VS, VS                 | Vertical sync pulse (start).                       |
| 7        | VS, VS                 | Vertical sync pulse.                               |
| 8        | VS, VS                 | Vertical sync pulse.                               |
| 9        | VS, VS                 | Vertical sync pulse.                               |
| 10       | VS, VS                 | Vertical sync pulse (end).                         |
| 11       | EQ, EQ                 | Post-equalizing pulses (5 pulses total, 2.5H).     |
| 12       | EQ, EQ                 | Post-equalizing pulses.                            |
| 13       | EQ, EQ                 | Post-equalizing pulses.                            |
| 14       | EQ, EQ                 | Post-equalizing pulses.                            |
| 15       | EQ, EQ                 | Post-equalizing pulses.                            |
| 16-18    | BL, BL                 | VBI (Laserdisc biphase: programme status on 16, picture stop/chapter on 17, chapter/time code on 18). |
| 19-21    | BL, BL                 | VBI (VITS on 19/20; VITC on 19–21; subtitle on 20–21).    |
| 22       | BL, BL                 | VBI (CEA-608; blanked when Laserdisc active).              |
| 23-620   | Active Video           | Visible content.                                   |
| 621      | EQ, EQ                 | Pre-equalizing pulses (5 pulses total, 2.5H).      |
| 622      | EQ, EQ                 | Pre-equalizing pulses.                             |
| 623      | VS, VS                 | Vertical sync pulse (start).                       |
| 624      | VS, VS                 | Vertical sync pulse.                               |
| 625      | VS, VS                 | Vertical sync pulse (end).                         |


#### **Field 2 (Even Field)**


| **Line** | **Half-Line Elements** | **Notes**                                      |
| -------- | ---------------------- | ---------------------------------------------- |
| 311      | EQ, EQ                 | Pre-equalizing pulses.                         |
| 312      | EQ, EQ                 | Pre-equalizing pulses.                         |
| 313      | EQ, VS                 | Transition into broad-sync block (mixed half-line). |
| 314      | VS, VS                 | Vertical sync pulse.                           |
| 315      | VS, VS                 | Vertical sync pulse (end).                     |
| 316      | EQ, EQ                 | Post-equalizing pulses.                        |
| 317      | EQ, EQ                 | Post-equalizing pulses.                        |
| 318      | EQ, BL                 | Transition out of sync block (mixed half-line).|
| 319-334  | BL, BL                 | VBI lines.                                     |
| 335-622  | Active Video           | Visible content.                               |
| 623-625  | EQ, EQ                 | Pre-equalizing pulses for next frame boundary. |


---

### **6.4. NTSC Non-Visible Line Structures**

For **NTSC (525-line system)**, the vertical blanking interval (VBI) is defined as follows, with **each line consisting of two half-line elements**:

#### **Field 1 (Odd Field)**


| **Line** | **Half-Line Elements** | **Notes**                                          |
| -------- | ---------------------- | -------------------------------------------------- |
| 1        | EQ, EQ                 | Pre-equalizing pulses (6 pulses total, 3H).        |
| 2        | EQ, EQ                 | Pre-equalizing pulses.                             |
| 3        | EQ, EQ                 | Pre-equalizing pulses.                             |
| 4        | VS, VS                 | Vertical sync pulse (start).                       |
| 5        | VS, VS                 | Vertical sync pulse.                               |
| 6        | VS, VS                 | Vertical sync pulse (end).                         |
| 7        | EQ, EQ                 | Post-equalizing pulses (6 pulses total, 3H).       |
| 8        | EQ, EQ                 | Post-equalizing pulses.                            |
| 9        | EQ, EQ                 | Post-equalizing pulses.                            |
| 10-11    | BL, BL                 | VBI (Laserdisc 40-bit FM coded signal reserved: FM data on 10, white flag on 11). |
| 12-15    | BL, BL                 | VBI (Laserdisc biphase reserved; reserved for future applications).               |
| 16       | BL, BL                 | VBI (Laserdisc biphase: programme status / CLV picture number / users code).      |
| 17-18    | BL, BL                 | VBI (Laserdisc biphase: picture number / chapter / programme time code on 17–18). |
| 19       | BL, BL                 | VBI (VIR signal; mandatory on NTSC laserdisc per IEC 60857 §9.1.3).              |
| 20       | BL, BL                 | VBI (VITS composite test signal; recommended per IEC 60857 §9.1.4).              |
| 21       | BL, BL                 | VBI (CEA-608 / closed caption).                                                   |
| 22-262   | Active Video           | Visible content in field 1 through the field boundary lead-in. |
| 263      | SY, EQ                 | Field-transition lead-in (mixed half-line; horizontal-sync then equalizing). |
| 264-265  | EQ, EQ                 | Pre-equalizing pulses.                             |
| 266      | EQ, VS                 | Transition into broad-sync block (mixed half-line). |
| 267-268  | VS, VS                 | Vertical sync pulses.                              |
| 269      | VS, EQ                 | Transition out of broad-sync block (mixed half-line). |
| 270-271  | EQ, EQ                 | Post-equalizing pulses.                            |
| 272      | EQ, BL                 | Final transition to blanking (mixed half-line).    |


#### **Field 2 (Even Field)**


| **Line** | **Half-Line Elements** | **Notes**                                    |
| -------- | ---------------------- | -------------------------------------------- |
| 263      | SY, EQ                 | Field-transition lead-in (mixed half-line; horizontal-sync then equalizing).  |
| 264-265  | EQ, EQ                 | Pre-equalizing pulses.                       |
| 266      | EQ, VS                 | Transition into broad-sync block (mixed half-line). |
| 267-268  | VS, VS                 | Vertical sync pulses.                        |
| 269      | VS, EQ                 | Transition out of broad-sync block (mixed half-line). |
| 270-271  | EQ, EQ                 | Post-equalizing pulses.                      |
| 272      | EQ, BL                 | Final transition to blanking (mixed half-line). |
| 273-282  | BL, BL                 | VBI lines.                                   |
| 283      | BL, BL                 | Field-2 transition line; not treated as full active picture. |
| 284-520  | Active Video           | Visible content in field 2.                         |
| 521-525  | EQ/VS                  | Frame-end sync block; no active picture.            |


---

---

## **7. YAML Project File Specification**

### **Specification Cross-Check (Section 7)**

- Standard-dependent fixed line counts and timing families are grounded in [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Annex 1, Table 1 and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §1.1/§11.
- `signal_state_preset` locked-state constraints for 4fsc encoding are grounded in [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) §3.1 and [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md) §1.1.1.
- Laserdisc-specific YAML constraints are grounded in [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §9.1.4/§10.1 and [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §9.1.5/§10.1/§10.2.
- VITC structure, flags, CRC, and line-placement constraints are grounded in [IEC 60461:2010](../analogue-video-specifications/docs/video_metadata/IEC-60461-2010-Time-and-control-code/IEC-60461-2010-Time-and-control-code.md) §9.2.3, §9.2.5, §9.2.7, §9.6.2, §9.6.3.
- VITS type families are grounded in [PAL VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/PAL-VITS.md) and [NTSC VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/NTSC-VITS.md).

---

### **Top-Level Structure**

```yaml
project:
  name: "Example PAL CVBS Output"  # User-friendly name
  version: "1.0"                  # Project file version
  description: "A test output with colour bars and line injections"  # Optional

cvbs_presets:
  video_standard_preset: PAL     # PAL or NTSC (only one allowed per project)
  sample_encoding_preset: CVBS_U10_4FSC  # CVBS_U10_4FSC, CVBS_U16_4FSC, RAW_S16_28M, RAW_S16_40M, CVBS_TPG21_4FSC
  signal_state_preset: STANDARD_TBC_LOCKED  # STANDARD_TBC_LOCKED or other spec-defined signal-state presets
  mode: locked                   # locked or unlocked
  pal_laserdisc_pilot_burst: false  # PAL only; inject 3.75 MHz pilot burst on all sync pulses (IEC 60856 §9.1.2); default: false
  ntsc_laserdisc_vbi_burst: false   # NTSC only; insert colour burst on equalizing and broad sync pulses (IEC 60857 §9.1.2); default: false
  field_order: upper_first      # upper_first or lower_first (default: upper_first)
  field_dominance: field1       # field1 or field2 (default: field1 for PAL, field2 for NTSC)
  endianness: little            # little or big (default: little)

output:
  video_path: "out/pal_test_video.composite"
  metadata_path: "out/pal_test_metadata.meta"

sections:
  - name: "Colour Bars with VITS and Laserdisc"
    type: software_generated   # Frame-based content
    duration_frames: 10
    pattern: "pal_ebu_colour_bars_100"
    line_injections:           # Line-based injections for this section
      - type: vits
        target_lines: [10, 11, 12]
        vits_type: "virs"
      - type: laserdisc
        disc_type: CAV
        # No target_lines — line placement is fixed by IEC 60856 §10
        codes:
          - code_type: picture_number
            start_value: 1
          - code_type: picture_stop
          - code_type: chapter_number
            chapter: 5
          - code_type: programme_status
            programme_status: "0x8F0000"
```

---

### **File Path Resolution**

Any field in a project YAML that accepts a file path (e.g., `source` on a `progressive` section) resolves the path using the following priority order:

1. **Built-in sources** — paths prefixed with `builtin:` refer to source files bundled and installed with the application. The prefix is followed by the built-in asset name (e.g., `builtin:smpte_leader`). The application resolves these against its installation data directory at runtime, regardless of where the project YAML is located.
2. **Absolute paths** — paths beginning with `/` are used exactly as specified. The file must exist at that location on the host filesystem.
3. **Project-relative paths** — all other paths are resolved relative to the **directory containing the project YAML file**. This allows a project and its associated assets to be grouped together and moved as a unit.

The application must report a clear validation error if a referenced file cannot be found after applying these rules.

#### **Examples**

```yaml
source: "builtin:smpte_leader"        # built-in asset bundled with the application
source: "/media/archive/clip.mov"     # absolute path
source: "assets/clip.mov"             # relative to the project YAML directory
source: "../shared/clip.mov"          # relative path traversal is permitted
video_path: "out/pal_test_video.composite" # project-relative output file path
```

---

### **Validation Rules**

1. **Single CVBS Preset**:
  - Only one `video_standard_preset` (PAL or NTSC) per project.
  - Only one `sample_encoding_preset` per project.
  - Only one `signal_state_preset` per project.
  - `output.video_path` and `output.metadata_path` are required in the project YAML.
  - `output.video_path` and `output.metadata_path` must resolve to different paths.
  - Output resolution is fixed by the standard (720x576 for PAL, 720x480 for NTSC) and must not be specified in the project file.
  - 4fsc generation requires a 4fsc `sample_encoding_preset` and a locked `signal_state_preset`.
  - `pal_laserdisc_pilot_burst` can **only be enabled for PAL projects**. If enabled for NTSC, the YAML is considered **invalid**.
  - `ntsc_laserdisc_vbi_burst` can **only be enabled for NTSC projects**. If enabled for PAL, the YAML is considered **invalid**.
2. **Sections**:
  - Each section must have a valid `type` (`software_generated` or `progressive`).
  - Each section must have at least one of:
    - Frame-based content (`software_generated` or `progressive`).
    - Line injections (`line_injections`).
  - For `progressive` sections, `source` must point to a valid file.
3. **Line Injection Constraints**:
  - `target_lines` must be within the valid range for the standard (1-625 for PAL, 1-525 for NTSC).
  - `target_lines` must **not** be specified for `laserdisc` injection types; specifying it is a validation error.
  - **If injected lines overlap within a section, the YAML must fail validation.**
  - **When a `laserdisc` injection is active in a section, no other injection type may target lines within the laserdisc reserved ranges** (PAL: Field 1 lines 6–18, Field 2 lines 319–331; NTSC: Field 1 lines 10–18, Field 2 lines 273–281).
  - **A `vitc` injection and a `laserdisc` injection must not appear in the same section.** Laserdisc does not use VITC.
4. **Progressive Sources**:
  - `source` must resolve to an accessible file after applying [File Path Resolution](#file-path-resolution) rules. If the resolved path does not exist, the YAML is considered **invalid**.
  - Progressive source dimensions must be standard-consistent and one of:
    - PAL: `720x576` or `704x576`
    - NTSC: `720x480` or `704x480`
  - `704`-wide progressive sources are normalized to the internal `720`-wide raster with `8` pixels of nominal-black side padding on each side.
  - Decoder/container padding must be cropped to the declared display aperture before applying 704/720 normalization.
  - `duration_frames` must be either:
    - A positive integer (fixed number of frames).
    - `"all"` (use all available frames from the source).

---

---

## **8. Section Types**

### **Specification Cross-Check (Section 8)**

- Software-generated pattern naming is an implementation catalog; waveform/level validity for test insertions maps to [PAL VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/PAL-VITS.md), [NTSC VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/NTSC-VITS.md), and analogue level limits in [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md).
- Progressive-source frame-rate matching to output standards is grounded in [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Annex 1, Table 1 and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §11.3.
- Laserdisc code-type definitions and per-line placement are grounded in [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §10.1 and [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §10.1/§10.2, plus Amendment 2 updates for CLV picture-number behavior.
- VITC fields (`timecode`, flags, CRC) are grounded in [IEC 60461:2010](../analogue-video-specifications/docs/video_metadata/IEC-60461-2010-Time-and-control-code/IEC-60461-2010-Time-and-control-code.md) §9.2 and §9.5-§9.6.

---

### **8.1. Frame-Based Sections**

These define the **primary content** for a set of frames.

---

#### **Software-Generated Frames**

Generates static or dynamic test patterns. Each test pattern is **predefined** and has a **unique name**.

##### **Fields**


| **Field**         | **Type** | **Required** | **Description**                 | **Example**                                  |
| ----------------- | -------- | ------------ | ------------------------------- | -------------------------------------------- |
| `name`            | string   | Yes          | User-friendly name.             | `"Colour Bars"`                              |
| `type`            | string   | Yes          | Must be `"software_generated"`. | `"software_generated"`                       |
| `duration_frames` | integer  | Yes          | Number of frames to generate.   | `10`                                         |
| `pattern`         | string   | Yes          | Name of the test pattern.       | `"pal_ebu_colour_bars_100"` |
| `line_injections` | list     | No           | List of line-based injections.  | (See [Line Injections](#82-line-injections)) |


##### **Predefined Patterns**

Each pattern is **unique** and has no configurable options. Variations (e.g., 75% vs. 100% colour bars) are separate patterns.

Pattern names are split by output format:

- PAL projects use only `pal_*` pattern names.
- NTSC projects use only `ntsc_*` pattern names.

Authoritative pattern definitions (waveform, geometry, levels, and deterministic mapping rules) are specified in [Software-Generated Patterns](software-generated-patterns.md).


**PAL patterns**


| **Pattern Name**                              | **Description**                                     |
| --------------------------------------------- | --------------------------------------------------- |
| `pal_ebu_colour_bars_100`                    | PAL colour bars, 100% saturation variant.           |
| `pal_ebu_colour_bars_75`                     | PAL colour bars, 75% saturation variant.            |
| `pal_linear_grayscale_ramp_horizontal`       | Horizontal linear grayscale ramp (PAL geometry).    |
| `pal_linear_grayscale_ramp_vertical`         | Vertical linear grayscale ramp (PAL geometry).      |
| `pal_luma_checkerboard_8x8`                  | Luma-only checkerboard with 8x8 pixel tiles.        |
| `pal_luma_checkerboard_16x16`                | Luma-only checkerboard with 16x16 pixel tiles.      |
| `pal_full_field_black`                       | Full-field black level.                             |
| `pal_full_field_white`                       | Full-field white level.                             |
| `pal_pluge_5patch_near_black`                | PLUGE near-black strip (five-patch anchored layout) around PAL black reference.             |
| `pal_crosshatch_visible_area_grid`           | PAL crosshatch grid confined to the visible active aperture. |


**NTSC patterns**


| **Pattern Name**                              | **Description**                                     |
| --------------------------------------------- | --------------------------------------------------- |
| `ntsc_smpte_170m_colour_bars_100`            | NTSC SMPTE multi-region colour bars, 100% variant. |
| `ntsc_smpte_170m_colour_bars_75`             | NTSC SMPTE multi-region colour bars, 75% variant.  |
| `ntsc_linear_grayscale_ramp_horizontal`      | Horizontal linear grayscale ramp (NTSC geometry).   |
| `ntsc_linear_grayscale_ramp_vertical`        | Vertical linear grayscale ramp (NTSC geometry).     |
| `ntsc_luma_checkerboard_8x8`                 | Luma-only checkerboard with 8x8 pixel tiles.        |
| `ntsc_luma_checkerboard_16x16`               | Luma-only checkerboard with 16x16 pixel tiles.      |
| `ntsc_full_field_black`                      | Full-field black level.                             |
| `ntsc_full_field_white`                      | Full-field white level.                             |
| `ntsc_pluge_5patch_near_black`               | PLUGE near-black strip (five-patch anchored layout) around NTSC black reference.            |
| `ntsc_crosshatch_visible_area_grid`          | NTSC crosshatch grid confined to the visible active aperture. |


##### **Example**

```yaml
sections:
  - name: "PAL Colour Bars"
    type: software_generated
    duration_frames: 5
    pattern: "pal_ebu_colour_bars_100"
    line_injections:
      - type: vits
        target_lines: [10, 11, 12]
        vits_type: "virs"
```

---

#### **Progressive Sources**

Ingests progressive sources (MOV, MP4, PNG, RAW) and converts them to interlaced signals.

##### **Fields**


| **Field**         | **Type**       | **Required** | **Description**                                                                | **Example**          |
| ----------------- | -------------- | ------------ | ------------------------------------------------------------------------------ | -------------------- |
| `name`            | string         | Yes          | User-friendly name.                                                            | `"MOV Source"`       |
| `type`            | string         | Yes          | Must be `"progressive"`.                                                       | `"progressive"`      |
| `source`          | string         | Yes          | Path to the source file. May be a `builtin:` prefixed name, an absolute path, or a path relative to the project YAML. See [File Path Resolution](#file-path-resolution). | `"assets/test.mov"` |
| `start_frame`     | integer        | No           | First frame to use (default: `0`).                                             | `0`                  |
| `duration_frames` | integer/string | No           | Number of frames to extract. Use `"all"` for all frames or a positive integer. | `100` or `"all"`     |


##### **Colour Space and Frame Rate**

- **Colour Space**: Determined by the input source type (e.g., MOV files are typically YUV, PNG files are RGB).
- **Frame Rate**: **Fixed by the output standard** (25 fps for PAL, ~29.97 fps for NTSC). Input sources **must match** this frame rate.

##### **Accepted Dimensions and Padding Behavior**

- PAL progressive sources: `720x576` or `704x576`.
- NTSC progressive sources: `720x480` or `704x480`.
- `704`-wide sources are mapped to the internal `720`-wide raster by adding `8` nominal-black pixels on both left and right sides.
- For codecs/containers that store padded coded dimensions, the decoder must apply crop/display-aperture metadata first. Validation and normalization then operate on the cropped display frame.

##### **Example**

```yaml
sections:
  - name: "MOV with VITC"
    type: progressive
    source: "/assets/test.mov"
    duration_frames: "all"  # Use all frames from the source
    line_injections:
      - type: vitc
        target_lines: [19, 20, 21]
        timecode: "00:00:00:00"
        flags:
          drop_frame: false
          colour_frame: true
          field_flag: 1
        crc: true
```

---

---

### **8.2. Line Injections**

Line-based content to inject into the **VBI of frames** in the parent section.  
Each line injection is defined as an item in the `line_injections` list under a section.

---

#### **Common Fields for All Line Injections**


| **Field**      | **Type** | **Required**              | **Description**                                                                                                                                                  | **Example**    |
| -------------- | -------- | ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------- |
| `type`         | string   | Yes                       | Type of line injection: `vits`, `laserdisc`, `vitc`, or `line_content`.                                                                                          | `"vits"`       |
| `target_lines` | list     | Yes (except `laserdisc`)  | Lines to inject into (1-based, sequential across the entire frame). **Not applicable for `laserdisc` injections** — line placement is fixed by the standard.     | `[10, 11, 12]` |


---

#### **VITS (Vertical Interval Test Signals)**

For **VITS waveform definitions**, refer to:

- [NTSC VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/NTSC-VITS.md)
- [PAL VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/PAL-VITS.md)

Valid `vits_type` values depend on the active video standard. PAL types must only be used in PAL projects; NTSC types must only be used in NTSC projects.

##### **Fields**


| **Field**   | **Type** | **Required** | **Description**                                             | **Example**         |
| ----------- | -------- | ------------ | ----------------------------------------------------------- | ------------------- |
| `vits_type` | string   | Yes          | Type of VITS waveform. Valid values depend on the standard. | `"ntc7-composite"`  |


##### **`vits_type` Options — PAL**


| **VITS Type**     | **Description**                                                         | **Reference**                                                                                               |
| ----------------- | ----------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| `vits17`          | EBU/CCIR primary insertion test signal (VBI line 17, field 1).          | [PAL VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/PAL-VITS.md)               |
| `itu-multiburst`  | ITU multiburst signal for PAL systems B, D, G, H, I (VBI line 18, field 1). | [PAL VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/PAL-VITS.md)          |
| `uk-national`     | IBA/EBU UK national insertion test signal for PAL-I (VBI line 19, field 1). | [PAL VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/PAL-VITS.md)           |
| `vits20`          | IBA/EBU UK ITS-2 chrominance amplitude reference signal (VBI line 20, field 1). | [PAL VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/PAL-VITS.md)      |
| `itu-composite`   | ITU PAL composite insertion test signal, BT.628/BT.473 (VBI line 330, field 2). | [PAL VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/PAL-VITS.md)      |
| `itu-combination` | ITU combination insertion test signal for PAL, BT.473 (VBI line 331, field 2). | [PAL VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/PAL-VITS.md)       |


##### **`vits_type` Options — NTSC**


| **VITS Type**      | **Description**                                                                        | **Reference**                                                                                                |
| ------------------ | -------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `ntc7-composite`   | NTC-7 composite insertion test signal, EIA RS-498 / SMPTE RP 168 (VBI line 17, field 1).   | [NTSC VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/NTSC-VITS.md)         |
| `ntc7-combination` | NTC-7 combination insertion test signal, EIA RS-498 / SMPTE RP 168 (VBI line 280, field 2). | [NTSC VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/NTSC-VITS.md)        |
| `fcc-multiburst`   | FCC multi-burst insertion test signal, FCC Part 73 / EIA RS-498 (VBI line 18, field 1).    | [NTSC VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/NTSC-VITS.md)         |
| `fcc-composite`    | FCC composite insertion test signal, FCC Part 73 / EIA RS-498 (VBI line 281, field 2).     | [NTSC VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/NTSC-VITS.md)         |
| `virs`             | Video Index Reference Signal, SMPTE RP 168 / EIA RS-498. Format identification and level calibration aid for NTSC recordings. | [NTSC VITS Definitions](../analogue-video-specifications/docs/video_metadata/VITS/NTSC-VITS.md) |


##### **Example**

```yaml
line_injections:
  - type: vits
    target_lines: [17]
    vits_type: "ntc7-composite"
```

---

#### **Laserdisc Biphase Encoding**

For **Laserdisc biphase encoding**, refer to:

- [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) (and Amendments 1–2): Laservision PAL
- [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) (and Amendments 1–2): Laservision NTSC

> **Important**: For `laserdisc` injections, **`target_lines` must not be specified**. VBI line placement is entirely determined by the standard: each code type is assigned to specific lines based on the `disc_type` and the current field identity (which field opens the picture). User-specified `target_lines` on a `laserdisc` injection will fail validation.

Laserdisc VBI encoding is a **frame-level operation**: both fields of each frame carry codes on specific lines, with many codes split across fields or placed conditionally based on which field is the first field of the picture. This field-conditional placement is intrinsic to the standard and is handled automatically by the encoder.

##### **Reserved VBI Address Ranges**

The following VBI lines are reserved exclusively for laserdisc address/data signals. No other injection type may target lines within these ranges when a `laserdisc` injection is active in the same section:

| **Standard** | **Field** | **Reserved Line Range** | **Reference**    |
| ------------ | --------- | ----------------------- | ---------------- |
| PAL          | Field 1   | 6–18                    | IEC 60856 §9.1.4 |
| PAL          | Field 2   | 319–331                 | IEC 60856 §9.1.4 |
| NTSC         | Field 1   | 10–18                   | IEC 60857 §9.1.5 |
| NTSC         | Field 2   | 273–281                 | IEC 60857 §9.1.5 |

> **PAL note**: Lines 6–15 within the Field 1 reserved range are occupied by equalizing and vertical sync pulses and carry no biphase content. The active biphase insertion lines within the reserved range are **16–18** (Field 1) and **329–331** (Field 2).
>
> **NTSC note**: Lines 10–15 are within the reserved range; lines 10–11 and 273–274 additionally carry the 40-bit FM coded signal (see below). The active 24-bit biphase insertion lines are **16–18** (Field 1) and **279–281** (Field 2). Because the reserved range spans 10–18, **NTSC NTC-7 VITS types that target lines 17 and 280 cannot coexist with a laserdisc injection** in the same section and will fail validation.

##### **Code Systems**

Two coding systems are defined:

1. **24-bit biphase coded signal** (PAL and NTSC, §10.1): The primary address/control code system. Each bit cell is 2 µs; the digital level ranges from 30% to 100% white (PAL) or 0 to 100 IRE (NTSC). The 24-bit payload is subdivided into 6 hex nibbles; the first nibble is always the key and begins with a logic-one bit.

2. **40-bit FM coded signal** (NTSC only, §10.2): An additional signal on lines 10/273 (data) and 11/274 (white flag). It provides picture number information (CAV) or programme time code (CLV) independently of the 24-bit biphase system and is always present alongside it on compliant NTSC discs.

##### **24-bit Biphase Code Types and Line Placement**

Lines are assigned per code type and field identity. "First field of picture" is the field that carries the picture number (CAV) or programme time code (CLV); "second field of picture" is the other field.

**PAL — IEC 60856 §10.1 (as amended):**

| **Code Type**              | **First-Field Lines** | **Second-Field Lines** | **Priority / Notes**                                                                                     |
| -------------------------- | --------------------- | ---------------------- | -------------------------------------------------------------------------------------------------------- |
| Lead-in                    | 17, 18                | 330, 331               | Both fields; ≥ 1.5 mm of tracks before programme start. Code: `88FFFF`                                 |
| Lead-out                   | 17, 18                | 330, 331               | Both fields; ≥ 2 mm of tracks after programme end. Code: `80EEEE`                                      |
| Picture number (CAV)       | 17, 18                | —                      | First field only. Code: `FX₁X₂X₃X₄X₅`; max 99 999; auto-increments each frame.                        |
| Picture stop (CAV)         | —                     | 329, 330               | Second field only (field after picture-number field). Code: `82CFFF`; overrides programme status on 329. |
| Chapter number (CAV)       | 17, 18                | 330, 331               | Fields without picture number. Lines 17/330 are lower priority than picture stop. Code: `8X₁X₂DDD`; max 79. |
| Programme time code (CLV)  | 17, 18                | —                      | First field only. Code: `FX₁DDX₂X₃`; X₁ = hours; X₂X₃ = minutes; auto-updates each frame.             |
| CLV code                   | 17 or 330             | —                      | Fields without programme time code and CLV picture number. Code: `87FFFF`                               |
| CLV picture number         | 16 or 329             | —                      | First field only (Amendment 2). Code: `8X₁EX₃X₄X₅`; auto-updates each frame.                          |
| Chapter number (CLV)       | 18 or 331             | —                      | Fields without programme time code and CLV picture number. Same hex format as CAV.                      |
| Programme status (CAV)     | 16, 329               | —                      | Every frame. Picture stop has priority on line 329. Code: `8DC/BAX₃X₄X₅` (see Appendix C).              |
| Programme status (CLV)     | 16 or 329             | —                      | Same fields as CLV code insertion. Code: `8DC/BAX₃X₄X₅`                                                |
| Users code                 | 16, 329               | —                      | Lead-in and/or lead-out area only. Code: `8X₁DX₃X₄X₅`                                                  |

**NTSC — IEC 60857 §10.1 (as amended):**

| **Code Type**              | **First-Field Lines** | **Second-Field Lines** | **Priority / Notes**                                                                                      |
| -------------------------- | --------------------- | ---------------------- | --------------------------------------------------------------------------------------------------------- |
| Lead-in                    | 17, 18                | 280, 281               | Both fields; ≥ 1.5 mm of tracks before programme start. Code: `88FFFF`                                  |
| Lead-out                   | 17, 18                | 280, 281               | Both fields; ≥ 2 mm of tracks after programme end. Code: `80EEEE`                                       |
| Picture number (CAV)       | 17, 18                | —                      | First field only. Code: `FX₁X₂X₃X₄X₅`; max 79 999 (Amendment 2); auto-increments each frame.           |
| Picture stop (CAV)         | —                     | 279, 280               | Second field only (field after picture-number field). Code: `82CFFF`; overrides programme status on 279. |
| Chapter number (CAV)       | 17, 18                | 280, 281               | Fields without picture number. Lines 17/280 are lower priority than picture stop. Code: `8X₁X₂DDD`; max 79. |
| Programme time code (CLV)  | 17, 18                | —                      | First field only. Code: `FX₁DDX₂X₃`; auto-updates each frame.                                           |
| CLV code                   | 17 or 280             | —                      | Fields without programme time code and CLV picture number. Code: `87FFFF`                                |
| CLV picture number         | 16 or 279             | —                      | First field only (Amendment 2). Code: `8X₁EX₃X₄X₅`; auto-updates each frame.                           |
| Chapter number (CLV)       | 18 or 281             | —                      | Fields without programme time code and CLV picture number.                                               |
| Programme status (CAV)     | 16, 279               | —                      | Every frame. Picture stop has priority on line 279. Code: `8DC/BAX₃X₄X₅`                                |
| Programme status (CLV)     | 16 or 279             | —                      | Same fields as CLV code insertion. Code: `8DC/BAX₃X₄X₅`                                                 |
| Users code                 | 16, 279               | —                      | Lead-in and/or lead-out area only. Code: `8X₁DX₃X₄X₅`                                                   |

##### **40-bit FM Coded Signal (NTSC only — IEC 60857 §10.2)**

This signal co-exists with the 24-bit biphase system and occupies separate lines within the laserdisc reserved range. It is mandatory on compliant NTSC discs and is enabled by including the appropriate `code_type` values in the `codes` list.

| **Code Type**                   | **Field 1 Line** | **Field 2 Line** | **Notes**                                                                                           |
| ------------------------------- | ---------------- | ---------------- | --------------------------------------------------------------------------------------------------- |
| Lead-in white flag              | 11               | —                | Present for ≥ tracks corresponding to 1.5 mm before programme start.                               |
| Lead-out white flag             | 11               | 274              | Present for ≥ 600 tracks after programme end.                                                       |
| Picture numbers (CAV)           | 10               | 273              | Always both lines; picture number updated on second field of each picture; max 79 999 (Amendment 2); auto-increments. |
| White flag (first-field marker) | 11 or 274        | —                | Line 11 = field that opens the picture; line 274 = the other field. Controls player still-picture logic. |
| Programme time code (CLV)       | 10               | 273              | Always both lines; minutes and seconds; auto-updates each frame.                                    |

##### **Fields**

| **Field**   | **Type** | **Required** | **Description**                            | **Example** |
| ----------- | -------- | ------------ | ------------------------------------------ | ----------- |
| `disc_type` | string   | Yes          | `"CAV"` or `"CLV"`.                        | `"CAV"`     |
| `codes`     | list     | Yes          | List of biphase codes to encode per frame. | (See below) |

> **Note**: `target_lines` must **not** be specified for `laserdisc` injections.

##### **`codes` Item Fields**

| **Field**           | **Type**   | **Required**           | **Description**                                                                                 | **Example**    |
| ------------------- | ---------- | ---------------------- | ----------------------------------------------------------------------------------------------- | -------------- |
| `code_type`         | string     | Yes                    | Type of biphase code (see tables above for valid values per disc format).                       | `"picture_number"` |
| `start_value`       | integer    | For incrementing codes | Starting value for codes that auto-increment per frame (e.g. `picture_number`, `clv_picture_number`). | `1`        |
| `chapter`           | integer    | For chapter codes      | Chapter number (0–79).                                                                          | `1`            |
| `time_hours`        | integer    | CLV time code          | Hours component of programme time code starting value.                                          | `0`            |
| `time_minutes`      | integer    | CLV time code          | Minutes component of programme time code starting value.                                        | `30`           |
| `programme_status`  | hex string | Status code            | 24-bit programme status value per IEC 60856/60857 Appendix C (audio/video channel use).         | `"0x8F0000"`   |
| `user_data`         | hex string | Users code             | 24-bit users code value; first nibble must be 8; `X₁` = 0–7; see IEC 60856/60857 §10.1.9.      | `"0x810123"`   |

##### **Valid `code_type` Values — CAV**

| **`code_type`**    | **Hex Format**   | **Description**                                                                                        |
| ------------------ | ---------------- | ------------------------------------------------------------------------------------------------------ |
| `lead_in`          | `88FFFF`         | Lead-in marker; inserted for ≥ 1.5 mm of tracks before programme start.                               |
| `lead_out`         | `80EEEE`         | Lead-out marker; inserted for ≥ 2 mm of tracks after programme end.                                   |
| `picture_number`   | `FX₁X₂X₃X₄X₅`   | Frame address; max 99 999 (PAL) / 79 999 (NTSC); auto-increments from `start_value` each frame.       |
| `picture_stop`     | `82CFFF`         | Inserted in the field following the picture-number field to trigger player still-frame mode.           |
| `chapter_number`   | `8X₁X₂DDD`       | Chapter identifier (0–79); requires `chapter` field.                                                  |
| `programme_status` | `8DC/BAX₃X₄X₅`   | Audio/video channel use identification per Appendix C; requires `programme_status` field.             |
| `users_code`       | `8X₁DX₃X₄X₅`    | Optional user identification; lead-in/lead-out only; requires `user_data` field.                      |
| `fm_picture_number`| (40-bit FM)      | **NTSC only.** 40-bit FM coded picture number on lines 10/273; auto-increments from `start_value`.    |
| `fm_white_flag`    | (100 IRE white)  | **NTSC only.** First-field white flag on line 11 or 274.                                               |

##### **Valid `code_type` Values — CLV**

| **`code_type`**         | **Hex Format**  | **Description**                                                                                         |
| ----------------------- | --------------- | ------------------------------------------------------------------------------------------------------- |
| `lead_in`               | `88FFFF`        | Lead-in marker; inserted for ≥ 1.5 mm of tracks before programme start.                                |
| `lead_out`              | `80EEEE`        | Lead-out marker; inserted for ≥ 2 mm of tracks after programme end.                                    |
| `programme_time_code`   | `FX₁DDX₂X₃`    | Running time (hours + minutes); auto-updates from `time_hours`/`time_minutes` each frame.              |
| `clv_code`              | `87FFFF`        | CLV format indicator; on fields without programme time code or CLV picture number.                      |
| `clv_picture_number`    | `8X₁EX₃X₄X₅`   | Frame identifier within second (seconds + picture count); auto-updates from `start_value` each frame.  |
| `chapter_number`        | `8X₁X₂DDD`      | Chapter identifier (0–79); requires `chapter` field.                                                   |
| `programme_status`      | `8DC/BAX₃X₄X₅`  | Audio/video channel use identification per Appendix C; requires `programme_status` field.              |
| `users_code`            | `8X₁DX₃X₄X₅`   | Optional user identification; lead-in/lead-out only; requires `user_data` field.                       |
| `fm_programme_time_code`| (40-bit FM)     | **NTSC only.** 40-bit FM coded programme time (minutes + seconds) on lines 10/273; auto-updates.       |
| `fm_white_flag`         | (100 IRE white) | **NTSC only.** First-field white flag on line 11 or 274.                                               |

##### **Example (PAL CAV)**

```yaml
line_injections:
  - type: laserdisc
    disc_type: CAV
    # No target_lines — line placement is fixed by IEC 60856 §10
    codes:
      - code_type: picture_number
        start_value: 1          # Auto-increments each frame
      - code_type: picture_stop
      - code_type: chapter_number
        chapter: 1
      - code_type: programme_status
        programme_status: "0x8F0000"
```

##### **Example (NTSC CLV)**

```yaml
line_injections:
  - type: laserdisc
    disc_type: CLV
    # No target_lines — line placement is fixed by IEC 60857 §10
    codes:
      - code_type: programme_time_code
        time_hours: 0
        time_minutes: 30        # Auto-updates each frame
      - code_type: clv_code
      - code_type: clv_picture_number
        start_value: 0          # Auto-updates each frame
      - code_type: chapter_number
        chapter: 1
      - code_type: programme_status
        programme_status: "0x8F0000"
      - code_type: fm_programme_time_code  # 40-bit FM signal (NTSC mandatory)
      - code_type: fm_white_flag
```

---

#### **VITC (Vertical Interval Timecode)**

> **Note**: VITC is **incompatible with laserdisc biphase encoding**. Laserdisc discs use the IEC biphase system as their sole timecode and address mechanism; VITC is not used on laserdisc and must not be combined with a `laserdisc` injection in the same section. Doing so will fail validation.

For **VITC timecode**, refer to:

- [SMPTE 12M](../analogue-video-specifications/docs/video_metadata/IEC-60461-2010-Time-and-control-code/IEC-60461-2010-Time-and-control-code.md): Vertical Interval Timecode

##### **Fields**


| **Field**   | **Type** | **Required** | **Description**                           | **Example**             |
| ----------- | -------- | ------------ | ----------------------------------------- | ----------------------- |
| `timecode`  | string   | Yes          | Timecode in `HH:MM:SS:FF` format.         | `"01:23:45:12"`         |
| `flags`     | object   | No           | VITC flags.                               | `{ drop_frame: false }` |
| `crc`       | boolean  | No           | Enable CRC-16 checksum (default: `true`). | `true`                  |
| `user_data` | string   | No           | Optional 16-bit user data (hex).          | `"0x1234"`              |


##### `**flags` Fields**


| **Field**      | **Type** | **Required** | **Description**                             | **Example** |
| -------------- | -------- | ------------ | ------------------------------------------- | ----------- |
| `drop_frame`   | boolean  | No           | Drop-frame flag for NTSC.                   | `false`     |
| `colour_frame` | boolean  | No           | Colour frame flag.                          | `true`      |
| `field_flag`   | integer  | No           | Field flag: `1` (Field 1) or `2` (Field 2). | `1`         |


##### **Example**

```yaml
line_injections:
  - type: vitc
    target_lines: [19, 20, 21]
    timecode: "01:23:45:12"
    flags:
      drop_frame: false
      colour_frame: true
      field_flag: 1
    crc: true
    user_data: "0x1234"
```

---

---

## **9. Field and Line Handling**

### **Specification Cross-Check (Section 9)**

- PAL/NTSC frame/field line totals and scanning relationships are grounded in [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Annex 1, Table 1 and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §1.1/§9/§13.
- Horizontal sync width (`4.7 us`), equalizing pulse width (`2.3 us`), vertical-sync pulse block durations (`2.5H` PAL, `3H` NTSC), and burst phase conventions are grounded in [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md), Table 2/Table 3/Figures 5/8/9, and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §13.1-§13.3.
- PAL laserdisc pilot burst and NTSC VBI burst behavior are grounded in [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §9.1.2 and [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §9.1.2.

For **field and line handling**, refer to:

- [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md): Conventional Television Systems.

---

### **Line Numbering**

VideoSynth generates frames as **two sequential fields** written one after the other. There is no interlacing — lines are numbered and processed purely sequentially within the frame. The line numbering scheme follows the analogue standard line counts (625 for PAL, 525 for NTSC), where each line number maps directly to a position in the sequential output.

- **Lines are numbered 1 to 625 (PAL) or 1 to 525 (NTSC), sequentially across the frame in field order.**
- **Field 1** occupies the first half of the frame: lines 1–312 (PAL) or lines 1–262 (NTSC).
- **Field 2** occupies the second half of the frame: lines 313–625 (PAL) or lines 263–525 (NTSC).
- **VBI Lines**: Lines 1–22 (PAL) and 1–21 (NTSC) in each field are the VBI region available for metadata injection (e.g., VITS, VITC, Laserdisc biphase).

> **Note**: This numbering is the generator's internal sequential line addressing. It does not imply anything about how the signal is displayed on an analogue monitor — the analogue receiver is responsible for interpreting the two sequential fields as an interlaced image. VideoSynth itself performs no interlacing.

---

### **Progressive Source to Two-Field Frame Conversion**

When ingesting a progressive source, the generator splits each progressive frame into **two fields** and writes them sequentially into the output frame. This is a spatial line-splitting operation — not interlacing. The output is two fields in sequence, each containing half the source lines.

1. **Split Fields**:
  - For a progressive source frame of height `H`:
    - **Field 1**: Odd source lines (1, 3, 5, ..., H-1), written sequentially as lines 1, 2, 3, ... of Field 1.
    - **Field 2**: Even source lines (2, 4, 6, ..., H), written sequentially as lines 1, 2, 3, ... of Field 2.
  - **PAL Example (720×576 progressive source)**:
    - Field 1: source lines 1, 3, 5, ..., 575 → 288 lines.
    - Field 2: source lines 2, 4, 6, ..., 576 → 288 lines.
  - **NTSC Example (720×480 progressive source)**:
    - Field 1: source lines 1, 3, 5, ..., 479 → 240 lines.
    - Field 2: source lines 2, 4, 6, ..., 480 → 240 lines.
2. **Field Order**:
  - **PAL**: Field 1 is always written first, then Field 2.
  - **NTSC**: Field 1 is written first, then Field 2 (native 29.97 fps sources).
3. **Field Dominance**:
  - **PAL**: Field 1 is **temporally first** (dominant).
  - **NTSC**: Field 2 is **temporally first** for 3:2 pulldown sequences (24 fps sources).

---

### **Sync and Burst Insertion**

For **HSync, VSync, and colour burst insertion**, refer to:

- [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md)
- [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md)

1. **Horizontal Sync (HSync)**:
  - Insert a **4.7 µs pulse at 0V** at the start of each line.
  - Followed by **back porch (0.3V)** and **colour burst (2.25 µs, 0.3V p-p)**.
  - **PAL**: Burst phase polarity follows the BT.1700 Table 1 item 10f sequence (I/II/III/IV over colour fields), with nominal **±135°** phase per line and sequence-dependent odd/even polarity assignment.
  - **NTSC**: Burst phase follows continuous-subcarrier line progression (line-to-line $\pi$ alternation at 4fsc in the current implementation).
2. **Vertical Sync (VSync)**:
  - **PAL**:
    - **Lines 623-625**: 5 broad pulses (2.5H each) with equalizing pulses.
    - **Equalizing Pulses**: 5 pulses before/after VSync (2.3 µs each).
  - **NTSC**:
    - **Lines 523-525**: 3 broad pulses (3H each) with equalizing pulses.
    - **Equalizing Pulses**: 6 pulses before/after VSync (2.3 µs each).
  - **PAL burst blanking during vertical interval**: burst suppression windows follow BT.1700 Figure 8 sequence windows:
    - Sequence I: lines 623–625 and 1–6.
    - Sequence II: lines 310–318.
    - Sequence III: lines 622–625 and 1–5.
    - Sequence IV: lines 311–319.
3. **PAL Laserdisc Pilot Burst** (when `pal_laserdisc_pilot_burst: true`):
  - Superimpose a continuous **3.75 MHz** ($240 \times f_H$) sinusoidal burst on the **sync pulse level** of every horizontal and vertical sync pulse in every line (see [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §9.1.2).
  - Amplitude: $\frac{6}{7} \times 700\ \text{mV} = 600\ \text{mV p-p}$, centred on sync tip (−300 mV), so the burst swings between **−600 mV and 0 mV**.
  - The pilot burst is applied to the full duration of every sync pulse (line-sync, broad/vertical sync, and equalizing pulses).
  - This extends the signal range 300 mV below standard sync tip; see §6.1 for the headroom rationale and the required internal floating-point representation.
4. **NTSC Laserdisc VBI Burst** (when `ntsc_laserdisc_vbi_burst: true`):
  - Insert the standard NTSC **colour burst (3.579545 MHz)** on the back porch of **equalizing pulses and broad (field) sync pulses** during the vertical interval, where burst is normally suppressed in standard NTSC (see [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §9.1.2).
  - The burst uses the same amplitude and phase as regular NTSC colour burst; no new frequency or amplitude is introduced.
  - This applies to all equalizing pulse and broad sync pulse lines in both Field 1 (lines 1–9) and Field 2 (lines 263–271).

---

---

## **10. 4fsc Sampling and Subcarrier Locking**

### **Specification Cross-Check (Section 10)**

- Core 4fsc sampling rule is grounded in [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) §3.1 (sample at `4 x fsc`, phase-referenced to subcarrier).
- NTSC `14.31818 MHz` and sample-axis relationship (I/Q axes) are grounded in [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) §3.4 and §4.1.2.
- PAL `17.734475 MHz` interface sampling reference is grounded in [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md) §1.1.1.
- Subcarrier nominal frequencies and frequency tolerances are grounded in [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Table 2 item 2.11(a), and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §11.1.

For **4fsc sampling**, refer to:

- [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md): Bit-Parallel Digital Interface for NTSC.
- [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md): 625-Line Digital PAL Interfaces.

---

### **4fsc Sampling Modes**


| **Mode**                 | **Description**                                                                                                                  | **Use Case**                                                                         | **Reference**                     |
| ------------------------ | -------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ | --------------------------------- |
| **Locked to Subcarrier** | Sample clock is **phase-locked** to the colour subcarrier (e.g., 4 × f_sc) using an **NCO (Numerically Controlled Oscillator)**. | Studio applications, testing, or when phase coherence is critical.                   | SMPTE 244M-2003, EBU Tech. 3280-E |
| **Free-Running**         | Sample clock runs **independently** of the subcarrier (e.g., 17.734475 MHz for PAL, but not locked).                             | General-purpose generation, emulation, or when subcarrier alignment is not required. | SMPTE 244M-2003, EBU Tech. 3280-E |


---

### **Subcarrier Lock Implementation**

For **testing applications**, the subcarrier locking implementation **must meet or exceed** the phase accuracy specified in the PAL/NTSC standards. The following approach is used:

- **Numerically Controlled Oscillator (NCO)**:
  - Use an **NCO** for precise frequency synthesis and phase alignment.
  - **Phase Accuracy**: The NCO must achieve **≤ ±1° phase error** relative to the subcarrier, as specified in [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md) and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md).
  - **Frequency Accuracy**: The NCO must generate a sample clock with **≤ 1 Hz error** relative to the ideal 4fsc rate (17.734475 MHz for PAL, 14.31818 MHz for NTSC).
  - **Alignment**:
    - Samples must align with the **U-axis (PAL)** or **I-axis (NTSC)** zero-crossings of the colour subcarrier.

---

### **YAML Configuration**

```yaml
cvbs_presets:
  sample_encoding_preset: CVBS_U10_4FSC
  signal_state_preset: STANDARD_TBC_LOCKED
```

---

### **Implementation Notes**

- **Locked Mode**:
  - Use an **NCO** to generate samples at `4 × f_sc` with **≤ ±1° phase error**.
  - Align samples to the **U-axis (PAL)** or **I-axis (NTSC)** zero-crossings.
- **Free-Running Mode**:
  - Use a **fixed sample rate** (e.g., 17.734475 MHz for PAL) without phase locking.

---

---

## **11. VBI Line Allocation**

### **Specification Cross-Check (Section 11)**

- PAL laserdisc reserved ranges and address-signal constraints are grounded in [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §9.1.4 and Clause 10.
- NTSC laserdisc reserved ranges and 40-bit FM coexistence are grounded in [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §9.1.5, §10.1, §10.2.
- VIRS/ITS line assignments for NTSC laserdisc are grounded in [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §9.1.3-§9.1.4.
- PAL amendment-specific alternates (e.g., added CLV picture-number behavior and alternate VBI usage) are grounded in [IEC 60856 Amendment 2](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL-Amendment-2/IEC-60856-1986-Laservision-PAL-Amendment-2.md) §9.1.3/§9.1.4/§10.1.10.
- NTSC amendment-specific CLV/FM updates are grounded in [IEC 60857 Amendment 2](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC-Amendment-2/IEC-60857-1986-Laservision-NTSC-Amendment-2.md) §10.1.10 and §10.2.3.
- VITC location constraints used for conflict checks are grounded in [IEC 60461:2010](../analogue-video-specifications/docs/video_metadata/IEC-60461-2010-Time-and-control-code/IEC-60461-2010-Time-and-control-code.md) §9.6.2 and §9.6.3.

VBI line allocations differ between PAL and NTSC and between Laserdisc and non-Laserdisc use. The laserdisc standards define exclusive reserved ranges that must not overlap with any other injection type.

---

### **PAL VBI Line Allocation (IEC 60856 as amended, ITU-R BT.470-6)**

| **Signal**                         | **Field 1 Lines** | **Field 2 Lines** | **Notes**                                                                                                  |
| ---------------------------------- | ----------------- | ----------------- | ---------------------------------------------------------------------------------------------------------- |
| Laserdisc biphase — reserved range | 6–18              | 319–331           | Entire range reserved; no other injection allowed here when laserdisc is active. Lines 6–15 carry sync/eq pulses and hold no biphase content. |
| Laserdisc biphase — key code lines | 16–18             | 329–331           | Active biphase payload: picture numbers, chapter codes, status, etc. (within reserved range).             |
| VITS                               | 19, 20 (or 13 per Amend. 2) | 332, 333 (or 326 per Amend. 2) | Lines 22 and 335 shall be blanked when laserdisc is active (disk noise measurement).   |
| VITC                               | 19–21             | 332–334           | **Incompatible with laserdisc biphase.** Lines are outside the laserdisc reserved range but VITC must not be used in any section that also contains a `laserdisc` injection. |
| Subtitle / closed caption          | 20, 21            | 333, 334          | No VITS on lines 20/333 when subtitle is present; lines 14, 15/327, 328 for extra subtitle capacity (Amend. 2). |
| CEA-608                            | 22                | 335               | Line 335 shall be blanked when laserdisc is active.                                                       |

---

### **NTSC VBI Line Allocation (IEC 60857 as amended, SMPTE 170M-2004)**

| **Signal**                              | **Field 1 Lines** | **Field 2 Lines** | **Notes**                                                                                                                    |
| --------------------------------------- | ----------------- | ----------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| Laserdisc biphase — reserved range      | 10–18             | 273–281           | Entire range reserved; no other injection allowed here when laserdisc is active.                                            |
| Laserdisc 40-bit FM coded signal (NTSC) | 10, 11            | 273, 274          | Within reserved range; 40-bit FM data on 10/273, white flag on 11/274.                                                      |
| Laserdisc biphase — key code lines      | 16–18             | 279–281           | Active 24-bit biphase payload (within reserved range).                                                                       |
| VITC                                    | 14–16             | 277–279           | **Incompatible with laserdisc biphase, and lines 14–16/277–279 fall within the NTSC laserdisc reserved range.** Must not appear in any section that contains a `laserdisc` injection. |
| VIR signal                              | 19                | 282               | Mandatory on NTSC laserdisc per IEC 60857 §9.1.3; FCC Recommendation 73-699.                                               |
| VITS (NTC-7 composite/combination)      | 17, 280           | —                 | **Conflicts with laserdisc reserved range.** NTC-7 types cannot coexist with a laserdisc injection in the same section.      |
| VITS (composite/combination test)       | 20                | 283               | Recommended by IEC 60857 §9.1.4; outside reserved range; compatible with laserdisc.                                        |
| CEA-608 / closed caption                | 21                | 284 (first half)  | Per PBS Report E7709 (handicapped caption data).                                                                             |

---

### **Conflict Resolution**

- **If injected lines overlap within a section, the YAML must fail validation.**
- **When a `laserdisc` injection is active in a section, any other injection targeting lines within the reserved range for that standard must fail validation** (PAL: F1 6–18, F2 319–331; NTSC: F1 10–18, F2 273–281).
- **A `vitc` injection and a `laserdisc` injection must not appear in the same section.** Laserdisc does not use VITC; the biphase system is the sole timecode and address mechanism on laserdisc.
---

---

## **12. Implementation Pipeline**

### **Specification Cross-Check (Section 12)**

- Validation-stage checks listed here are a direct implementation of constraints traced in Sections 7, 8, 11, and 13 (and therefore map to BT.470/1700, SMPTE 170M/244M, IEC 60856/60857, IEC 60461).
- Generation-stage sync, burst, and modulation behavior is grounded in [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md) and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md).
- Output-stage quantization and reserved/excluded code handling is grounded in [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md) and [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) §4.2.
- Rise/fall-time targets (`140 ns ± 20 ns`, `200 ns ± 50 ns`) are grounded in [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §13 notes (default pulse transitions) and [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md), Table 2/Table 3.

---

The generator uses a **modular pipeline** to process sections and combine them into the final output. The pipeline is divided into the **Generation Stage** and **Output Stage**.

### **Pipeline Overview**

```
[YAML Project File] → [Validation] → [Generation Stage] → [Output Stage] → [CVBS File (Video + Metadata)]
```

---

### **Detailed Pipeline Steps**

#### **1. Validation Stage**

- Parse the YAML file and validate all fields.
- Check for conflicts (e.g., overlapping `target_lines` in line injections).
- Ensure the selected `signal_state_preset` denotes locked operation when using a 4fsc `sample_encoding_preset`.
- **Fail validation if any errors are found.**

#### **2. Generation Stage**

- **Input**: Validated YAML project file.
- **Output**: Time-based representations of **luma (Y)** and **chroma (C)** signals.
- **Steps**:
  1. For each section:
    - Generate the **frame-based content** (`software_generated` or `progressive`).
    - For each frame in the section:
      - Apply **all line injections** for that section to the frame's VBI lines.
      - Insert **sync pulses** and **colour burst** (see [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md)).
      - Apply **ramping and transition smoothing** to simulate analogue behavior (see [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md) for ramping requirements).
    - Output **time-based Y and C signals** (not yet sampled).

#### **3. Output Stage**

- **Input**: Time-based Y and C signals from the generation stage.
- **Output**: CVBS file (video + metadata).
- **Steps**:
  1. **Generate Metadata**:
    - Create the CVBS file header with all required metadata (see [CVBS File Format Specification](../cvbs-file-format-specification/docs/index.md)).
  2. **Sampling**:
    - Sample the time-based Y and C signals at the specified rate (e.g., 4fsc, 20MSPS).
    - For **4fsc encodings with `signal_state_preset: STANDARD_TBC_LOCKED`**, use an **NCO** to lock the sample clock to the colour subcarrier.
  3. **mV to Integer Conversion**:
    - Map each `double` mV sample to a 10-bit integer using the normative linear mapping for the active standard (EBU Tech. 3280-E for PAL, SMPTE 244M-2003 for NTSC). See [§6.1](#61-signal-levels) for the formulae.
    - Clamp to the legal code range; excluded values (codes 0–3 and 1020–1023) must not appear in output.
  4. **Combining Y and C**:
    - Combine the quantised luma and chroma integer samples into a composite signal (CVBS).
  5. **Output Formatting**:
    - Write the metadata header to the **metadata file**.
    - Write the raw samples to the **video file**.

---

### **Ramping and Transition Smoothing**

To simulate **analogue output**, the generator must:

- Avoid **sharp transitions** in the signal.
- Apply **ramping** to sync pulses, colour burst, and active video transitions as specified in [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md) and [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md).
- **Rise/Fall Times**:
  - **Luma**: 140 ns ± 20 ns (10% to 90% of white level) (see [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md)).
  - **Chroma**: 200 ns ± 50 ns (10% to 90% of chroma amplitude) (see [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md)).

---

---

## **13. Error Handling and Validation**

### **Specification Cross-Check (Section 13)**

- `video_standard_preset`/line-range consistency checks map to [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Annex 1, Table 1 and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §1.1.
- `signal_state_preset` and 4fsc sample-encoding checks map to [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) §3.1 and [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md) §1.1.1.
- Laserdisc reserved-range conflicts and laserdisc-only placement logic map to [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md) §9.1.4/§10 and [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md) §9.1.5/§10.
- VITC constraints (including incompatibility decisions when laserdisc is active in the same section) map to [IEC 60461:2010](../analogue-video-specifications/docs/video_metadata/IEC-60461-2010-Time-and-control-code/IEC-60461-2010-Time-and-control-code.md) Clause 9 and laserdisc Clause 10 families.
- Frame-rate checks map to [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md), Annex 1 Table 1 and [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) §11.3.

---

### **Validation Rules**

1. **CVBS Presets**:
  - Only one `video_standard_preset` (PAL or NTSC) per project.
  - Only one `sample_encoding_preset` per project.
  - Only one `signal_state_preset` per project.
  - Output resolution is fixed by the standard (720x576 for PAL, 720x480 for NTSC) and must not be specified in the project file.
  - 4fsc generation requires a 4fsc `sample_encoding_preset` and a locked `signal_state_preset`.
  - `pal_laserdisc_pilot_burst` can **only be enabled for PAL projects**. If enabled for NTSC, the YAML is considered **invalid**.
  - `ntsc_laserdisc_vbi_burst` can **only be enabled for NTSC projects**. If enabled for PAL, the YAML is considered **invalid**.
2. **Sections**:
  - Each section must have a valid `type` (`software_generated` or `progressive`).
  - Each section must have at least one of:
    - Frame-based content (`software_generated` or `progressive`).
    - Line injections (`line_injections`).
  - For `progressive` sections, `source` must point to a valid file.
  - **Input source frame rate must match the required output format**.
  - **Progressive source dimensions must be PAL: `720x576` or `704x576`; NTSC: `720x480` or `704x480`**.
  - **When source width is `704`, ingestion must normalize to the internal `720`-wide raster using `8` pixels of nominal-black side padding on each side**.
3. **Line Injection Constraints**:
  - `target_lines` must be within the valid range for the standard (1-625 for PAL, 1-525 for NTSC).
  - `target_lines` must **not** be specified for `laserdisc` injection types; specifying it is a validation error.
  - **If injected lines overlap within a section, the YAML must fail validation.**
  - **When a `laserdisc` injection is active in a section, no other injection type may target lines within the laserdisc reserved ranges** (PAL: Field 1 lines 6–18, Field 2 lines 319–331; NTSC: Field 1 lines 10–18, Field 2 lines 273–281).
  - **A `vitc` injection and a `laserdisc` injection must not appear in the same section.** Laserdisc does not use VITC.

---

### **Error Messages**


| **Error**                                  | **Message**                                                                                                                              |
| ------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------- |
| Invalid standard                           | "video_standard_preset must be 'PAL' or 'NTSC'."                                                                                         |
| `resolution` field present                 | "`resolution` must not be specified; it is fixed by the standard."                                                                       |
| Invalid sample rate                        | "Sample rate 10MSPS may not capture the full CVBS bandwidth (5.5 MHz for PAL)."                                                          |
| Missing source file                        | "File '/nonexistent.mov' not found."                                                                                                     |
| Invalid line injection target lines        | "Line 700 is out of range for PAL (max 625)."                                                                                            |
| Laserdisc `target_lines` specified         | "`target_lines` must not be specified for laserdisc injections; line placement is fixed by the standard."                                |
| Laserdisc reserved range conflict          | "Injection type [type] targets line [n] which is within the laserdisc reserved range for [standard]; cannot coexist with laserdisc injection." |
| VITC with laserdisc                        | "`vitc` injection cannot be used in the same section as a `laserdisc` injection; laserdisc does not use VITC."                              |
| Overlapping target lines                   | "Overlapping target_lines in section: [section_name]."                                                                                   |
| Invalid signal state preset                | "signal_state_preset must indicate locked state for 4fsc generation."                                                                    |
| Invalid pilot burst                        | "pal_laserdisc_pilot_burst can only be enabled for PAL projects."                                                                        |
| Invalid VBI burst                          | "ntsc_laserdisc_vbi_burst can only be enabled for NTSC projects."                                                                        |
| Invalid frame rate                         | "Input frame rate must match the output standard's frame rate (25 fps for PAL, ~29.97 fps for NTSC)."                                    |
| Invalid progressive source dimensions      | "Progressive source dimensions are invalid for the selected standard. PAL requires 720x576 or 704x576; NTSC requires 720x480 or 704x480." |


---

---

## **14. CLI Interface**

### **Specification Cross-Check (Section 14)**

- This section defines application interface behavior (non-normative to analogue waveform standards).
- `--validate` behavior is intended to enforce constraints traced to Sections 7, 11, and 13 (which are normatively tied to BT.470/1700, SMPTE 170M/244M, IEC 60856/60857, IEC 60461).

---

### **Usage**

```bash
videosynth --project project.yaml [options]
```

---

### **Options**


| **Option**   | **Description**                                   | **Default** |
| ------------ | ------------------------------------------------- | ----------- |
| `--project`  | Path to the YAML project file (required).         | -           |
| `--validate` | Validate the YAML file without generating output. | `false`     |
| `--verbose`  | Enable verbose logging.                           | `false`     |

Output paths are configured in YAML under `output.video_path` and `output.metadata_path`.


---

### **Example**

```bash
# Generate CVBS output from a project file
videosynth --project pal_test.yaml

# Validate a project file without generating output
videosynth --project ntsc_test.yaml --validate
```

---

---

## **15. Build and Packaging**

### **Specification Cross-Check (Section 15)**

- This section is implementation/tooling guidance and does not introduce analogue waveform constants.
- No additional normative analogue-spec clauses are required here beyond those already referenced in design requirements sections.

---

### **Build Environment**

- **Language**: C++17.
- **Build System**: Nix.
- **Dependencies**:
  - **YAML Parsing**: [yaml-cpp](https://github.com/jbeder/yaml-cpp).
  - **Image/Video Decoding**: [FFmpeg](https://ffmpeg.org/) (for MOV/MP4/PNG/RAW).
  - **Sample Rate Conversion**: [soxr](https://sourceforge.net/p/soxr/).
  - **Logging**: [spdlog](https://github.com/gabime/spdlog).
  - **Testing**: [Google Test](https://github.com/google/googletest).

---

### **Nix Configuration**

#### `**flake.nix` Example**

```nix
{
  description = "VideoSynth - Analogue Video Format Generator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          name = "videosynth";
          src = ./.;

          buildInputs = with pkgs; [
            cmake
            yaml-cpp
            ffmpeg
            soxr
            spdlog
            googletest
          ];

          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];

          installPhase = ''
            mkdir -p $out/bin
            cp videosynth $out/bin/
          '';
        };
      }
    );
}
```

---

#### `**default.nix` Example**

```nix
{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.mkDerivation {
  name = "videosynth";
  src = ./.;

  buildInputs = with pkgs; [
    cmake
    yaml-cpp
    ffmpeg
    soxr
    spdlog
    googletest
  ];

  cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];

  installPhase = ''
    mkdir -p $out/bin
    cp videosynth $out/bin/
  '';
}
```

---

### **CMake Configuration**

#### `**CMakeLists.txt` Example**

```cmake
cmake_minimum_required(VERSION 3.15)
project(VideoSynth LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(yaml-cpp REQUIRED)
find_package(FFmpeg REQUIRED)
find_package(soxr REQUIRED)
find_package(spdlog REQUIRED)

# Add executable
add_executable(videosynth
    src/main.cpp
    src/yaml_parser.cpp
    src/yaml_parser.h
    src/generation_stage.cpp
    src/generation_stage.h
    src/output_stage.cpp
    src/output_stage.h
    src/field_handler.cpp
    src/field_handler.h
    src/line_injection.cpp
    src/line_injection.h
    src/vits_generator.cpp
    src/vits_generator.h
    src/laserdisc_generator.cpp
    src/laserdisc_generator.h
    src/vitc_generator.cpp
    src/vitc_generator.h
    src/nco.cpp
    src/nco.h
    src/ramping.cpp
    src/ramping.h
)

# Link libraries
target_link_libraries(videosynth
    yaml-cpp
    avcodec
    avformat
    avutil
    soxr
    spdlog::spdlog
    GTest::GTest
)

# Install
install(TARGETS videosynth DESTINATION bin)
```

---

---

## **16. Directory Structure**

### **Specification Cross-Check (Section 16)**

- This section is repository-layout guidance (non-normative to analogue waveform standards).
- The listed spec submodule locations correspond to the normative sources cited in Sections 2-13.

```
videosynth/
├── CMakeLists.txt
├── flake.nix
├── default.nix
├── src/
│   ├── main.cpp
│   ├── yaml_parser.cpp
│   ├── yaml_parser.h
│   ├── generation_stage.cpp
│   ├── generation_stage.h
│   ├── output_stage.cpp
│   ├── output_stage.h
│   ├── field_handler.cpp
│   ├── field_handler.h
│   ├── line_injection.cpp
│   ├── line_injection.h
│   ├── vits_generator.cpp
│   ├── vits_generator.h
│   ├── laserdisc_generator.cpp
│   ├── laserdisc_generator.h
│   ├── vitc_generator.cpp
│   ├── vitc_generator.h
│   ├── nco.cpp
│   ├── nco.h
│   ├── ramping.cpp
│   ├── ramping.h
│   ├── progressive_source.cpp
│   ├── progressive_source.h
│   └── ...
├── docs/
│   ├── analogue-video-specifications/  (submodule)
│   ├── cvbs-file-format-specification/ (submodule)
│   ├── design/
│   │   ├── high-level-design.md
│   │   └── ...
│   └── user/
│       ├── getting_started.md
│       ├── yaml_reference.md
│       └── ...
├── resources/
│   └── assets/                          # Built-in source assets installed with the application
│       ├── 720x480/                     # NTSC-dimensioned assets
│       │   ├── stills/
│       │   └── video/
│       ├── 704x480/                     # Optional NTSC active-picture-aligned assets (normalized to internal 720x480 raster)
│       │   ├── stills/
│       │   └── video/
│       ├── 720x576/                     # PAL-dimensioned assets
│       │   ├── stills/
│       │   └── video/
│       └── 704x576/                     # Optional PAL active-picture-aligned assets (normalized to internal 720x576 raster)
│           ├── stills/
│           └── video/
├── tests/
│   ├── test_yaml_parser.cpp
│   ├── test_generation_stage.cpp
│   ├── test_output_stage.cpp
│   └── ...
└── README.md
```

---

---

## **17. Future Requirements**

### **Specification Cross-Check (Section 17)**

- CEA-608 future feature reference is grounded in [ANSI/CTA-608-E S-2019](../analogue-video-specifications/docs/video_metadata/ANSI-CTA-608-E-S-2019/ANSI-CTA-608-E-S-2019.md).
- No additional current PAL/NTSC waveform constants are introduced in this section.

The following features are **reserved for future implementation**:

1. **CEA-608 Closed Captions**:
  - Support for embedding CEA-608 closed captions in the VBI (Line 21 for NTSC, Line 22 for PAL).
  - References:
    - [ANSI/CTA-608-E S-2019](../analogue-video-specifications/docs/video_metadata/ANSI-CTA-608-E-S-2019/ANSI-CTA-608-E-S-2019.md): Line 21 Data Services.
2. **Multi-Threading**:
  - Parallel processing of sections or frames for improved performance on multi-core systems.

---

---

## **18. Appendix: References**

### **Specification Cross-Check (Section 18)**

- To support clause-level traceability used above, include amendment documents explicitly with base laserdisc standards.
- Include [IEC 60856 Amendment 2](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL-Amendment-2/IEC-60856-1986-Laservision-PAL-Amendment-2.md) and [IEC 60857 Amendment 2](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC-Amendment-2/IEC-60857-1986-Laservision-NTSC-Amendment-2.md) when citing CLV picture-number and line-allocation updates.

All specifications referenced in this document are available in the following repositories:

- **[CVBS File Format Specification](../cvbs-file-format-specification/docs/index.md)**
- **[Analogue Video Specifications](../analogue-video-specifications/docs/index.md)**
  - [ITU-R BT.470-6](../analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md): Conventional Television Systems.
  - [ITU-R BT.1700](../analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md): Composite Video Signal Characteristics.
  - [SMPTE 170M-2004](../analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md): Composite Analog Video Signal for NTSC.
  - [SMPTE 244M-2003](../analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md): Bit-Parallel Digital Interface for NTSC.
  - [EBU Tech. 3280-E](../analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md): 625-Line Digital PAL Interfaces.
  - [IEC 60856](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL/IEC-60856-1986-Laservision-PAL.md): Laservision PAL.
  - [IEC 60856 Amendment 1](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL-Amendment-1/IEC-60856-1986-Laservision-PAL-Amendment-1.md): Laservision PAL Amendment 1.
  - [IEC 60856 Amendment 2](../analogue-video-specifications/docs/laserdisc/IEC-60856-1986-Laservision-PAL-Amendment-2/IEC-60856-1986-Laservision-PAL-Amendment-2.md): Laservision PAL Amendment 2.
  - [IEC 60857](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC/IEC-60857-1986-Laservision-NTSC.md): Laservision NTSC.
  - [IEC 60857 Amendment 1](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC-Amendment-1/IEC-60857-1986-Laservision-NTSC-Amendment-1.md): Laservision NTSC Amendment 1.
  - [IEC 60857 Amendment 2](../analogue-video-specifications/docs/laserdisc/IEC-60857-1986-Laservision-NTSC-Amendment-2/IEC-60857-1986-Laservision-NTSC-Amendment-2.md): Laservision NTSC Amendment 2.
  - [SMPTE 12M](../analogue-video-specifications/docs/video_metadata/IEC-60461-2010-Time-and-control-code/IEC-60461-2010-Time-and-control-code.md): Vertical Interval Timecode (VITC).
  - [Vertical Interval Test Signals - NTSC and PAL definitions](../analogue-video-specifications/docs/video_metadata/VITS/index.md): VITS waveforms.
