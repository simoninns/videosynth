# EXR BT.601 Compliance Requirements

Generated: 2026-06-01

This document defines the checks a verification script must perform to claim that an EXR still is:

1. structurally correct as an EXR deliverable; and
2. consistent with BT.601-derived standard-definition component video.

BT.601 does not define OpenEXR. It defines the digital component video system that the image content must represent. For that reason, the required checks are split into three groups:

- `EXR deliverable checks`: verify the file container and EXR metadata.
- `BT.601 content checks`: verify that the stored picture content is consistent with BT.601 component coding.
- `Source-coupled checks`: requirements that cannot be proven from a standalone RGB EXR and therefore must be checked against the source representation, a decoded Y'CbCr representation, or a reconstructed waveform analysis.

## 1. Normative sources

### BT.601 core

- ITU-R BT.601-5 (1995), Part A, Table 2: 13.5 MHz 4:2:2 member.
- ITU-R BT.601-5 (1995), section 2.3: spectral control and anti-alias filtering.
- ITU-R BT.601-5 (1995), sections 3.1 to 3.4: sampling structure, co-siting, and code-word usage.
- ITU-R BT.601-5 (1995), sections 3.5.1 to 3.5.5: Y', Cb, Cr derivation, quantization, and limiting.
- ITU-R BT.601-5 (1995), Appendix 1 to Part A: active-line relationship to analogue sync reference.
- ITU-R BT.601-5 (1995), Appendix 2 to Part A: luminance and chroma filter templates.
- ITU-R BT.601-5 (1995), Annex 1: implementation guidance for delay equalization and filter verification.

### System-specific supporting sources

- SMPTE 170M-2004: 525/59.94 studio colorimetry, timing, and waveform relationships.
- ITU-R BT.470-6 (1998), Annex 1, Tables 1, 1-1, 1-2, 2: conventional 525/59.94 and 625/50 system timing and colorimetry.

## 2. Important scope limit

This document assumes the EXR under test stores image data as linear RGB float. BT.601 is defined in terms of gamma-pre-corrected component signals and 4:2:2 sampling. Therefore:

- a standalone EXR can prove raster size, aspect ratio, frame-rate metadata, and exact rendered pixel values;
- a standalone EXR cannot by itself prove original 4:2:2 chroma co-siting, analogue sync position, or compliance with the Appendix 2 analogue anti-alias filters;
- to claim full BT.601 compliance, the test workflow must include source-aware checks.

## 3. Numeric requirements needed for test design

This section is the self-contained set of numbers and formulas that a test script needs. If a value appears here, the script should use this document rather than re-reading the source standards.

### 3.1 EXR structural requirements

These are direct EXR checks.

| Item | 525 set | 625 set | How to verify |
| --- | --- | --- | --- |
| File format | OpenEXR scanline image | OpenEXR scanline image | EXR header |
| Channels | `R`, `G`, `B` only | `R`, `G`, `B` only | EXR header |
| Sample type | 32-bit float | 32-bit float | EXR header |
| Compression | none | none | EXR header |
| Raster | `720 x 486` | `720 x 576` | EXR header or ffprobe |
| `dataWindow` | `(0 0) - (719 485)` | `(0 0) - (719 575)` | EXR header |
| `displayWindow` | `(0 0) - (719 485)` | `(0 0) - (719 575)` | EXR header |
| Pixel aspect ratio | `108:119` = `0.907563` | `128:117` = `1.094017` | EXR header or ffprobe |
| Frame-rate metadata | `30000/1001` | `25/1` | EXR header |
| Line order | increasing y | increasing y | EXR header |
| Gamma tag | `1` | `1` | EXR header |

### 3.2 BT.601 Part A 13.5 MHz 4:2:2 model

These are the core BT.601 numbers that define the represented signal.

| Item | 525 system | 625 system | Reference |
| --- | --- | --- | --- |
| Family member | Part A, 13.5 MHz, 4:2:2 | Part A, 13.5 MHz, 4:2:2 | BT.601 Part A Table 2 |
| Total Y samples per line | `858` | `864` | BT.601 Part A Table 2 item 2 |
| Total `C_R` samples per line | `429` | `432` | BT.601 Part A Table 2 item 2 |
| Total `C_B` samples per line | `429` | `432` | BT.601 Part A Table 2 item 2 |
| Active Y samples per line | `720` | `720` | BT.601 Part A Table 2 item 6 |
| Active `C_R` samples per line | `360` | `360` | BT.601 Part A Table 2 item 6 |
| Active `C_B` samples per line | `360` | `360` | BT.601 Part A Table 2 item 6 |
| Y sampling frequency | `13.5 MHz` | `13.5 MHz` | BT.601 Part A Table 2 item 4 |
| `C_R`/`C_B` sampling frequency | `6.75 MHz` | `6.75 MHz` | BT.601 Part A Table 2 item 4 |
| Sample structure | Orthogonal, line/field/frame repetitive | Orthogonal, line/field/frame repetitive | BT.601 sections 3.1 and Table 2 item 3 |
| Chroma co-siting | `C_R` and `C_B` co-sited with odd Y samples | same | BT.601 section 3.2 and Table 2 item 3 |
| Coding | Uniform PCM, 8-bit or optionally 10-bit component coding | same | BT.601 Part A Table 2 item 5 |

### 3.3 BT.601 matrix and quantization formulas

These formulas should be used whenever the script converts the EXR RGB values into BT.601-derived component values for analysis.

#### Primary-to-component equations

For gamma-pre-corrected component signals:

$$
E_Y' = 0.299 E_R' + 0.587 E_G' + 0.114 E_B'
$$

$$
E_R' - E_Y' = 0.701 E_R' - 0.587 E_G' - 0.114 E_B'
$$

$$
E_B' - E_Y' = -0.299 E_R' - 0.587 E_G' + 0.886 E_B'
$$

Renormalized colour-difference signals:

$$
K_R = 0.713, \quad K_B = 0.564
$$

$$
E_{C_R}' = 0.500 E_R' - 0.419 E_G' - 0.081 E_B'
$$

$$
E_{C_B}' = -0.169 E_R' - 0.331 E_G' + 0.500 E_B'
$$

#### 8-bit quantization equations

In this document, `8-bit` means an 8-bit scalar component code value in BT.601 component coding, namely one code value for `Y'`, `C_R`, or `C_B` on the range `0` to `255`. It does not mean packed `RGB888` pixel storage.

Available code values are `0` to `255`, but for 4:2:2 active video:

- `0` and `255` are reserved for synchronization data.
- Active video may use only `1` to `254`.
- Nominal black for Y is `16`.
- Nominal peak white for Y is `235`.
- Chroma zero is `128`.

Quantization formulas:

$$
\overline{Y} = 219 E_Y' + 16
$$

$$
\overline{C_R} = 224 [0.713(E_R' - E_Y')] + 128 = 160(E_R' - E_Y') + 128
$$

$$
\overline{C_B} = 224 [0.564(E_B' - E_Y')] + 128 = 126(E_B' - E_Y') + 128
$$

The quantized code value is the nearest integer.

Equivalent digital-domain approximation via directly quantized primaries:

$$
E'_{R_D} = \lfloor 219 E_R' \rfloor + 16
$$

$$
E'_{G_D} = \lfloor 219 E_G' \rfloor + 16
$$

$$
E'_{B_D} = \lfloor 219 E_B' \rfloor + 16
$$

$$
Y = \frac{77}{256} E'_{R_D} + \frac{150}{256} E'_{G_D} + \frac{29}{256} E'_{B_D}
$$

$$
C_R = \frac{131}{256} E'_{R_D} - \frac{110}{256} E'_{G_D} - \frac{21}{256} E'_{B_D} + 128
$$

$$
C_B = -\frac{44}{256} E'_{R_D} - \frac{87}{256} E'_{G_D} + \frac{131}{256} E'_{B_D} + 128
$$

#### Canonical normalized colour values useful for test vectors

| Colour | $E_R'$ | $E_G'$ | $E_B'$ | $E_Y'$ | $E_R' - E_Y'$ | $E_B' - E_Y'$ |
| --- | --- | --- | --- | --- | --- | --- |
| White | `1.0` | `1.0` | `1.0` | `1.0` | `0` | `0` |
| Black | `0` | `0` | `0` | `0` | `0` | `0` |
| Red | `1.0` | `0` | `0` | `0.299` | `0.701` | `-0.299` |
| Green | `0` | `1.0` | `0` | `0.587` | `-0.587` | `-0.587` |
| Blue | `0` | `0` | `1.0` | `0.114` | `-0.114` | `0.886` |
| Yellow | `1.0` | `1.0` | `0` | `0.886` | `0.114` | `-0.886` |
| Cyan | `0` | `1.0` | `1.0` | `0.701` | `-0.701` | `0.299` |
| Magenta | `1.0` | `0` | `1.0` | `0.413` | `0.587` | `0.587` |

### 3.4 BT.601 Appendix 1 active-line placement

These are the sample-grid values needed to test whether the active picture is placed correctly relative to the analogue horizontal reference `O_H`.

| Item | 525 system | 625 system |
| --- | --- | --- |
| Offset from end of digital active line to `O_H` | `16T` | `12T` |
| Remaining digital blanking after `O_H` | `122T` | `132T` |
| Y sample numbers shown around the boundary | `... 717 718 719 720 721 ... 734 735 736 737 ... 856 857 0 1 2 ...` | `... 717 718 719 720 721 ... 730 731 732 733 ... 862 863 0 1 2 ...` |
| `C_R` sample numbers shown around the boundary | `... 359 360 ... 367 368 ... 428 0 1 ...` | `... 359 360 ... 365 366 ... 431 0 1 ...` |
| `C_B` sample numbers shown around the boundary | `... 359 360 ... 367 368 ... 428 0 1 ...` | `... 359 360 ... 365 366 ... 431 0 1 ...` |

`T` is the luminance sampling period.

If a source-to-EXR workflow uses clean-aperture decoding before writing a full-raster EXR, the validator should additionally check that any horizontal padding preserves the BT.601 active-line placement and does not alter the intended sample-grid position relative to `O_H`.

### 3.5 525/59.94 supporting timing and colorimetry

These are the numeric values a 525-system test harness should use when it reconstructs or evaluates analogue-domain timing.

#### Colorimetry and transfer

| Item | Value |
| --- | --- |
| Red primary | `x=0.630`, `y=0.340` |
| Green primary | `x=0.310`, `y=0.595` |
| Blue primary | `x=0.155`, `y=0.070` |
| Reference white | `D65`, `x=0.3127`, `y=0.3290` |

Reference camera OETF:

$$
V_C = 1.099 L_C^{0.4500} - 0.099 \quad \text{for } 0.018 \leq L_C \leq 1
$$

$$
V_C = 4.500 L_C \quad \text{for } 0 \leq L_C < 0.018
$$

Reference display EOTF:

$$
L_T = \left[ \frac{V_r + 0.099}{1.099} \right]^{1/0.4500} \quad \text{for } 0.0812 \leq V_r \leq 1
$$

$$
L_T = V_r / 4.500 \quad \text{for } 0 \leq V_r < 0.0812
$$

#### Timing and waveform values

| Item | Value |
| --- | --- |
| Subcarrier frequency | `3.579545... MHz ± 10 Hz` |
| Subcarrier drift recommendation | `< 0.1 Hz/s` |
| Subcarrier jitter recommendation | `< 1 ns p-p` over one horizontal line |
| Line frequency | `15,734.265... Hz` |
| Subcarrier cycles per line | `227.5` |
| Field frequency | `59.94005994... Hz` |
| Lines per frame | `525` |
| Interlace | `2:1` |
| Time-coincidence tolerance | `±25 ns` |
| Composite amplitude without chroma | `140 IRE p-p` |
| Maximum composite amplitude with chroma | `171 IRE p-p` |
| White level | `100 IRE ± 1` |
| Black setup | `7.5 IRE ± 1` |
| Blanking level | `0 IRE` |
| Burst amplitude | `40 IRE p-p ± 1` |
| Sync level | `-40 IRE ± 1` |
| Total line period | `63.556 us` |
| Horizontal blanking rise time | `140 ns ± 20 ns` |
| Sync rise time | `140 ns ± 20 ns` |
| Burst envelope rise time | `300 ns`, tolerance `+200 / -100 ns` |
| Blanking start to horizontal reference point | `1.5 us ± 0.1 us` |
| Horizontal sync width | `4.70 us ± 0.10 us` |
| Horizontal reference point to burst start | `19` subcarrier cycles |
| SC/H phase | `0° ± 10°` |
| Horizontal reference point to blanking end | `9.20 us +0.20/-0.10 us` |
| Burst duration | `9 ± 1` cycles |
| Burst half-envelope imbalance | `<= 0.5 IRE` |
| Burst omitted during vertical sync | `9` lines |
| Field period | `16.6833 ms` |
| Frame period | `33.3667 ms` |
| Vertical blanking start before first equalizing pulse | `1.50 us ± 0.10 us` |
| Vertical blanking duration | `20 lines + 1.5 us`, tolerance `±0.1 line / us` |
| Pre-equalizing duration | `3` lines |
| Pre-equalizing pulse width | `2.30 us ± 0.10 us` |
| Vertical sync duration | `3` lines |
| Vertical serration pulse width | `4.70 us ± 0.10 us` |
| Post-equalizing duration | `3` lines |
| Post-equalizing pulse width | `2.30 us ± 0.10 us` |

NTSC chroma filter guidance relevant to reconstructed waveform checks:

- `B-Y`/`R-Y` colour-difference signals must be less than `2 dB` down at `1.3 MHz` and at least `20 dB` down at `3.6 MHz`.
- Historical `Q`-channel guidance was less than `2 dB` at `0.4 MHz`, less than `6 dB` at `0.5 MHz`, and at least `6 dB` at `0.6 MHz`.

### 3.6 625/50 supporting timing and colorimetry

These are the numeric values a 625-system test harness should use.

#### Colorimetry

| Item | Value |
| --- | --- |
| Red primary | `x=0.64`, `y=0.33` |
| Green primary | `x=0.29`, `y=0.60` |
| Blue primary | `x=0.15`, `y=0.06` |
| Reference white | `D65`, `x=0.313`, `y=0.329` |
| Receiver gamma assumption | `2.8` |

#### Timing and waveform values

| Item | Value |
| --- | --- |
| Lines per frame | `625` |
| Field frequency | `50 Hz` |
| Line frequency | `15,625 Hz` |
| Nominal line period | `64 us` |
| Line blanking interval | `12.0 us +0.0/-0.3 us` |
| `O_H` to back edge of line blanking | `10.5 us` |
| Front porch | `1.5 us +0.3/-0.0 us` |
| Sync pulse width | `4.7 us ± 0.2 us` |
| Line blanking edge rise/fall | `0.3 us ± 0.1 us` |
| Sync edge rise/fall | `0.2 us ± 0.1 us` |
| Field period | `20 ms` |
| Field blanking interval | `25H + a` |
| Field-blanking edge rise/fall | `0.3 us ± 0.1 us` |
| Front of field blanking to first equalizing pulse | `3 us ± 2 us` |
| First equalizing sequence duration | `2.5H` |
| Sync sequence duration | `2.5H` |
| Second equalizing sequence duration | `2.5H` |
| Equalizing pulse width | `2.35 us ± 0.1 us` |
| Field sync pulse width | `27.3 us nominal` |
| Interval between field sync pulses | `4.7 us ± 0.2 us` |
| Sync/equalizing edge rise/fall | `0.2 us ± 0.1 us` |

PAL chroma-side values relevant to reconstructed waveform checks:

| Item | Value |
| --- | --- |
| PAL subcarrier frequency | `4.43361875 MHz ± 5 Hz` |
| PAL subcarrier to line relation | $f_{sc} = \left(\frac{1135}{4} + \frac{1}{625}\right) f_H$ |
| `E_U'` attenuation | `< 3 dB at 1.3 MHz` |
| `E_V'` attenuation | `> 20 dB at 4 MHz` |
| Start of burst after `O_H` | `5.6 us ± 0.1 us` |
| Burst duration | `2.25 us ± 0.23 us`, equivalently `10 ± 1` cycles |
| Burst amplitude | `3/7` of white-to-blanking excursion, tolerance `±10%` |
| Burst amplitude for systems D and I | tolerance `±3%` |
| Burst phase | `135°` relative to `E_U'` axis with PAL sign alternation |
| Luma/chroma time-coincidence error | `< 0.05 us` |

### 3.7 BT.601 Appendix 2 filter masks

These are the filter numbers that were missing from the earlier draft. They are derived directly from the published Appendix 2 figure templates and should be implemented as masks, not as single-point spot checks.

#### 13.5 MHz luminance or RGB source filter

Insertion-loss mask from Figure 3a:

- low-loss passband from `0` to `5.75 MHz`
- stopband requirement of `20 dB` at `5.75 MHz`
- alias-region requirement of `12 dB` at `6.75 MHz`
- stopband requirement of `40 dB` from `8 MHz` to `13.5 MHz`

Passband ripple mask from Figure 3b, relative to the value at `1 kHz`:

- suggested theoretical design: about `0.01 dB p-p`
- practical limit across most of the passband: `0.05 dB p-p`
- practical edge limit at `5.75 MHz`: `0.10 dB p-p`

Passband group-delay mask from Figure 3c, relative to the value at `1 kHz`:

- suggested theoretical design: `2 ns p-p` at the low-frequency end, increasing to `4 ns p-p` at `5.75 MHz`
- practical limit: `6 ns p-p` at `5.75 MHz`

#### 6.75 MHz colour-difference source filter

Insertion-loss mask from Figure 4a:

- low-loss passband from `0` to `2.75 MHz`
- stopband requirement of `20 dB` at `2.75 MHz`
- alias-region requirement of `6 dB` at `3.375 MHz`
- stopband requirement of `40 dB` from `4 MHz` to `6.75 MHz`

Passband ripple mask from Figure 4b, relative to the value at `1 kHz`:

- suggested theoretical design: `0.02 dB p-p`
- practical limit: `0.10 dB p-p`

Passband group-delay mask from Figure 4c, relative to the value at `1 kHz`:

- suggested theoretical design: `4 ns p-p` at the low-frequency end, increasing to `8 ns p-p` near `2.75 MHz`
- practical limit: `12 ns p-p` near `2.75 MHz`
- outer stopband step visible in the template: `24 ns p-p`

#### 4:4:4 to 4:2:2 digital chroma-conversion filter

Insertion-loss mask from Figure 5a:

- low-loss passband from `0` to `2.75 MHz`
- stopband requirement of `20 dB` at `2.75 MHz`
- half-amplitude point at `3.375 MHz`
- alias-region requirement of `6 dB` at `3.375 MHz`
- stopband requirement of `40 dB` at `4 MHz`
- stopband rising to `55 dB` by `6.25 MHz`
- the linear-scale amplitude response should be skew-symmetrical about the half-amplitude point

Passband ripple mask from Figure 5b:

- practical and theoretical limits are the same
- visible tolerance is `0.10 dB p-p`
- delay distortion is `0` by design

Appendix 2 notes that the ripple and group-delay masks are defined relative to `1 kHz`, not DC.

### 3.8 Additional implementation constraints from BT.601 Annex 1

- The post-DAC path is assumed to correct the sample-and-hold `sin(x)/x` characteristic.
- Filter, `sin(x)/x` corrector, and delay equalizer should be treated as a single unit when verifying passband tolerances.
- Total delays of luminance and colour-difference paths must be equal.
- The colour-difference filter delay is double the luminance-filter delay.
- Bulk delay equalization should be done in the digital domain in integer multiples of the sampling period.
- Any residual correction must account for an additional decoder sample-and-hold delay of `0.5T`.

### 3.9 What is and is not provable from the EXR alone

#### Directly provable from EXR alone

- container type, channels, sample type, compression, raster, windows, line order, gamma tag, aspect ratio, and frame-rate metadata
- rendered pixel equivalence, if the EXR is compared against a trusted source frame after any declared decode, crop, pad, or colour-conversion steps

#### Provable only with source-aware analysis

- original 4:2:2 chroma co-siting
- placement of the active 720-sample window relative to `O_H`
- compliance with the Appendix 2 insertion-loss, ripple, and group-delay masks
- compliance with path-delay equalization requirements
- proof that the RGB EXR is the correct rendering of BT.601-coded Y/`C_R`/`C_B` rather than some other colour interpretation
- pattern semantics such as multiburst frequency, PLUGE pedestal offsets, ramp linearity, or bar ordering, unless an external pattern oracle is provided

### 3.10 RGB-domain surrogate tests for otherwise non-provable items

Some BT.601 properties cannot be proven exactly once the source has been converted to RGB EXR. They can, however, often be checked indirectly by reconstructing a candidate BT.601 component representation from the RGB pixels and applying a small engineering tolerance. These checks should be reported as `proxy` or `surrogate` results, not as direct proofs of original encoding compliance.

#### General method

1. Convert the EXR RGB values into the intended analysis domain.
	If the EXR stores linear RGB, first apply the declared or assumed OETF to obtain gamma-pre-corrected `R'G'B'`.
2. Compute reconstructed `Y'`, `C_R`, and `C_B` using the BT.601 equations in section 3.3.
3. If needed, form a synthetic 4:2:2 representation by applying a BT.601-compliant or near-compliant chroma low-pass filter and 2:1 horizontal chroma subsampling.
4. Measure legality, alignment, spectral occupancy, and round-trip residuals in that reconstructed domain.
5. Mark the result as a `surrogate consistency check`, not as proof of the original pre-RGB signal path.

#### Surrogate checks that are usually worth doing

| Non-provable exact item | RGB-domain surrogate check | What it tells you | What it cannot tell you |
| --- | --- | --- | --- |
| Original 4:2:2 chroma co-siting | Reconstruct `Y'`, `C_R`, `C_B`, resample to 4:2:2 with odd-sample co-siting, upsample back to 4:4:4, and compare to the analysed RGB image | Whether the RGB image is consistent with a BT.601-style 4:2:2 representation | Whether the original source actually used that co-siting before RGB conversion |
| Active-line placement relative to `O_H` | Reconstruct luma, find edge positions or active-picture boundaries, and compare them with the Appendix 1 sample-grid model | Whether the visible image content is positioned consistently with the expected BT.601 active window | Whether the original analogue sync reference and digital sampling phase were correct |
| Appendix 2 filter compliance | Measure the spectra of reconstructed `Y'`, `C_R`, and `C_B`, and compare with the BT.601 masks | Whether the RGB image is consistent with a BT.601-band-limited component signal | Whether the original anti-alias filter itself met the exact template before conversion |
| Path-delay equalization | Compare reconstructed luma and chroma edge centroids or phase delay on strong transitions | Whether there is visible luma/chroma misregistration in the RGB result | Whether the original encoder and decoder delays were equalized per Annex 1 |
| Correct BT.601 colour interpretation | Convert RGB to reconstructed BT.601 `Y'CbCr`, then round-trip back to RGB and measure residual error | Whether the EXR is numerically consistent with a BT.601 interpretation | Whether another colour interpretation could also fit nearly as well |

#### Recommended engineering tolerances for surrogate tests

These tolerances are practical test-design values. They are not normative BT.601 limits; they are intended to avoid false failures caused by rounding, interpolation, or RGB-domain conversion loss.

In this section, `8-bit-equivalent` means that an error measured in float RGB or reconstructed component space is expressed on the scale of a single 8-bit channel or component code value, that is, one least-significant step on a `0` to `255` scale. It does not imply that the EXR itself stores 8-bit RGB pixels.

| Surrogate test | Suggested tolerance |
| --- | --- |
| Reconstructed Y'/Cb/Cr legal-range boundary checks | allow `±1` code in 8-bit-equivalent space at the decision boundary |
| RGB -> reconstructed BT.601 -> RGB round-trip residual | target mean error `<= 1` 8-bit-equivalent code per channel and worst-case error `<= 2` codes for ordinary picture content |
| Reconstructed chroma co-siting consistency | target reconstructed-vs-original error `<= 1` 8-bit-equivalent code over flat areas and small average error around clean edges |
| Reconstructed luma/chroma edge alignment | target relative edge displacement `<= 0.25` luma sample for synthetic edges and `<= 0.5` sample for natural-image features |
| Spectral mask comparison in reconstructed component space | allow a small measurement margin, typically `0.1 dB` amplitude or the equivalent of one analysis-bin interpolation uncertainty, whichever is larger |

#### Recommended reporting language

If these RGB-domain checks pass, the script should say something like:

- `The EXR is consistent with a BT.601 interpretation under reconstructed-component analysis.`
- `No RGB-domain evidence of illegal BT.601 range, luma/chroma misregistration, or excessive out-of-band energy was found.`

It should not say:

- `The original source is proven to have been encoded with correct BT.601 chroma co-siting.`
- `The original anti-alias filters are proven to satisfy Appendix 2.`

The distinction is:

- direct proof: properties preserved in the file or directly observable from a source-domain representation
- surrogate consistency: properties inferred from reconstructed BT.601 maths applied to RGB with a tolerance allowance

## 4. Minimum script outputs

To be useful, the verification script should report at least these result groups:

- `container`: EXR header and metadata checks.
- `raster`: dimensions, aspect ratio, frame-rate metadata, windows, line order.
- `content-equivalence`: whether the EXR matches the declared source rendering after any stated decode, crop, pad, or colour-conversion steps.
- `bt601-derived-codes`: reconstructed Y', Cb, Cr ranges, legal-code occupancy, and out-of-gamut findings.
- `rgb-surrogate-results`: indirect RGB-domain consistency checks for co-siting, timing, filter behaviour, and BT.601 round-trip residuals.
- `timing-model`: whether the file is being interpreted as 525/59.94 or 625/50 and which BT.601 timing constants were applied.
- `filter-mask-results`: pass/fail against the Appendix 2 insertion-loss, ripple, and group-delay masks when source-domain analysis is available.
- `non-provable-from-exr`: optional summary of BT.601 clauses the script cannot prove from the EXR alone; this may be omitted to keep routine validator output focused on actionable checks.

## 5. Practical conclusion

If you want to claim `EXR is structurally correct and render-faithful`, the EXR plus source-MOV comparison is sufficient.

If you want to claim `EXR is fully compliant with BT.601`, the test system must additionally:

- reconstruct BT.601 Y'CbCr from the RGB EXR;
- evaluate legal ranges and gamut limits using BT.601 equations;
- check source-domain or reconstructed-domain timing against Appendix 1; and
- measure filter magnitude, ripple, and group delay against Appendix 2 templates.

Without those source-coupled checks, the strongest honest claim is: `the EXR is a structurally correct EXR deliverable that faithfully renders a BT.601-derived source`, not `the EXR alone proves full BT.601 compliance`.