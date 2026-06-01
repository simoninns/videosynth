# Phase 2 Asset Manifest and Fixture Mapping

Captured (UTC): 2026-06-01

## Goal

Define a deterministic mapping from current fixture sections under tests/projects to replacement assets in videosynth-assets/assets, and identify any corpus gaps that block direct one-to-one replacement.

## Path Policy (Normative)

- Canonical asset paths are repository-relative and rooted at videosynth-assets/assets.
- Progressive EXR paths must use:
  - videosynth-assets/assets/exr/720x576/... for PAL fixtures.
  - videosynth-assets/assets/exr/720x486/... for NTSC fixtures.
- Progressive MKV paths must use:
  - videosynth-assets/assets/mkv/720x576/... for PAL fixtures.
  - videosynth-assets/assets/mkv/720x486/... for NTSC fixtures.
- No fixture may reference resources/assets.
- No fallback path policy is permitted.

## Asset Manifest

### EXR Corpus

PAL EXR set (720x576), 19 files:

- 100_BARS.exr
- 75_BARS.exr
- 75_BARS_RED.exr
- CHROMA_RAMP.exr
- FULL_RAMP.exr
- GREY_10H_STEP.exr
- GREY_10V_STEP.exr
- GREY_5H_STEP.exr
- GREY_5V_STEP.exr
- LEGAL_RAMP.exr
- LUMA_RAMP.exr
- LUMA_RAMP_DOWN.exr
- MULTIBURST.exr
- PLUGE.exr
- SMPTE_BARS.exr
- TARTAN.exr
- VALID_RAMPS.exr
- VERT_LUMA_RAMP.exr
- Y_CB_CR_RAMPS.exr

NTSC EXR set (720x486), 19 files:

- 100_BARS.exr
- 75_BARS.exr
- 75_BARS_RED.exr
- CHROMA_RAMP.exr
- FULL_RAMP.exr
- GREY_10H_STEP.exr
- GREY_10V_STEP.exr
- GREY_5H_STEP.exr
- GREY_5V_STEP.exr
- LEGAL_RAMP.exr
- LUMA_RAMP.exr
- LUMA_RAMP_DOWN.exr
- MULTIBURST.exr
- PLUGE.exr
- SMPTE_BARS_001.exr
- TARTAN.exr
- VALID_RAMPS.exr
- VERT_LUMA_RAMP.exr
- Y_CB_CR_RAMPS.exr

### MKV Corpus and Metadata

| File | Codec | Pixel format | Width | Height | Field order | Frame rate | Matrix | Primaries | Transfer | Range |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| videosynth-assets/assets/mkv/720x576/MOVING_ZONE_2H.mkv | ffv1 | yuv422p10le | 720 | 576 | tb | 25/1 | smpte170m | bt470bg | bt709 | tv |
| videosynth-assets/assets/mkv/720x486/MOVING_ZONE_2H.mkv | ffv1 | yuv422p10le | 720 | 486 | bt | 30000/1001 | smpte170m | smpte170m | bt709 | tv |

## Fixture Mapping

### tests/projects/pal_progressive_exr.yaml

| Existing section | Existing source | Replacement source | Status |
| --- | --- | --- | --- |
| PalExr100Bars | resources/assets/720x576/stills/exr/100_BARS.exr | videosynth-assets/assets/exr/720x576/100_BARS.exr | mapped |
| PalExr75Bars | resources/assets/720x576/stills/exr/75_BARS.exr | videosynth-assets/assets/exr/720x576/75_BARS.exr | mapped |
| PalExr75BarsRed | resources/assets/720x576/stills/exr/75_BARS_RED.exr | videosynth-assets/assets/exr/720x576/75_BARS_RED.exr | mapped |
| PalExrChromaRamp | resources/assets/720x576/stills/exr/CHROMA_RAMP.exr | videosynth-assets/assets/exr/720x576/CHROMA_RAMP.exr | mapped |
| PalExrFullRamp | resources/assets/720x576/stills/exr/FULL_RAMP.exr | videosynth-assets/assets/exr/720x576/FULL_RAMP.exr | mapped |
| PalExrGrey10HStep | resources/assets/720x576/stills/exr/GREY_10H_STEP.exr | videosynth-assets/assets/exr/720x576/GREY_10H_STEP.exr | mapped |
| PalExrLumaRamp | resources/assets/720x576/stills/exr/LUMA_RAMP.exr | videosynth-assets/assets/exr/720x576/LUMA_RAMP.exr | mapped |
| PalExrGrey10V | resources/assets/720x576/stills/exr/GREY_10V_STEP.exr | videosynth-assets/assets/exr/720x576/GREY_10V_STEP.exr | mapped |
| PalExrGrey5HStep | resources/assets/720x576/stills/exr/GREY_5H_STEP.exr | videosynth-assets/assets/exr/720x576/GREY_5H_STEP.exr | mapped |
| PalExrGrey5VStep | resources/assets/720x576/stills/exr/GREY_5V_STEP.exr | videosynth-assets/assets/exr/720x576/GREY_5V_STEP.exr | mapped |
| PalExrLegalRamp | resources/assets/720x576/stills/exr/LEGAL_RAMP.exr | videosynth-assets/assets/exr/720x576/LEGAL_RAMP.exr | mapped |
| PalExrLumaRampDown | resources/assets/720x576/stills/exr/LUMA_RAMP_DOWN.exr | videosynth-assets/assets/exr/720x576/LUMA_RAMP_DOWN.exr | mapped |
| PalExrMultiburst | resources/assets/720x576/stills/exr/MULTIBURST.exr | videosynth-assets/assets/exr/720x576/MULTIBURST.exr | mapped |
| PalExrPluge | resources/assets/720x576/stills/exr/PLUGE.exr | videosynth-assets/assets/exr/720x576/PLUGE.exr | mapped |
| PalExrSmpteBars | resources/assets/720x576/stills/exr/SMPTE_BARS.exr | videosynth-assets/assets/exr/720x576/SMPTE_BARS.exr | mapped |
| PalExrTartan | resources/assets/720x576/stills/exr/TARTAN.exr | videosynth-assets/assets/exr/720x576/TARTAN.exr | mapped |
| PalExrValidRamps | resources/assets/720x576/stills/exr/VALID_RAMPS.exr | videosynth-assets/assets/exr/720x576/VALID_RAMPS.exr | mapped |
| PalExrVertLumaRamp | resources/assets/720x576/stills/exr/VERT_LUMA_RAMP.exr | videosynth-assets/assets/exr/720x576/VERT_LUMA_RAMP.exr | mapped |
| PalExrYCbCrRamps | resources/assets/720x576/stills/exr/Y_CB_CR_RAMPS.exr | videosynth-assets/assets/exr/720x576/Y_CB_CR_RAMPS.exr | mapped |
| PalTestcardF | resources/assets/720x576/stills/exr/testcard-f-bt601-studio.exr | none available in submodule corpus | gap |

### tests/projects/ntsc_progressive_exr.yaml

| Existing section | Existing source | Replacement source | Status |
| --- | --- | --- | --- |
| NtscExr100Bars | resources/assets/720x480/stills/exr/100_BARS.exr | videosynth-assets/assets/exr/720x486/100_BARS.exr | mapped |
| NtscExr75Bars | resources/assets/720x480/stills/exr/75_BARS.exr | videosynth-assets/assets/exr/720x486/75_BARS.exr | mapped |
| NtscExr75BarsRed | resources/assets/720x480/stills/exr/75_BARS_RED.exr | videosynth-assets/assets/exr/720x486/75_BARS_RED.exr | mapped |
| NtscExrChromaRamp | resources/assets/720x480/stills/exr/CHROMA_RAMP.exr | videosynth-assets/assets/exr/720x486/CHROMA_RAMP.exr | mapped |
| NtscExrFullRamp | resources/assets/720x480/stills/exr/FULL_RAMP.exr | videosynth-assets/assets/exr/720x486/FULL_RAMP.exr | mapped |
| NtscExrGrey10HStep | resources/assets/720x480/stills/exr/GREY_10H_STEP.exr | videosynth-assets/assets/exr/720x486/GREY_10H_STEP.exr | mapped |
| NtscExrLumaRamp | resources/assets/720x480/stills/exr/LUMA_RAMP.exr | videosynth-assets/assets/exr/720x486/LUMA_RAMP.exr | mapped |
| NtscExrGrey10V | resources/assets/720x480/stills/exr/GREY_10V_STEP.exr | videosynth-assets/assets/exr/720x486/GREY_10V_STEP.exr | mapped |
| NtscExrGrey5HStep | resources/assets/720x480/stills/exr/GREY_5H_STEP.exr | videosynth-assets/assets/exr/720x486/GREY_5H_STEP.exr | mapped |
| NtscExrGrey5VStep | resources/assets/720x480/stills/exr/GREY_5V_STEP.exr | videosynth-assets/assets/exr/720x486/GREY_5V_STEP.exr | mapped |
| NtscExrLegalRamp | resources/assets/720x480/stills/exr/LEGAL_RAMP.exr | videosynth-assets/assets/exr/720x486/LEGAL_RAMP.exr | mapped |
| NtscExrLumaRampDown | resources/assets/720x480/stills/exr/LUMA_RAMP_DOWN.exr | videosynth-assets/assets/exr/720x486/LUMA_RAMP_DOWN.exr | mapped |
| NtscExrMultiburst | resources/assets/720x480/stills/exr/MULTIBURST.exr | videosynth-assets/assets/exr/720x486/MULTIBURST.exr | mapped |
| NtscExrPluge | resources/assets/720x480/stills/exr/PLUGE.exr | videosynth-assets/assets/exr/720x486/PLUGE.exr | mapped |
| NtscExrSmpteBars | resources/assets/720x480/stills/exr/SMPTE_BARS_001.exr | videosynth-assets/assets/exr/720x486/SMPTE_BARS_001.exr | mapped |
| NtscExrTartan | resources/assets/720x480/stills/exr/TARTAN.exr | videosynth-assets/assets/exr/720x486/TARTAN.exr | mapped |
| NtscExrValidRamps | resources/assets/720x480/stills/exr/VALID_RAMPS.exr | videosynth-assets/assets/exr/720x486/VALID_RAMPS.exr | mapped |
| NtscExrVertLumaRamp | resources/assets/720x480/stills/exr/VERT_LUMA_RAMP.exr | videosynth-assets/assets/exr/720x486/VERT_LUMA_RAMP.exr | mapped |
| NtscExrYCbCrRamps | resources/assets/720x480/stills/exr/Y_CB_CR_RAMPS.exr | videosynth-assets/assets/exr/720x486/Y_CB_CR_RAMPS.exr | mapped |

### tests/projects/pal_progressive_mov.yaml

| Existing section | Existing source | Replacement source | Status |
| --- | --- | --- | --- |
| PalMovMovingZone | resources/assets/720x576/video/mov_25_00/Moving-Zone-2H.mov | videosynth-assets/assets/mkv/720x576/MOVING_ZONE_2H.mkv | mapped |
| PalMovPluge | resources/assets/720x576/video/mov_25_00/PLUGE.mov | none available in submodule MKV corpus | gap |
| PalMovPt5300 | resources/assets/720x576/video/mov_25_00/pt5300.mov | none available in submodule MKV corpus | gap |

### tests/projects/ntsc_progressive_mov.yaml

| Existing section | Existing source | Replacement source | Status |
| --- | --- | --- | --- |
| NtscMovMovingZone | resources/assets/704x480/video/mov_29_97/MOVING_ZONE_2H.mov | videosynth-assets/assets/mkv/720x486/MOVING_ZONE_2H.mkv | mapped |

## Gap List and Follow-Up Tasks

The following fixture intents do not have direct assets in videosynth-assets/assets and therefore block strict one-to-one migration:

- PAL testcard still:
  - missing asset equivalent to testcard-f-bt601-studio.exr.
- PAL moving-video fixtures beyond Moving Zone:
  - missing MKV for PLUGE motion fixture.
  - missing MKV for pt5300 motion fixture.

Required follow-up tasks before full fixture migration in Phase 5:

1. Add BT.601-compliant replacement assets for each gap to videosynth-assets/assets.
2. If an intent is intentionally removed, update fixture design and expected coverage explicitly in docs/design and tests.
3. Re-run mapping review and sign off zero unresolved corpus gaps for required fixture objectives.

## Phase 2 Exit-Criteria Status

- Reviewed manifest exists: complete.
- Deterministic section mapping exists for all current fixture sections: complete.
- Gaps identified with explicit follow-up actions: complete.