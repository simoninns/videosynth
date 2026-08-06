# Picture sources

Every section draws its picture content from one **progressive source** file. videosynth converts it to an interlaced signal on the way through — the source is progressive, the output is interlaced, and the two-field weave is the generator's job, not yours.

Source ingestion is deliberately strict. There is no scaling, no cropping, no frame-rate conversion and no format guessing: a file either matches a supported profile exactly or it is rejected at validation. This is what makes the output predictable — nothing is silently resampled behind your back.

## Supported profiles

### EXR still image

A single-frame OpenEXR image, used when a section should hold one picture for its whole duration.

| Requirement | Value |
|-------------|-------|
| Container | Scanline OpenEXR, single frame |
| Channels | `R`, `G`, `B` |
| Type | `FLOAT` (32-bit) |
| Compression | None |
| Windows | Data and display windows must cover the full raster |
| Raster | 720×576 (PAL) or 720×486 (NTSC/PAL-M) |
| Frame rate metadata | `25/1` (PAL) or `30000/1001` (NTSC/PAL-M) |
| Pixel aspect metadata | Must match the standard |

### MKV video clip

A Matroska clip, used when a section should show moving content.

| Requirement | Value |
|-------------|-------|
| Container | Matroska |
| Video codec | FFV1 |
| Pixel format | `yuv422p10le` |
| Raster and rate | 720×576 @ 25 (PAL) or 720×486 @ 30000/1001 (NTSC/PAL-M) |
| Metadata | Standard-matching SD field order and colour metadata |

### BT.601 content compliance

Both profiles additionally require the *content* to be BT.601-consistent, not merely the metadata: the sampling model, active-window placement and horizontal padding must match what the profile expects.

| Standard | Raster | Sample aspect ratio | Horizontal padding |
|----------|--------|---------------------|--------------------|
| PAL | 720×576 | 128:117 | 8–704–8 |
| NTSC / PAL-M | 720×486 | 108:119 | 8–704–8 |

Source data is converted to 10-bit 4:4:4 YCbCr BT.601 studio swing at ingestion, and studio-domain sub-black and over-white excursions are preserved where the source carries them.

The full requirements are documented in the asset repository, in `videosynth-assets/docs/mkv-bt601-compliance-requirements.md` and `exr-bt601-compliance-requirements.md`.

## Bundled assets

videosynth ships a set of test patterns in both rasters, addressed through the `{bundled}` [asset root](../reference/asset-roots.md):

```yaml
source: "{bundled}/exr/720x576/100_BARS.exr"
```

The layout is `{bundled}/<type>/<raster>/<file>`, where `<type>` is `exr` or `mkv` and `<raster>` is `720x576` or `720x486`.

### Stills (`exr`)

| Asset | Content |
|-------|---------|
| `100_BARS.exr` | 100 % colour bars |
| `75_BARS.exr` | 75 % colour bars |
| `75_BARS_RED.exr` | 75 % bars with 100 % red |
| `SMPTE_BARS.exr` / `SMPTE_BARS_001.exr` | SMPTE bars (PAL / System-M raster) |
| `PLUGE.exr` | PLUGE black-level alignment pattern |
| `MULTIBURST.exr` | Frequency response multiburst |
| `LUMA_RAMP.exr`, `LUMA_RAMP_DOWN.exr`, `VERT_LUMA_RAMP.exr` | Luma ramps |
| `CHROMA_RAMP.exr` | Chroma ramp |
| `FULL_RAMP.exr`, `LEGAL_RAMP.exr`, `VALID_RAMPS.exr` | Full-range, legal-range and valid-range ramps |
| `Y_CB_CR_RAMPS.exr` | Separate Y, Cb and Cr ramps |
| `GREY_5H_STEP.exr`, `GREY_10H_STEP.exr` | Horizontal grey staircases |
| `GREY_5V_STEP.exr`, `GREY_10V_STEP.exr` | Vertical grey staircases |
| `TARTAN.exr` | Tartan/crosshatch pattern |
| `TESTCARD-F.exr` | UK Test Card F (PAL raster only) |

### Clips (`mkv`)

| Asset | Content |
|-------|---------|
| `SMPTE_BARS.mkv` / `SMPTE_BARS_001.mkv` | Static SMPTE bars as a clip |
| `MOVING_ZONE_2H.mkv` | Moving zone plate — motion and high-frequency content |

Always reference bundled assets through `{bundled}/…` rather than a bare relative path. A bare relative path resolves against the working directory in the CLI but against the project directory in the GUI, so it will work in one and not the other.

## Choosing frames from the source

Three section fields control which frames a section takes and how many it emits:

| Field | Meaning |
|-------|---------|
| `start_frame` | First source frame to use (default `0`) |
| `duration_frames` | How many output frames the section produces — a positive integer, or `"all"` |
| `duration_repeat` | With `duration_frames: "all"`, how many times to replay the whole source (default `1`) |

**A fixed count** takes exactly that many output frames. For a still, the same picture is emitted for all of them. For a clip shorter than the count, the source loops.

```yaml
- name: TenSecondsOfBars
  type: progressive
  source: "{bundled}/exr/720x576/100_BARS.exr"
  duration_frames: 250          # 10 s at 25 fps
```

**`"all"`** uses the source's own length, discovered by probing the file.

```yaml
- name: WholeClipTwice
  type: progressive
  source: "{bundled}/mkv/720x576/MOVING_ZONE_2H.mkv"
  duration_frames: "all"
  duration_repeat: 2            # total = source length x 2
```

`duration_repeat` is ignored with a fixed `duration_frames`, and validation warns if you set it there.

## Source probing

Validation probes each source file: it opens it, checks it against the profile rules and reads its frame count. A source that does not exist, or does not match a profile, is a validation error naming the file and the reason.

In the GUI this runs in the background whenever you change a section's source, and the **Source Profile** box in the section editor reports the result — *Source profile OK*, *Source profile incompatible*, or *Probing source…* while it works. The frame count it discovers is what feeds the `duration_frames: "all"` totals shown beside the duration field.

## Converting your own material

The repository ships `scripts/convert_progressive_raw_to_exr.py` for turning raw `yuv422p10le` stills into conforming OpenEXR fixtures, with `scripts/run_stage1_raw_to_exr_conversion.sh` to run it in an ad-hoc Nix shell.

For video, the essential FFmpeg requirements are FFV1 in Matroska at `yuv422p10le`, at the exact raster and frame rate for the target standard, with correct SD colour and field-order metadata. Anything else will be rejected — which is the point.
