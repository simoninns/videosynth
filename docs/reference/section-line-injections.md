# `sections[].line_injections`

The laserdisc biphase codes this section carries. Optional.

This is a **list**, and a different shape from the project-level [`line_injections:`](line-injections.md) map. The project-level block holds the disc format and the VITS set; this one holds only the per-section codes.

```yaml
    line_injections:
      - type: laserdisc
        codes:
          - code_type: picture_number
            start_value: 1
          - code_type: chapter_number
            chapter: 1
          - code_type: programme_status
            programme_status: "0x8DC000"
```

## Injection keys

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| `type` | string | Yes | The injection type — see below |
| `target_lines` | list of int | Never for `laserdisc` | Frame lines. Specifying it on a `laserdisc` injection is an error |
| `codes` | list | Yes for `laserdisc` | The biphase codes (below) |

No other key is accepted.

## `type`

| Value | Status |
|-------|--------|
| `laserdisc` | Implemented |
| `vitc` | **Not implemented** — using it is a validation error |
| `line_content` | **Not implemented** — using it is a validation error |

`vits` is not a section-level type; VITS are declared project-wide.

Rather than generating a file that quietly lacks the content you asked for, videosynth rejects the unimplemented types outright:

```text
Project configuration error: line injection type 'vitc' is not implemented in
the current runtime.
```

## No line selection

`target_lines` must not appear on a `laserdisc` injection. VBI line placement is determined entirely by IEC 60856/60857 from the code type, the disc format, and which field opens the current picture — videosynth computes it per frame.

```text
target_lines must not be specified for laserdisc injections; line placement is
fixed by the standard.
```

Nor is `disc_type` repeated here. It is project-wide.

## `codes[]`

| Key | Type | Applies to | Description |
|-----|------|------------|-------------|
| `code_type` | string | All | Which code — see [laserdisc code types](laserdisc-codes.md) |
| `start_value` | int | `picture_number`, `fm_picture_number` | Anchors the count for this section |
| `chapter` | int | `chapter_number` | Chapter number 0–79 |
| `programme_status` | hex string | `programme_status` | 24-bit status word, e.g. `"0x8DC000"` |
| `users_code` | hex string | `users_code` | 24-bit user's code, e.g. `"0x80D234"` |

No other key is accepted. `code_type` is required on every entry.

Each optional field applies only to the code types listed; supplying one on a code type that does not use it is rejected.

## Which codes are legal here

The validator checks each `code_type` against the project's `disc_type` **and** this section's `section_type`. See the [compatibility matrices](../user-manual/laserdisc.md#which-codes-go-where).

In short:

```text
lead_in:         lead_in, users_code, fm_white_flag (NTSC)
programme_area:  picture_number, picture_stop, chapter_number, programme_status,
                 programme_time_code, clv_code, clv_picture_number,
                 fm_picture_number, fm_programme_time, fm_white_flag (NTSC)
lead_out:        lead_out, users_code, fm_white_flag (NTSC)
```

## Continuity across sections

`picture_number`, `programme_time_code` and `clv_picture_number` run continuously across section boundaries by design (IEC 60856/60857 §10.1.5 requires it). The CLV clocks have nothing to configure at all.

For `picture_number`:

- **Omit `start_value`** on a section to continue from the previous one. If no earlier section set it, numbering begins at 1.
- **Set `start_value`** to re-anchor the count at that section — a deliberate discontinuity, for modelling a player skip or a replayed passage.

The same pattern applies to `chapter`: omit it to continue the previous chapter (carrying its stop-bit track counter across the boundary), set it to start a new one.

Setting `start_value: 1` on every chapter would restart the numbering at each boundary, which is almost never what you want.

## Examples

**PAL CAV programme section**

```yaml
    line_injections:
      - type: laserdisc
        codes:
          - code_type: picture_number
            start_value: 1
          - code_type: picture_stop
          - code_type: chapter_number
            chapter: 1
          - code_type: programme_status
            programme_status: "0x8DC000"
```

**NTSC CLV programme section** — the 40-bit FM codes are mandatory alongside the 24-bit ones

```yaml
    line_injections:
      - type: laserdisc
        codes:
          - code_type: programme_time_code
          - code_type: clv_code
          - code_type: clv_picture_number
          - code_type: chapter_number
            chapter: 1
          - code_type: programme_status
            programme_status: "0x8DC000"
          - code_type: fm_programme_time
          - code_type: fm_white_flag
```

**Lead-in with a user's code**

```yaml
    line_injections:
      - type: laserdisc
        codes:
          - code_type: lead_in
          - code_type: users_code
            users_code: "0x80D234"
```
