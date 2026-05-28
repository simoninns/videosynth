# VideoSynth Software-Generated Pattern Specification

## 1. Scope

This document defines software-generated frame patterns for `sections[].type: software_generated`.

Pattern naming and behavior are split by output format because PAL and NTSC have different frame geometry and analogue interpretation requirements.

## 2. Available Patterns

### 2.1 PAL Pattern Set

Valid only when `video_standard_preset: PAL`:

- `pal_ebu_colour_bars_100`
- `pal_ebu_colour_bars_75`
- `pal_linear_grayscale_ramp_horizontal`
- `pal_linear_grayscale_ramp_vertical`
- `pal_luma_checkerboard_8x8`
- `pal_luma_checkerboard_16x16`
- `pal_full_field_black`
- `pal_full_field_white`
- `pal_pluge_5patch_near_black`
- `pal_crosshatch_visible_area_grid`

### 2.2 NTSC Pattern Set

Valid only when `video_standard_preset: NTSC`:

- `ntsc_smpte_170m_colour_bars_100`
- `ntsc_smpte_170m_colour_bars_75`
- `ntsc_linear_grayscale_ramp_horizontal`
- `ntsc_linear_grayscale_ramp_vertical`
- `ntsc_luma_checkerboard_8x8`
- `ntsc_luma_checkerboard_16x16`
- `ntsc_full_field_black`
- `ntsc_full_field_white`
- `ntsc_pluge_5patch_near_black`
- `ntsc_crosshatch_visible_area_grid`

### 2.3 Validation Rules

- PAL projects must reject pattern names not prefixed with `pal_`.
- NTSC projects must reject pattern names not prefixed with `ntsc_`.
- Both PAL and NTSC sets must expose two colour-bar saturation variants: `*_colour_bars_100` and `*_colour_bars_75`.

## 3. Generic Geometry and Value Primitives

### 3.1 Raster and Coordinates

- PAL raster: `W=720`, `H=576`.
- NTSC raster: `W=720`, `H=480`.
- Pixel coordinates are zero-based:
  - `x in [0, W-1]`
  - `y in [0, H-1]`

### 3.2 Visible Active Aperture

- Frame-based sources use the fixed rasters in Section 3.1, but picture content is confined to the visible active aperture.
- Pixels outside the visible active aperture must remain nominal black `(64,512,512)` and must not be modified by software-generated patterns or ingested progressive sources.

PAL aperture derivation:

- ITU-R BT.1700 Table 1 item `1a`: `576` active lines.
- ITU-R BT.1700 Table 2: line period `64.0 us`, line blanking `12.0 us`, so analogue active line duration is `52.0 us`.
- Under the shared ITU-R BT.601 `13.5 MHz` digital sampling model, `52.0 us * 13.5 MHz = 702` visible pixels.
- PAL visible aperture is therefore:
  - `AX=9`
  - `AY=0`
  - `AW=702`
  - `AH=576`
  - visible coordinates `x in [9,710]`, `y in [0,575]`

NTSC aperture derivation:

- SMPTE 170M-2004 analogue timing yields an NTSC active picture interval of approximately `52.666 us`.
- Under the shared ITU-R BT.601 `13.5 MHz` digital sampling model, `52.666 us * 13.5 MHz = 711` visible pixels.
- VideoSynth frame-based sources expose the `480` full active lines used by the timing model; the three partly active analogue transition lines are not addressable in the frame-source raster.
- NTSC visible aperture is therefore:
  - `AX=4`
  - `AY=0`
  - `AW=711`
  - `AH=480`
  - visible coordinates `x in [4,714]`, `y in [0,479]`

### 3.3 Output Colour-Space Contract

- 10-bit 4:4:4 YCbCr (BT.601 coding domain).
- Nominal luma range: `64..940`.
- Chroma range: `64..960`.
- Neutral chroma: `Cb=512`, `Cr=512`.

PLUGE exception:

- Below-black patches intentionally use `Y=48`.

### 3.4 Shared Luma/Chroma Constants

| Symbol | Y | Cb | Cr | Meaning |
| --- | ---: | ---: | ---: | --- |
| `BLACK` | 64 | 512 | 512 | nominal black |
| `WHITE` | 940 | 512 | 512 | nominal white |
| `BELOW_BLACK` | 48 | 512 | 512 | PLUGE below-black |
| `ABOVE_BLACK` | 80 | 512 | 512 | PLUGE above-black |

### 3.5 Colour-Bar Triplets

#### 3.4.1 100% Bar Triplets

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

#### 3.4.2 75% Bar Triplets

| Colour | Y | Cb | Cr |
| --- | ---: | ---: | ---: |
| White | 940 | 512 | 512 |
| Yellow | 648 | 176 | 568 |
| Cyan | 524 | 624 | 176 |
| Green | 448 | 288 | 232 |
| Magenta | 336 | 736 | 792 |
| Red | 260 | 400 | 848 |
| Blue | 140 | 848 | 456 |
| Black | 64 | 512 | 512 |

### 3.6 Standard 8-Bar Geometry

Used by PAL colour-bar patterns.

- Draw only inside the current standard's visible aperture from Section 3.2.
- 8 equal vertical bars spanning aperture width.
- Bar index: `i = min(7, floor((x - AX) * 8 / AW))`.
- Bar order: white, yellow, cyan, green, magenta, red, blue, black.

### 3.7 Standard Grayscale Geometry

Horizontal ramp:

- Draw only inside the visible aperture from Section 3.2.
- `Y(x,y)=64 + round((940-64) * (x-AX) / (AW-1))`, `Cb=Cr=512`.

Vertical ramp:

- `Y(x,y)=64 + round((940-64) * (y-AY) / (AH-1))`, `Cb=Cr=512`.

### 3.8 Standard Checkerboard Geometry

Tile size `T`:

- Draw only inside the visible aperture from Section 3.2.
- `tile=(floor((x-AX)/T) + floor((y-AY)/T)) mod 2`
- If `tile==0`: `(Y,Cb,Cr)=(940,512,512)`
- Else: `(Y,Cb,Cr)=(64,512,512)`

### 3.9 Standard Full-Field Geometry

- Outside the visible aperture, pixels remain `(64,512,512)`.
- `full_field_black`: visible aperture pixels `(64,512,512)`.
- `full_field_white`: visible aperture pixels `(940,512,512)`.

### 3.10 Boundary-Aligned Crosshatch Geometry

- Base field black.
- Grid lines are chosen so spacing is an integer divisor of the visible aperture dimensions.
- Grid starts at the visible-aperture origin and ends on the visible-aperture boundary.
- No additional center cross is applied; the center intersection occurs naturally.

PAL aligned grid primitive:

- Visible aperture `702x576` from Section 3.2.
- Horizontal spacing `SX=78` (since `702/78=9`).
- Vertical spacing `SY=72` (since `576/72=8`).
- 1-pixel white grid lines at:
  - `x = AX + n*78` for integer `n >= 0` while `x < AX+AW`
  - `y = AY + n*72` for integer `n >= 0` while `y < AY+AH`
- Aperture closure lines:
  - `x = AX + AW - 1`
  - `y = AY + AH - 1`

NTSC aligned grid primitive:

- Visible aperture `711x480` from Section 3.2.
- Horizontal spacing `SX=79` (since `711/79=9`).
- Vertical spacing `SY=60` (since `480/60=8`).
- 1-pixel white grid lines at:
  - `x = AX + n*79` for integer `n >= 0` while `x < AX+AW`
  - `y = AY + n*60` for integer `n >= 0` while `y < AY+AH`
- Aperture closure lines:
  - `x = AX + AW - 1`
  - `y = AY + AH - 1`

### 3.11 Standard PLUGE Geometry

- Base field black.
- PLUGE window:
  - `x0=AX + round(0.20*AW)` to `x1=AX + round(0.80*AW)-1`
  - `y0=AY + round(0.75*AH)` to `y1=AY + round(0.875*AH)-1`
- Window split into 5 equal vertical patches (left-to-right precedence for remainder).
- Patch sequence (left to right):
  1. `BLACK` (anchor)
  2. `BELOW_BLACK`
  3. `BLACK`
  4. `ABOVE_BLACK`
  5. `BLACK` (anchor)

PLUGE level intent:

- NTSC relationship target: `0 / 3.5 / 7.5 / 11.5 / 0 IRE`.
- PAL relationship target: `0 / -4 / 0 / +4 / 0 IRE`.

### 3.12 NTSC SMPTE Multi-Region Geometry Primitive

Used by both NTSC SMPTE colour-bar patterns.

- Three vertical bands:
  - Draw only inside the NTSC visible aperture from Section 3.2.
  - Band A (top): `y in [AY, AY + round((2.0/3.0)*AH)-1]`
  - Band B (middle): `y in [AY + round((2.0/3.0)*AH), AY + round((3.0/4.0)*AH)-1]`
  - Band C (bottom): `y in [AY + round((3.0/4.0)*AH), AY+AH-1]`

Band A:

- Seven equal-width bars.
- Order: white/gray, yellow, cyan, green, magenta, red, blue.

Band B:

- Seven equal-width castellations.
- Order: blue, black, magenta, black, cyan, black, white/gray.

Band C:

- Contains, left-to-right, four functional blocks:
  1. `-I` patch
  2. white reference patch
  3. `+Q` patch
  4. black reference region containing PLUGE

## 4. PAL Pattern Definitions

### 4.1 `pal_ebu_colour_bars_100`

- Use geometry from Section 3.5.
- Use 100% triplets from Section 3.4.1.

### 4.2 `pal_ebu_colour_bars_75`

- Use geometry from Section 3.5.
- Use 75% triplets from Section 3.4.2.

### 4.3 `pal_linear_grayscale_ramp_horizontal`

- Use Section 3.6 horizontal ramp.

### 4.4 `pal_linear_grayscale_ramp_vertical`

- Use Section 3.6 vertical ramp.

### 4.5 `pal_luma_checkerboard_8x8`

- Use Section 3.7 with `T=8`.

### 4.6 `pal_luma_checkerboard_16x16`

- Use Section 3.7 with `T=16`.

### 4.7 `pal_full_field_black`

- Use Section 3.8 black field.

### 4.8 `pal_full_field_white`

- Use Section 3.8 white field.

### 4.9 `pal_pluge_5patch_near_black`

- Use Section 3.10.
- Must map to PAL PLUGE intent in Section 3.10.
- Represents a five-patch anchored PLUGE strip.

### 4.10 `pal_crosshatch_visible_area_grid`

- Use PAL primitive from Section 3.9.

## 5. NTSC Pattern Definitions

### 5.1 `ntsc_smpte_170m_colour_bars_75`

- Use multi-region geometry from Section 3.11.
- Band A and coloured Band B cells use 75% triplets from Section 3.4.2.
- Black cells use `BLACK`.
- Band C block requirements:
  - `-I` patch value: `(Y,Cb,Cr)=(244,612,395)`.
  - White patch value: `(Y,Cb,Cr)=(940,512,512)`.
  - `+Q` patch value: `(Y,Cb,Cr)=(141,697,606)`.
  - Black region uses `BLACK` and contains PLUGE from Section 3.10.

### 5.2 `ntsc_smpte_170m_colour_bars_100`

- Use multi-region geometry from Section 3.11.
- Band A and coloured Band B cells use 100% triplets from Section 3.4.1.
- Black cells use `BLACK`.
- Band C block requirements are identical to Section 5.1.

### 5.3 `ntsc_linear_grayscale_ramp_horizontal`

- Use Section 3.6 horizontal ramp.

### 5.4 `ntsc_linear_grayscale_ramp_vertical`

- Use Section 3.6 vertical ramp.

### 5.5 `ntsc_luma_checkerboard_8x8`

- Use Section 3.7 with `T=8`.

### 5.6 `ntsc_luma_checkerboard_16x16`

- Use Section 3.7 with `T=16`.

### 5.7 `ntsc_full_field_black`

- Use Section 3.8 black field.

### 5.8 `ntsc_full_field_white`

- Use Section 3.8 white field.

### 5.9 `ntsc_pluge_5patch_near_black`

- Use Section 3.10.
- Must map to NTSC PLUGE intent in Section 3.10.
- Represents a five-patch anchored PLUGE strip.

### 5.10 `ntsc_crosshatch_visible_area_grid`

- Use NTSC primitive from Section 3.9.

## 6. Determinism and Rounding

- All intermediate computations use `double`.
- `round()` means round-half-away-from-zero.
- Clamp final codes before storage:
  - `Y` to `[48,940]`
  - `Cb/Cr` to `[64,960]`

## 7. Relationship to HLD

Pattern names in this document are authoritative for the `pattern` field in software-generated sections of the high-level design.
