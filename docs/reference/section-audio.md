# `sections[].audio`

Audio channel pairs for this section. Optional — a section without an `audio:` block emits silence for its frames across every pair, still frame-locked.

```yaml
    audio:
      channel_pairs:
        - pair: 0
          description: Analogue stereo
          left:  { waveform: sine, frequency: 1000.0, amplitude: 0.5 }
          right: { waveform: sine, frequency: 1000.0, amplitude: 0.5 }
```

## Keys

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| `channel_pairs` | list | Yes | One entry per stereo channel pair |

No other key is accepted.

## `channel_pairs[]`

| Key | Type | Required | Range | Description |
|-----|------|----------|-------|-------------|
| `pair` | int | Yes | 0–7 | Channel-pair number; names the `_audio_<pair>.wav` file and the `audio_channel_pair` metadata row. Unique within a section |
| `description` | string | No | — | Human-readable label recorded in the metadata, e.g. `Analogue stereo` |
| `left` | map | No\* | Tone descriptor | The odd (first) interleaved channel. Omitted → silent |
| `right` | map | No\* | Tone descriptor | The even (second) interleaved channel. Omitted → silent |

\* At least one of `left`/`right` must be present. A pair with no active channel is rejected.

A project may carry up to 8 pairs (0–7), i.e. up to 16 SMPTE 272M channels. The set of files written is the union of pair numbers declared across all sections, and every pair file spans the whole output.

## Tone descriptor

`left:` and `right:` each take:

| Key | Type | Required | Range / Values | Default |
|-----|------|----------|----------------|---------|
| `waveform` | string | No | `sine`, `square`, `sawtooth`, `triangle` | `sine` |
| `frequency` | float | No | 0–22 000 Hz | `1000.0` |
| `amplitude` | float | No | 0.0–1.0 of full scale | `0.5` |
| `ramp` | map | No | Frequency sweep (below) | — |

Waveforms are naive rather than band-limited, which is what a test signal wants.

`ramp:` and a fixed `frequency` are mutually exclusive.

## `ramp`

| Key | Type | Required | Range / Values | Default |
|-----|------|----------|----------------|---------|
| `start` | float | Yes | 0–22 000 Hz | — |
| `end` | float | Yes | 0–22 000 Hz | — |
| `mode` | string | No | `up`, `down`, `bounce` | `up` |
| `period` | float | No | 0 – section duration in seconds | `0` |

| `mode` | Sweep |
|--------|-------|
| `up` | start → end |
| `down` | end → start |
| `bounce` | start → end → start |

`period: 0` sweeps once over the whole section run. A positive `period` makes one sweep last that many seconds and repeat until the section ends.

Instantaneous frequency drives a phase accumulator, so swept tones stay phase-continuous within a section run.

## Frame lock

Every track is stereo, 24-bit signed little-endian PCM (RIFF/WAVE, format tag `0x0001`) at 48 000 Hz, synchronous with the video per SMPTE 272M §1.2.

| Standard | Samples per frame |
|----------|-------------------|
| PAL | 1920 (constant) |
| NTSC / PAL-M | 1602/1601 in the five-frame sequence 1602, 1601, 1602, 1601, 1602 → 8008 per 5 frames (SMPTE 272M §14.3) |

Audio follows the **output** frame stream rather than source frames, so alignment holds regardless of what the sections do. Each contiguous run of frames sharing one section restarts its oscillators at phase 0.

## Output

```text
<basename>_audio_<pair>.wav
```

where `<basename>` is `output.video_path` with its trailing `.cvbs` or `.cvbsy` removed.

When at least one pair is declared, one `audio_channel_pair` row per pair — with its `description` — is written to the `.meta` sidecar.

To additionally encode one pair as laserdisc EFM digital audio, see [`output.efm_audio`](output.md#efm_audio).

## Example

```yaml
sections:
  - name: StereoAndCommentary
    type: progressive
    source: "{bundled}/exr/720x576/75_BARS.exr"
    duration_frames: 200
    audio:
      channel_pairs:
        - pair: 0
          description: Analogue stereo
          left:  { waveform: sine, frequency: 1000.0, amplitude: 0.5 }
          right: { waveform: sine, frequency: 1000.0, amplitude: 0.5 }
        - pair: 1
          description: Commentary (left only)
          left:  { waveform: square, frequency: 440.0 }

  - name: SilenceGap                # no audio: block — silent, still frame-locked
    type: progressive
    source: "{bundled}/exr/720x576/75_BARS.exr"
    duration_frames: 50

  - name: SweepUp
    type: progressive
    source: "{bundled}/exr/720x576/75_BARS.exr"
    duration_frames: 200
    audio:
      channel_pairs:
        - pair: 0
          left:
            waveform: sawtooth
            amplitude: 0.5
            ramp: { start: 200.0, end: 4000.0, mode: up }
```
