# VideoSynth Software-Generated Pattern Specification

## 1. Scope

This document defines the exact software-generated frame patterns for `sections[].type: software_generated`.

The definitions are self-contained and implementation-ready. No external standard is required to implement the pixel output once this document is available.

## 2. Normative Basis

The pattern set and level conventions are aligned to the project's normative references:

- ITU-R BT.601-5: component coding model used by the generator interface.
- SMPTE 170M-2004: NTSC colour-bar amplitude families (`100/7.5/100/7.5`).
- EBU Tech. 3280-E: PAL 4fsc digital representation and `100/0/100/0` colour-bar framing.
- ITU-R BT.1700 and SMPTE 170M-2004: PAL/NTSC analogue level interpretation in the downstream encoder.

## 3. Common Rules

### 3.1 Raster and Coordinates

- PAL frame raster: `W = 720`, `H = 576`.
- NTSC frame raster: `W = 720`, `H = 480`.
- Pixel coordinates are zero-based:
  - horizontal index `x in [0, W-1]`
  - vertical index `y in [0, H-1]`

### 3.2 Output Colour Space Contract

All patterns output per-pixel values in:

- 10-bit 4:4:4 YCbCr (BT.601 studio swing)
- Y range: `64..940`
- Cb/Cr range: `64..960`
- Neutral chroma: `Cb = 512`, `Cr = 512`

### 3.3 Constant Values

Luma/chroma constants used by multiple patterns:

| Symbol | Y | Cb | Cr | Meaning |
| --- | ---: | ---: | ---: | --- |
| `BLACK` | 64 | 512 | 512 | nominal black |
| `WHITE` | 940 | 512 | 512 | nominal white |
| `BELOW_BLACK` | 48 | 512 | 512 | PLUGE below-black bar |
| `ABOVE_BLACK` | 80 | 512 | 512 | PLUGE above-black bar |

### 3.4 100% Colour Primaries (BT.601 10-bit)

For colour-bar patterns, use these exact triplets:

| Colour | Y | Cb | Cr |
| --- | ---: | ---: | ---: |
| White | 940 | 512 | 512 |
| Yellow | 840 | 64 | 585 |
| Cyan | 678 | 663 | 64 |
| Green | 578 | 215 | 137 |
| Magenta | 426 | 809 | 887 |
| Red | 326 | 361 | 960 |
| Blue | 164 | 960 | 439 |
| Black | 64 | 512 | 512 |

## 4. Pattern Registry

The following names are the only valid software-generated pattern identifiers in this specification.

| Pattern Name | Standard Intent | Valid For |
| --- | --- | --- |
| `smpte_170m_100_7_5_100_7_5_colour_bars` | SMPTE 170M `100/7.5/100/7.5` family | NTSC projects |
| `ebu_tech_3280_100_0_100_0_colour_bars` | EBU Tech. 3280-E `100/0/100/0` family | PAL projects |
| `linear_grayscale_ramp_horizontal` | deterministic engineering ramp | PAL and NTSC |
| `linear_grayscale_ramp_vertical` | deterministic engineering ramp | PAL and NTSC |
| `luma_checkerboard_8x8` | deterministic engineering checkerboard | PAL and NTSC |
| `luma_checkerboard_16x16` | deterministic engineering checkerboard | PAL and NTSC |
| `full_field_black` | reference black field | PAL and NTSC |
| `full_field_white` | reference white field | PAL and NTSC |
| `pluge_3bar_near_black` | near-black PLUGE triplet | PAL and NTSC |
| `crosshatch_75_grid` | geometric alignment grid | PAL and NTSC |

Validation rule:

- `smpte_170m_100_7_5_100_7_5_colour_bars` must be rejected for PAL projects.
- `ebu_tech_3280_100_0_100_0_colour_bars` must be rejected for NTSC projects.

## 5. Pattern Definitions

### 5.1 `smpte_170m_100_7_5_100_7_5_colour_bars`

Geometry:

- Full frame height.
- 8 equal vertical bars.
- Bar width `BW = W / 8 = 90` pixels.

Left-to-right bar order:

1. White
2. Yellow
3. Cyan
4. Green
5. Magenta
6. Red
7. Blue
8. Black

Pixel assignment:

- `i = min(7, floor(x / BW))`
- Pixel `(x,y)` receives the YCbCr triplet for bar `i` from Section 3.4.

### 5.2 `ebu_tech_3280_100_0_100_0_colour_bars`

Geometry and bar order are identical to Section 5.1.

Pixel assignment is identical to Section 5.1.

Interpretation note:

- This pattern is intended for PAL projects where downstream analogue mapping is `100/0/100/0`.

### 5.3 `linear_grayscale_ramp_horizontal`

Geometry:

- Full-frame horizontal luma ramp.

Pixel assignment:

- `Y(x,y) = 64 + round((940 - 64) * x / (W - 1))`
- `Cb(x,y) = 512`
- `Cr(x,y) = 512`

Endpoints:

- Leftmost column (`x=0`) is black (`Y=64`).
- Rightmost column (`x=W-1`) is white (`Y=940`).

### 5.4 `linear_grayscale_ramp_vertical`

Geometry:

- Full-frame vertical luma ramp.

Pixel assignment:

- `Y(x,y) = 64 + round((940 - 64) * y / (H - 1))`
- `Cb(x,y) = 512`
- `Cr(x,y) = 512`

Endpoints:

- Top row (`y=0`) is black (`Y=64`).
- Bottom row (`y=H-1`) is white (`Y=940`).

### 5.5 `luma_checkerboard_8x8`

Geometry:

- Tile size `T = 8` pixels.

Pixel assignment:

- `tile = (floor(x / T) + floor(y / T)) mod 2`
- If `tile == 0`: `(Y,Cb,Cr) = (940,512,512)`
- Else: `(Y,Cb,Cr) = (64,512,512)`

### 5.6 `luma_checkerboard_16x16`

Geometry:

- Tile size `T = 16` pixels.

Pixel assignment:

- `tile = (floor(x / T) + floor(y / T)) mod 2`
- If `tile == 0`: `(Y,Cb,Cr) = (940,512,512)`
- Else: `(Y,Cb,Cr) = (64,512,512)`

### 5.7 `full_field_black`

Pixel assignment for every `(x,y)`:

- `(Y,Cb,Cr) = (64,512,512)`

### 5.8 `full_field_white`

Pixel assignment for every `(x,y)`:

- `(Y,Cb,Cr) = (940,512,512)`

### 5.9 `pluge_3bar_near_black`

Purpose:

- Provide below-black, black, and above-black near-reference bars for setup/brightness calibration checks.

Base field:

- Entire frame initialized to `BLACK` (`64,512,512`).

PLUGE window geometry:

- Horizontal span: from `x0 = round(0.20 * W)` to `x1 = round(0.80 * W) - 1`.
- Vertical span: from `y0 = round(0.75 * H)` to `y1 = round(0.875 * H) - 1`.
- Window width is split into 3 equal bars using integer partition with left-to-right precedence.

Bar levels (left to right):

1. `BELOW_BLACK` (`Y=48`)
2. `BLACK` (`Y=64`)
3. `ABOVE_BLACK` (`Y=80`)

For all three bars: `Cb=512`, `Cr=512`.

### 5.10 `crosshatch_75_grid`

Purpose:

- Geometric alignment and linearity check.

Base field:

- Entire frame initialized to black (`64,512,512`).

Grid definition:

- Vertical grid lines at all `x` such that `x mod 75 == 0`.
- Horizontal grid lines at all `y` such that `y mod 75 == 0`.
- Grid line colour: white (`940,512,512`).
- Grid line width: 1 pixel.

Center emphasis:

- Add center vertical line at `x = floor(W/2)` and `x = floor(W/2)-1`.
- Add center horizontal line at `y = floor(H/2)` and `y = floor(H/2)-1`.
- Center line colour: white (`940,512,512`).

## 6. Determinism and Rounding

To guarantee reproducibility across platforms:

- All intermediate computations use `double`.
- `round()` means round-half-away-from-zero.
- Clamp final Y/Cb/Cr outputs to legal ranges before storage:
  - `Y` to `[64,940]`
  - `Cb/Cr` to `[64,960]`

## 7. Relationship to HLD

The pattern names in this document are the authoritative names for the `pattern` field in software-generated sections of the high-level design.
