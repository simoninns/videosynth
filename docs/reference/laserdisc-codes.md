# Laserdisc code types

Every `code_type` accepted in a section's [`line_injections[].codes`](section-line-injections.md), with its hex format, where it is legal and what it needs.

`disc_type` (CAV/CLV) is project-wide; `section_type` is per section. Together they decide which code types are accepted.

## At a glance

| `code_type` | Disc | Standard | Section | Auto-increments | Field |
|-------------|------|----------|---------|-----------------|-------|
| `lead_in` | CAV/CLV | All | `lead_in` | — | — |
| `lead_out` | CAV/CLV | All | `lead_out` | — | — |
| `picture_number` | CAV | All | `programme_area` | ✅ | `start_value` |
| `picture_stop` | CAV | All | `programme_area` | — | — |
| `chapter_number` | CAV/CLV | All | `programme_area` | — | `chapter` |
| `programme_status` | CAV/CLV | All | `programme_area` | — | `programme_status` |
| `users_code` | CAV/CLV | All | `lead_in`, `lead_out` | — | `users_code` |
| `programme_time_code` | CLV | All | `programme_area` | ✅ | — |
| `clv_code` | CLV | All | `programme_area` | — | — |
| `clv_picture_number` | CLV | All | `programme_area` | ✅ | — |
| `fm_picture_number` | CAV | NTSC/PAL-M | `programme_area` | ✅ | — |
| `fm_programme_time` | CLV | NTSC/PAL-M | `programme_area` | ✅ | — |
| `fm_white_flag` | CAV/CLV | NTSC/PAL-M | all | — | — |

---

## `lead_in`

| Property | Value |
|----------|-------|
| Hex format | `88FFFF` |
| Section | `lead_in` only |
| Lines (PAL) | 17, 18, 330, 331 |
| Lines (NTSC) | 17, 18, 280, 281 |

Fixed code marking the lead-in area. No parameters.

```yaml
- code_type: lead_in
```

## `lead_out`

| Property | Value |
|----------|-------|
| Hex format | `80EEEE` |
| Section | `lead_out` only |
| Lines (PAL) | 17, 18, 330, 331 |
| Lines (NTSC) | 17, 18, 280, 281 |

Fixed code marking the lead-out area. No parameters.

```yaml
- code_type: lead_out
```

---

## `picture_number` (CAV)

| Property | Value |
|----------|-------|
| Hex format | `FX₁X₂X₃X₄X₅` |
| Section | `programme_area` |
| Lines (PAL) | 17, 18 or 330, 331 (field-dependent) |
| Lines (NTSC) | 17, 18 or 280, 281 |
| Range | 1–99 999 (PAL) / 1–79 999 (NTSC) |
| Digits | X₁–X₅ each 0–9 |

The frame address on a CAV disc, incrementing by 1 every frame.

`start_value` is optional:

- **Omit it** to continue from the previous section. The counter runs continuously across every section; a disc-wide sequential numbering needs `start_value` on the *first* programme section only, or nowhere at all (numbering then begins at 1).
- **Set it** to re-anchor the count for that section — a deliberate discontinuity, for a skip or replay.

```yaml
- code_type: picture_number
  start_value: 1
```

## `picture_stop` (CAV)

| Property | Value |
|----------|-------|
| Hex format | `82CFFF` |
| Section | `programme_area` |
| Lines (PAL) | 16, 17 or 329, 330 |
| Lines (NTSC) | 16, 17 or 279, 280 |

Tells the player to freeze on this frame. Generated on the field immediately following the picture-number field, and takes priority over `programme_status` on the line they share.

```yaml
- code_type: picture_stop
```

## `chapter_number` (CAV and CLV)

| Property | Value |
|----------|-------|
| Hex format | `8X₁X₂DDD` |
| Section | `programme_area` |
| Range | 0–79 |
| Minimum length | 30 tracks |
| Lines (CAV PAL) | 17, 18, 330, 331 where no picture number |
| Lines (CLV PAL) | 18 or 331 |
| Lines (CAV NTSC) | 17, 18, 280, 281 where no picture number |
| Lines (CLV NTSC) | 18 or 281 |

`chapter` is optional:

- **Omit it** to continue the previous section's chapter, carrying its stop-bit track counter across the boundary. If no earlier section set one, numbering begins at 0.
- **Set it** to start a new chapter here, restarting the stop-bit counter (IEC 60856/60857 §10.1.5).

```yaml
- code_type: chapter_number
  chapter: 3
```

### The stop-bit

Encoded in the MSB of X₁, and computed for you:

| Position within the chapter | Stop-bit |
|-----------------------------|----------|
| Tracks 0–399 | 0 — the player's search passes through |
| Track 400 onwards | 1 — search may stop here |
| Any track of a chapter shorter than 800 tracks | 1 |
| The first chapter after the lead-in | Always 1 |

```text
X₁ = (stop_bit << 3) | (chapter / 16)
X₂ = chapter % 16
```

Chapter 5 with stop-bit 1 encodes as `885DDD`.

## `programme_status` (CAV and CLV)

| Property | Value |
|----------|-------|
| Hex format | `8DC/BA X₃X₄X₅` |
| Section | `programme_area` |
| Lines (PAL) | 16 or 329 |
| Lines (NTSC) | 16 or 279 |
| Horizontal start | 0.172H into the active line |

Audio/video mode and copy permission, with a SECDED Hamming check digit.

| Field | Meaning |
|-------|---------|
| Nibbles 1–2 | `DC` = CX noise reduction on, `BA` = off |
| X₃ | Disc size, disc side, teletext presence, copy permission |
| X₄ | Audio/video mode — 0–3 standard, 8 = mono dump |
| X₅ | Hamming check, computed automatically |

```yaml
- code_type: programme_status
  programme_status: "0x8DC000"    # CX on, copy prohibited, stereo
```

The GUI's **Programme Status Code** dialog composes this word from readable fields, which is a great deal easier than assembling it by hand.

## `users_code`

| Property | Value |
|----------|-------|
| Hex format | `8X₁DX₃X₄X₅` |
| Section | `lead_in` or `lead_out` only |
| X₁ | 0–7 only |
| Lines (PAL) | 16 or 329 |
| Lines (NTSC) | 16 or 279 |

Disc-specific metadata for the manufacturer.

```yaml
- code_type: users_code
  users_code: "0x80D234"
```

X₁ values of 8–F are reserved and rejected:

```text
code_type 'users_code': X₁ value must be 0-7, got 8
```

---

## `programme_time_code` (CLV)

| Property | Value |
|----------|-------|
| Hex format | `FX₁DDX₂X₃` |
| Section | `programme_area` |
| Lines (PAL) | 17, 18 or 330, 331 |
| Lines (NTSC) | 17, 18 or 280, 281 |
| X₁ | 0–F (hours 0–15) |
| X₂ | 0–5 (minutes tens, BCD) |
| X₃ | 0–9 (minutes units, BCD) |

Elapsed programme time in hours and minutes. Starts at `0:00` and advances automatically — minutes increment every 1500 frames (PAL) or 1800 (NTSC). There is nothing to configure.

```yaml
- code_type: programme_time_code
```

## `clv_code` (CLV)

| Property | Value |
|----------|-------|
| Hex format | `87FFFF` |
| Section | `programme_area` |
| Lines (PAL) | 17 or 330 |
| Lines (NTSC) | 17 or 280 |
| Horizontal start | 0.172H into the active line (NTSC only) |

A fixed marker identifying the disc as CLV, placed on fields that carry no `programme_time_code` or `clv_picture_number`.

```yaml
- code_type: clv_code
```

## `clv_picture_number` (CLV)

| Property | Value |
|----------|-------|
| Hex format | `8X₁EX₃X₄X₅` |
| Section | `programme_area` |
| Lines (PAL) | 16 or 329 |
| Lines (NTSC) | 16 or 279 |
| X₁ | A–F |
| X₃ | 0–9 |
| X₄ | 0–2 |
| X₅ | 0–9 |

The frame position within the current second (X₄×10 + X₅ = frame 0–29) and the second offset within a 60-second block (X₁, X₃). Advances every frame, automatically.

On NTSC it includes colour time error correction (IEC 60857 Amendment 2 §10.1.10): the seconds count jumps forward at specific frame counts to keep colour timing accurate.

```yaml
- code_type: clv_picture_number
```

---

## NTSC 40-bit FM codes

These are NTSC and PAL-M only, and are mandatory on a compliant NTSC disc alongside the 24-bit biphase codes. The two systems occupy different lines within the reserved range and are generated simultaneously.

### `fm_picture_number` (NTSC CAV)

| Property | Value |
|----------|-------|
| Signal | 40-bit FM |
| Section | `programme_area` |
| Lines | 10, 273 |
| Range | 0–79 999 |
| Update | Second field of each new picture |

Carries the CAV picture number in the 40-bit FM format, advancing with the 24-bit `picture_number`.

```yaml
- code_type: fm_picture_number
```

### `fm_programme_time` (NTSC CLV)

| Property | Value |
|----------|-------|
| Signal | 40-bit FM |
| Section | `programme_area` |
| Lines | 10, 273 |

Carries CLV programme time in the 40-bit FM format. The X₅ mode indicator distinguishes the disc area: A = lead-in, B = end of lead-in, D = programme, C = lead-out.

```yaml
- code_type: fm_programme_time
```

### `fm_white_flag` (NTSC)

| Property | Value |
|----------|-------|
| Signal | 100 IRE constant level, 0.790H long, starting 0.160H from sync |
| Section | `lead_in`, `programme_area`, `lead_out` |
| Lines (lead-in) | 11 only |
| Lines (programme) | 11 or 274, field-dependent |
| Lines (lead-out) | 11 and 274 |

Marks the start of each new picture, which is how a player decides what a "frame" is when holding a still. Placed on the field that opens the picture and **deferred automatically** when consecutive fields are identical, as with telecine or a freeze.

```yaml
- code_type: fm_white_flag
```

---

## Priority

Where two code types would occupy the same line, the standard fixes the order:

1. Lead-in and lead-out codes take absolute priority in their sections.
2. `picture_stop` beats `programme_status`.
3. `programme_time_code` beats `chapter_number` on lines 17/18/280/281 (CLV).
4. `clv_picture_number` beats `chapter_number` on lines 16/279 (CLV).
5. `chapter_number` yields to `picture_number` on shared lines (CAV).

## Horizontal timing

`programme_status` (both standards) and `clv_code` (NTSC) begin **0.172H** into the active line — IEC 60856 Figure 14, IEC 60857 Figure 11. All other code types begin at the start of the active line. The offset is applied automatically.

## Further reading

The repository carries the full bit layouts, encoding rules and worked examples in [`docs-tech/user/biphase-design.md`](https://github.com/simoninns/videosynth/blob/main/docs-tech/user/biphase-design.md){target="_blank"}.
