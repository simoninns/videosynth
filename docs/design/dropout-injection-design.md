# Dropout Injection Design

**Scope**: Per-section dropout injection for videosynth, producing both a degraded
CVBS signal and a conformant dropout sidecar file (`<basename>.dropouts.meta`) as
defined in the [Dropout Extension Format](../cvbs-file-format-specification/docs/extensions/dropout-extension-format.md).

---

## Physical Background

A dropout in a pre-demodulation RF signal is a transient loss of carrier amplitude
caused by physical defects in the recording medium. The demodulator receives no
usable signal for the duration of the event and its output collapses toward a
rest-level — typically blanking (0 mV) for a composite demodulator. The affected
samples are therefore attenuated toward blanking rather than corrupted with
random values.

Two distinct defect mechanisms produce statistically different dropout patterns:

### Surface / Media Degradation (Random Dropouts)

Surface oxidation, shedding, or abrasion introduces microscopic voids in the
recorded medium. These voids are distributed quasi-randomly across the recording
surface, so the resulting dropouts:

- Are short (typically 1–100 µs, or roughly 20–1750 4fsc samples at PAL).
- Are distributed with approximately uniform spatial density across all lines and
  all positions within lines.
- Occur with a frequency that is statistically independent from frame to frame
  (Poisson arrival process).
- Do not repeat at the same position in subsequent frames.

The dominant parameter is the **mean event rate** (expected number of dropouts per
frame) plus a **maximum duration** that caps individual event lengths.

### Scratch / Track Defects (Scratch Dropouts)

A physical scratch or pit on a disc follows a radial path across the track spiral.
Because the same scratch crosses the same part of the recorded track on every
revolution, it manifests as a dropout that:

- Reappears at approximately the **same line** on every frame (within ±1–2 lines
  of jitter due to disc eccentricity and tracking variation).
- Grows from zero width on the first frame the scratch is encountered, reaches a
  **peak width** at the midpoint of its lifespan (the deepest part of the scratch),
  and shrinks symmetrically back to zero on the last frame — producing a
  characteristic triangular growth/decay envelope when plotted frame-by-frame.
- Is bounded in both **frame count** (how many frames the scratch affects) and
  **peak width** in samples (how wide the dropout is at its worst).

A single scratch event is parameterised by its anchor line, anchor position within
that line, total lifespan in frames, and peak width in samples. Multiple independent
scratch events may coexist within one section.

---

## Signal Modification Model

A dropout does not always drive the signal to a fixed extreme. The RF-demodulator
circuit behaviour during signal loss causes the output to be pushed in one direction
— either toward blanking (level pull-down) or toward white (level push-up / flash) —
and the magnitude of that push varies with the severity of the defect. The direction
and magnitude are properties of each individual dropout event.

### Direction

Each dropout event has a randomly assigned direction, drawn once when the event is
created:

- **-1 (low)**: signal is pushed toward blanking level (0 mV). The most common
  outcome of RF loss; the demodulator output collapses toward its quiescent level.
- **+1 (high)**: signal is pushed toward white level. Occurs when the demodulator
  overshoots after sudden signal loss, producing a brief luminance flash.

Direction is drawn with equal probability.

### Magnitude (push fraction)

Each event also has a `push_fraction` drawn from `Uniform(0.2, 1.0)`:

- `1.0` is a full push to the target level (blanking or white).
- `0.2` is a 20 % push — the signal moves only slightly toward the target, retaining
  80 % of its original value.

The range `[0.2, 1.0]` gives a realistic mix: most events are strong (heavy surface
loss) but some are partial (light surface contact).

### Application

For a given sample `s` with direction `d` and push fraction `p`:

```
target_mV = (d < 0) ? blanking_mv : white_mv
new_mV    = lerp(original_mV, target_mV, p)
          = original_mV + p × (target_mV − original_mV)

y[s] = clamp(new_mV)
c[s] = clamp(new_mV)
```

Both Y and C receive the same displacement because the dropout affects the composite
signal before any Y/C separation — the same carrier loss hits both channels
simultaneously.

No attack/release shaping is applied at run boundaries; the sharp edges are
physically realistic for RF dropouts, where signal loss is abrupt.

### Scratch Amplitude Envelope

For scratch dropouts, both the **spatial width** and the **push fraction** grow with
the same triangle envelope over the scratch lifespan:

```
triangle      = 1.0 − |2.0 × progress − 1.0|   // 0 at edges, 1.0 at midpoint
current_width = round(peak_width × triangle)
current_push  = max_push_fraction × triangle
```

At the first and last frames of the scratch lifespan the width is narrow and the
amplitude push is small — the scratch is just beginning to cut into the medium.
At the midpoint the width and push are both at maximum — the disc track is passing
through the deepest part of the scratch.

Results are clamped to the legal fixed-point range after modification to prevent
downstream quantisation overflow.

---

## Overlap Resolution

When both `random:` and `scratch:` are active in the same section, their sample
ranges may overlap within a frame. Scratch dropouts take precedence. The rule is:

> Any sample position already covered by a scratch dropout run must not also be
> covered by a random dropout run — in the signal buffers or in the sidecar.

This is necessary for two reasons:

1. **Signal correctness**: a scratch is a deterministic, repeatable defect with its
   own direction, push fraction, and amplitude envelope. A random dropout applied on
   top of a scratch-covered sample would compound the displacement unpredictably,
   breaking the scratch's consistent frame-to-frame appearance. The random pass must
   not treat scratch-covered samples as its own events.
2. **Sidecar conformance**: the dropout extension format schema uses
   `(cvbs_file_id, frame_id, sample_start)` as a primary key and requires
   non-overlapping runs per frame. Writing both a scratch row and a random row for
   the same sample_start violates the primary key constraint and breaks the
   "no overlapping runs" producer requirement.

### Per-Frame Resolution Algorithm

For each frame, overlap is resolved before any signal modification or sidecar writes:

```
1. Compute the scratch interval list S for this frame:
   Each entry is [sample_start, sample_start + width).

2. Generate the raw random event list R for this frame (positions and durations
   drawn from the Poisson/Geometric distributions as normal).

3. For each random event r in R:
   a. Clip r against every scratch interval in S:
      - If r is fully contained within any scratch interval: discard r entirely.
      - If r partially overlaps one or more scratch intervals: split r into
        sub-runs that cover only the non-scratch samples.
        The split produces at most two sub-runs (left remainder and right
        remainder) per scratch interval.
   b. Any surviving sub-run length >= 1 is retained in the resolved random list R'.

4. Apply scratch events to the signal buffers first; then apply R' events.

5. Write sidecar rows: scratch rows first, then R' rows.
   All rows for a given frame share the same frame_id; no two rows have
   overlapping [sample_start, sample_start + sample_count) ranges.
```

This also prevents random-vs-random self-overlap: after step 3 the surviving
sub-runs in R' are already non-overlapping with scratch intervals. Random events
drawn independently may still overlap each other (probability is low for typical
parameters but non-zero at high frequency). After clipping against S, the resulting
R' list is sorted by sample_start and merged (adjacent or overlapping sub-runs are
combined into a single row) before signal application and sidecar writing. The
merged row's severity is determined by its position (visible or non-visible) after
merging, using the same rule as all other rows.

### What the Sidecar Consumer Sees

A scratch run and a random run never share sample coverage in the same frame. A
consumer that renders dropouts by looking up all rows for a given frame will see:

- Scratch rows covering the scratch region.
- Random rows covering only the non-scratch random regions.
- Every row carries severity 25 (non-visible) or 75 (visible active picture).
- No gaps, no overlaps.

---

## Sidecar Format

The dropout sidecar is a SQLite file conforming to the
[Dropout Extension Format](../cvbs-file-format-specification/docs/extensions/dropout-extension-format.md)
(schema version 5).

The `dropout_run` table records every dropout run written during generation:

| Column | Value written by videosynth |
|--------|-----------------------------|
| `cvbs_file_id` | `1` (implicit default; matches the single capture in `<basename>.meta`). |
| `frame_id` | Zero-based sequential frame index within the output file. |
| `sample_start` | Zero-based sample index within the frame where the dropout begins. |
| `sample_count` | Number of consecutive samples affected. |
| `severity` | `75` if the run falls within the active picture sample range; `25` otherwise. |

### Active-Picture Sample Ranges

Severity is assigned based on whether the run's `sample_start` falls within the
active picture region of the frame. The active picture sample ranges at 4fsc are:

| Standard | Field 1 active lines | Field 2 active lines | Active picture start sample | Active picture end sample |
|----------|---------------------|---------------------|-----------------------------|--------------------------|
| PAL | 23–620 | 335–622 | line 23 × 1135 = 26 105 | end of line 622 = 706 419 |
| NTSC | 22–262 | 284–520 | line 22 × 910 = 20 020 | end of line 520 = 473 470 |

PAL uses the nominal 1135 samples/line; the four lines per frame with 1134 samples
each do not affect line-boundary classification at this level of precision.

A dropout run that **starts** in the active picture region receives `severity = 75`.
A run that starts in VBI, sync, or blanking receives `severity = 25`.

If a run spans the boundary between non-visible and visible regions it is **split
into two rows** at the first active-picture sample of the relevant line, with the
non-visible portion receiving `severity = 25` and the visible portion `severity = 75`.
This split is applied after overlap resolution so boundary-straddling runs are
never suppressed by the scratch precedence rule before they are examined.

The sidecar path is auto-derived from `output.metadata_path` by replacing the
`.meta` suffix with `.dropouts.meta`. If `metadata_path` does not end with `.meta`,
`.dropouts.meta` is appended to the full path.

Example: `out/pal_test.meta` → `out/pal_test.dropouts.meta`.

The sidecar is only created when at least one section has dropout injection enabled.
If no section enables dropouts, no sidecar file is written.

---

## YAML Schema Addition

Dropout parameters are optional and per-section, under a `dropouts:` sub-key.
Each sub-key (`random:` and `scratch:`) is independently optional.

Each type is controlled by a single `scale` integer (0–20), where `0` disables
that type and increasing values produce higher frequency and larger dropouts.
`seed` is an optional reproducibility refinement. `severity` is not a YAML
parameter; it is computed automatically by the injection stage from sample position.

```yaml
sections:
  - name: "Degraded laserdisc section"
    type: progressive
    source: "assets/source.mkv"
    duration_frames: 100
    dropouts:
      random:
        scale: 8   # 0 = disabled; 1–20 = increasing frequency and run length
        seed: 42   # Optional: base RNG seed for reproducibility
      scratch:
        scale: 5   # 0 = disabled; 1–20 = increasing count, lifespan, and width
        seed: 7    # Optional: base RNG seed
```

Rules:

- Neither `random:` nor `scratch:` present → no dropout injection; no sidecar.
- `random:` only → random surface-degradation dropouts.
- `scratch:` only → scratch-pattern dropouts.
- Both present → both types applied and merged into the same sidecar.
- `scale: 0` within either sub-key is equivalent to omitting that sub-key.
- `seed` is optional for both; when omitted a random base seed captured at
  pipeline construction time is used (fixed within one run, different across runs).
- Sidecar `severity` is computed automatically from sample position (25 for
  non-visible, 75 for active picture); it is not a YAML parameter.

---

## Valid Ranges

### `random.scale` and `scratch.scale`: 0 – 20 (integer)

- `0` disables the dropout type entirely; no sidecar rows are written for it.
- `1` is the minimum active level: very rare, very short events.
- `20` is the maximum: high event rate and long/wide events representing
  catastrophically degraded media.

---

## Scale Mapping

The `scale` value is mapped to the internal generation parameters by the following
exponential formulas, evaluated once at section start. These are implementation
constants; they are not exposed in the YAML.

### Random Dropout Mapping

```
frequency(scale)     = 0.05 × 2000 ^ ((scale − 1) / 19)   [events per frame]
max_duration(scale)  = max(1, round(5 × 400 ^ ((scale − 1) / 19)))  [samples]
```

At scale 1 the expected rate is 0.05 events/frame (≈ 1–2 events/second at PAL 25 fps,
representative of near-pristine media). At scale 20 the rate reaches 100 events/frame
with runs up to 2000 samples, representing catastrophically degraded media.

| Scale | frequency (ev/frame) | max_duration (samples) | Approx. physical equivalent |
|------:|---------------------:|----------------------:|------------------------------|
| 0 | — | — | Disabled |
| 1 | 0.05 | 5 | Near-pristine laserdisc |
| 5 | 0.25 | 18 | Good laserdisc |
| 8 | 0.82 | 46 | Fair laserdisc |
| 10 | 1.8 | 86 | Good VHS |
| 12 | 4.1 | 162 | Average VHS |
| 14 | 9.0 | 304 | Poor / worn tape |
| 16 | 20 | 571 | Severely degraded |
| 18 | 45 | 1074 | Near-unwatchable |
| 20 | 100 | 2000 | Maximum degradation |

### Scratch Dropout Mapping

```
count(scale)              = round(1 + (scale − 1) × 14 / 19)  [integer, 1 to 15]
max_duration_frames(scale) = max(1, round(2 × 250 ^ ((scale − 1) / 19)))  [frames]
max_width_samples(scale)  = max(1, round(5 × 400 ^ ((scale − 1) / 19)))  [samples]
```

`count` grows linearly so the number of discrete scratch events increases gradually.
`max_duration_frames` and `max_width_samples` grow exponentially to model the
disproportionate impact of deeper scratches.

| Scale | count | max_duration_frames | max_width_samples | Approx. physical equivalent |
|------:|------:|--------------------:|------------------:|------------------------------|
| 0 | — | — | — | Disabled |
| 1 | 1 | 2 | 5 | Hairline scratch |
| 5 | 4 | 6 | 18 | Light scratch |
| 8 | 6 | 15 | 46 | Moderate scratch |
| 10 | 8 | 27 | 86 | Significant scratch |
| 12 | 9 | 49 | 162 | Heavy scratch |
| 14 | 11 | 87 | 304 | Severe scratch |
| 16 | 12 | 156 | 571 | Very severe |
| 18 | 14 | 279 | 1074 | Near-destroyed |
| 20 | 15 | 500 | 2000 | Maximum degradation |

`max_width_samples` at scale 20 is 2000 samples, equivalent to ~113 µs or ~1.75
lines at PAL 4fsc — representing a very deep gouge. At scale 10 (significant
scratch) the cap is 86 samples (~5 µs), close to the typical width of a disc
scratch dropout in practice.

**Practical scale selection guide:**

| Source / condition        | `random.scale` | `scratch.scale` |
|---------------------------|:--------------:|:---------------:|
| Near-pristine laserdisc   | 1–3            | 0–2             |
| Good laserdisc            | 4–6            | 2–4             |
| Fair laserdisc / good VHS | 7–9            | 4–6             |
| Poor VHS / worn tape      | 10–13          | 5–8             |
| Severely degraded         | 14–17          | 7–11            |
| Near-unwatchable          | 18–20          | 12–15           |

---

## Dropout Generation Algorithms

### Scale-to-Parameter Derivation

Both algorithms operate on derived internal parameters. These are computed once per
section from the `scale` value using the formulas in §Scale Mapping:

```
// Random
frequency         = 0.05 × pow(2000.0, (scale − 1) / 19.0)
max_duration      = max(1, round(5.0 × pow(400.0, (scale − 1) / 19.0)))

// Scratch
count             = round(1.0 + (scale − 1) × 14.0 / 19.0)
max_dur_frames    = max(1, round(2.0 × pow(250.0, (scale − 1) / 19.0)))
max_width_samples = max(1, round(5.0 × pow(400.0, (scale − 1) / 19.0)))
```

### Random Dropout Algorithm (per frame)

```
// At section start: derive frequency and max_duration from scale (see above).

// Per frame:
1. Draw N ~ Poisson(frequency) using the per-frame RNG.
2. For each event i in 0..N-1:
   a. Draw sample_start ~ Uniform(0, samples_per_frame - 1).
   b. Draw duration ~ Geometric(0.5) capped at max_duration.
      (Geometric distribution gives an exponential-like spread: most events are
       short and rare events are long — matching physical reality.)
      Minimum clamped to 1.
   c. Clip sample_start + duration to frame boundary.
   d. Draw direction ~ choice(-1, +1).
   e. Draw push_fraction ~ Uniform(0.2, 1.0).
3. Apply overlap resolution (see §Overlap Resolution) to obtain the
   de-overlapped, sorted, merged sub-run list R'.
   (Each surviving sub-run inherits the direction and push_fraction of its
   originating event; merged adjacent sub-runs inherit from the first.)
4. For each sub-run in R':
   a. Split at any active-picture boundary (see §Active-Picture Sample Ranges)
      to produce at most two sub-runs, each wholly inside or outside the
      active picture.
   b. Apply directional push: for s in sub-run:
        target = (direction < 0) ? blanking_mv : white_mv
        y[s] = clamp(lerp(y[s], target, push_fraction))
        c[s] = clamp(lerp(c[s], target, push_fraction))
   c. Compute severity: 75 if sub-run starts in active picture, else 25.
   d. Append (frame_id, sample_start, duration, severity) to sidecar.
```

### Scratch Dropout Algorithm (per section)

Scratch event parameters are seeded from the section + event index (not the frame),
so the same scratch recurs consistently across frames.

```
// At section start: derive count, max_dur_frames, max_width_samples from scale.

// Pre-compute scratch event table:
For each event e in 0..count-1:
  seed_e = hash(section_index, e, scratch_base_seed)
  rng_e = mt19937_64(seed_e)

  anchor_line      = Uniform(0, lines_per_frame - 1)
  anchor_offset    = Uniform(0, samples_per_line - 1)
  duration_frames  = Uniform(1, max_dur_frames)
  peak_width       = Uniform(1, max_width_samples)
  direction        = choice(-1, +1)          // push direction: fixed for this scratch
  max_push_fraction = Uniform(0.2, 1.0)      // peak amplitude push at scratch centre

// Per-frame application:
For each frame F in the section:
  For each event e:
    progress = F / (duration_frames_e - 1)       // 0.0 at first frame, 1.0 at last
    triangle = 1.0 - |2.0 * progress - 1.0|     // 0 at edges, 1.0 at midpoint

    // Both width AND amplitude push scale with the triangle envelope:
    width        = round(peak_width_e * triangle)
    current_push = max_push_fraction_e * triangle
    if width == 0: skip

    // Per-frame line jitter (±2 lines) using the per-frame RNG
    line_jitter = Uniform(-2, +2)
    effective_line = clamp(anchor_line_e + line_jitter, 0, lines_per_frame - 1)

    sample_start = effective_line * samples_per_line + anchor_offset_e
    sample_start = clamp(sample_start, 0, samples_per_frame - width)

    // Apply directional push across the run:
    target = (direction_e < 0) ? blanking_mv : white_mv
    for s in [sample_start, sample_start + width):
      y[s] = clamp(lerp(y[s], target, current_push))
      c[s] = clamp(lerp(c[s], target, current_push))

    Split at any active-picture boundary (see §Active-Picture Sample Ranges).
    For each resulting sub-run:
      severity = 75 if sub-run starts in active picture, else 25.
      Append (frame_id, sub_run_start, sub_run_width, severity) to sidecar.
```

### RNG Seeding

Seeding follows the same scheme as `NoiseInjectionStage`:

```
// Random events: per-frame seed to ensure reproducibility regardless of batch size.
base_seed   = user-specified seed, or run_base_seed_ captured at construction
frame_seed  = (base_seed ^ (section_index * 2654435761ULL)) ^
              (frame_global_index * 2246822519ULL)

// Scratch event parameters: per-event seed
event_seed  = (base_seed ^ (section_index * 2654435761ULL)) ^
              (event_index * 1000000007ULL)
```

Two separate base seeds are used when both `random.seed` and `scratch.seed` are
present, ensuring the two types do not share RNG state.

---

## Pipeline Placement

Dropout injection occurs **after `NoiseInjectionStage`** and **before
`WriteFrameBatch`** (i.e., before `AppendSamples` in the output stage).

Rationale:
- Noise is a continuous baseband degradation; dropout is a signal-absence event.
  Applying dropout after noise ensures the dropout blanks both the signal and its
  noise, which matches the physical order: RF loss eliminates both the carrier and
  any overlay noise simultaneously.
- Quantisation happens after dropout injection, so the modified sample values appear
  correctly in the 10-bit output codes and in the sidecar frame/sample indices.

Pipeline call sequence in `pipeline.cpp`:

```
generate_->GenerateFrameBatch(...)       // fills y_mv, c_mv
noise_->InjectNoise(...)                 // adds Gaussian noise
dropout_->InjectDropouts(...)            // applies dropout blanking, writes sidecar records
output_->AppendSamples(y_mv, c_mv, ...) // quantises and writes CVBS file
```

`DropoutInjectionStage` is a concrete class with no virtual interface required. It
is constructed in `main.cpp` alongside `NoiseInjectionStage` and passed to
`VideoSynthPipeline` by pointer. When no section has dropout injection enabled,
`DropoutInjectionStage` is a no-op and writes no sidecar.

---

## Class Design

### `RandomDropoutParameters` and `ScratchDropoutParameters` (in `model.h`)

Added to `Section`:

```cpp
struct RandomDropoutParameters {
  bool enabled = false;
  int scale = 0;  // 1–20; maps to frequency and max_duration via scale mapping
  bool seed_specified = false;
  int64_t seed = 0;
};

struct ScratchDropoutParameters {
  bool enabled = false;
  int scale = 0;  // 1–20; maps to count, max_duration_frames, max_width_samples
  bool seed_specified = false;
  int64_t seed = 0;
};

struct DropoutParameters {
  RandomDropoutParameters random = {};
  ScratchDropoutParameters scratch = {};
};
```

The derived internal parameters are computed by the injection stage at runtime from
the `scale` value using the formulas in §Scale Mapping. They are not stored in the
model struct.

`Section` gains:

```cpp
DropoutParameters dropouts = {};
```

### `DropoutInjectionStage` (`dropout_injection_stage.h/.cpp`)

```
Module:  dropout_injection
Purpose: Applies per-section random and scratch dropout events to fixed-point mV
         Y/C buffers before output quantisation, and writes the dropout sidecar.
```

Public interface:

```cpp
class DropoutInjectionStage {
 public:
  explicit DropoutInjectionStage(ILogger* logger);

  // Opens the sidecar SQLite file (if any section has dropouts enabled).
  // Must be called before the first InjectDropouts call.
  bool Begin(const Project& project, std::vector<std::string>* errors);

  // Applies dropouts to y_mv/c_mv and appends records to the sidecar.
  void InjectDropouts(
      const Project& project,
      const std::vector<IGenerationStage::FrameScheduleItem>& schedule,
      std::size_t frame_offset,
      std::size_t frame_count,
      std::vector<SampleFixed>* y_mv,
      std::vector<SampleFixed>* c_mv);

  // Commits and closes the sidecar. No-op if Begin was not called or no
  // dropouts were written.
  bool Finalize(std::vector<std::string>* errors);
};
```

Internal state:

- `run_base_seed_` (random per pipeline construction; used when no seed is specified).
- SQLite database handle for the sidecar file (null when no dropout section exists).
- Cached scratch event tables per section (derived on first frame of each section,
  cleared when the section changes).

---

## Validation Rules

Added to `ProjectValidator::Validate`:

1. `random.scale` must be in [0, 20]. Out-of-range → validation error.
2. `scratch.scale` must be in [0, 20]. Out-of-range → validation error.
3. If `dropouts:` is present but both `random.scale` and `scratch.scale` are `0`
   (or both sub-keys are absent) → validation error (an empty or all-zero `dropouts:`
   block has no effect and is likely a configuration mistake).
4. If `scratch.scale > 0` and the `max_duration_frames` derived from that scale
   exceeds `section.duration_frames` → validation warning (the scratch lifespan will
   be truncated to the section length; the shape envelope will be clipped asymmetrically
   if the section is shorter than the scratch duration).

---

## Implementation Phases

### Phase 1 — Data Model and YAML

**Tasks:**

1. Add `RandomDropoutParameters`, `ScratchDropoutParameters`, and
   `DropoutParameters` structs to `model.h`. Add `DropoutParameters dropouts` field
   to `Section`.
2. Extend `YamlProjectParser` to parse the `dropouts:` block (`random:` and
   `scratch:` sub-keys with `scale` and optional `seed`; `severity` is not a
   YAML field).
3. Add scale-to-parameter helper functions (implementing the formulas from
   §Scale Mapping) as `inline` functions in a new header
   `include/videosynth/dropout_scale.h`, so they are testable independently of the
   injection stage.
4. Extend `ProjectValidator` with the dropout-specific validation rules above.
5. Add `dropout_path` derivation helper (strip `.meta`, append `.dropouts.meta`) to
   a utility function accessible by the injection stage.
6. Unit tests: parser round-trips for all scale/seed combinations (with and without
   optional `seed`); scale mapping functions produce expected values at boundary and
   mid-range scale inputs (scale 1, 10, 20); validator accepts valid configs;
   validator rejects out-of-range `scale`; validator warns on truncated scratch
   duration.

**Acceptance criteria:**

- Parser populates `Section::dropouts` correctly for all combinations (neither,
  `random:` only, `scratch:` only, both).
- Validator produces correct errors and warnings for all rule violations.
- All existing tests continue to pass.

---

### Phase 2 — Dropout Injection Stage and Sidecar

**Tasks:**

1. Implement `DropoutInjectionStage` in `src/dropout_injection_stage.cpp` and
   `include/videosynth/dropout_injection_stage.h`.
   - `Begin`: open (or create) the SQLite sidecar at the derived path; create the
     `dropout_run` table and index per schema version 5.
   - `InjectDropouts`: apply random and scratch algorithms; write `INSERT` rows per
     event.
   - `Finalize`: `COMMIT` the transaction and close the database handle.
2. Wire `DropoutInjectionStage` into `VideoSynthPipeline::Run` between
   `NoiseInjectionStage::InjectNoise` and `IOutputStage::AppendSamples`. Call
   `Begin` before the frame loop and `Finalize` after `FinalizeWrite`.
3. Unit tests:
   - With no `dropouts:` in any section, output equals input exactly and no sidecar
     is created.
   - With `random.scale > 0` and a fixed seed, affected samples are displaced
     toward either blanking or white by the drawn push fraction; unaffected
     samples are unchanged. A push toward blanking with `push_fraction = 1.0`
     drives the sample to exactly `blanking_mV`; a push toward white with
     `push_fraction = 1.0` drives it to exactly `white_mV`.
   - Direction and push fraction are consistent across batch boundaries: the
     same event drawn with the same per-frame seed always produces the same
     direction and push fraction.
   - Determinism: same section index, frame index, and `seed` always produce
     identical dropout positions and widths.
   - Scratch envelope — width: at frame 0 and at `max_duration_frames - 1` width
     is ~0; at the midpoint width equals `max_width_samples` within ±1 sample.
   - Scratch envelope — amplitude: at the first and last frames of a scratch
     event the signal displacement is near zero (current_push ≈ 0); at the
     midpoint current_push equals `max_push_fraction` within floating-point
     rounding. Both width and amplitude peak in the same frame.
   - Sidecar: `dropout_run` row count matches the number of sub-runs written across
     all frames; `sample_start + sample_count` does not exceed `samples_per_frame`.
   - Clamping: output samples remain within the legal fixed-point range.
   - **Severity — non-visible**: a dropout run that starts before the active picture
     sample range is written to the sidecar with `severity = 25`.
   - **Severity — visible**: a dropout run that starts within the active picture
     sample range is written with `severity = 75`.
   - **Severity — boundary split**: a run that spans the VBI/active-picture boundary
     produces two sidecar rows, the first with `severity = 25` and the second
     with `severity = 75`; together their `sample_count` values sum to the original
     run length.
   - **Overlap — scratch takes precedence**: when a random event fully overlaps a
     scratch interval, no random sidecar row is written for that region and the
     signal samples are set to blanking only once (not twice).
   - **Overlap — partial overlap splits the random run**: when a random event
     partially overlaps a scratch interval, the surviving sub-run(s) appear in the
     sidecar; scratch-covered samples are unaffected by the random pass.
   - **Overlap — no sidecar primary key violations**: for any frame produced when
     both `random:` and `scratch:` are active, every `(frame_id, sample_start)`
     pair in the sidecar is unique and no two rows have overlapping
     `[sample_start, sample_start + sample_count)` ranges.
   - **Overlap — random-vs-random merge**: when two random events for the same frame
     produce overlapping or adjacent sub-runs after scratch clipping, the merged
     result appears as a single sidecar row.

**Acceptance criteria:**

- `InjectDropouts` produces correctly blanked samples as verified by unit tests.
- Sidecar is conformant to schema version 5; rows survive a round-trip through an
  SQLite reader without constraint violations.
- Pipeline integration: a 50-frame PAL project with `random.scale = 12`
  (`frequency ≈ 4.1` events/frame) produces a sidecar with ~205 `dropout_run` rows
  (±2 σ of Poisson variance expected across 50 frames); all rows carry `severity`
  25 or 75 with no other values present.

---

### Phase 3 — HLD Update and Test Projects

**Tasks:**

1. Update `docs/design/high-level-design.md`:
   - Add `DropoutInjectionStage` to the architecture overview in Section 3.
   - Add dropout injection to the pipeline responsibilities table in Section 4.
   - Document the `dropouts:` YAML block in Section 7 (YAML Project File).
   - Add dropout validation rules to Section 13 (Error Handling and Validation).
2. Add `tests/projects/pal_dropout_ramp.yaml` — 9 sections × 8 frames, PAL, with
   noise and dropout parameters varying from clean to heavily degraded (see table
   below). Follows the same structure as `tests/projects/pal_noise_ramp.yaml`:
   uses `videosynth-assets/assets/exr/720x576/75_BARS.exr` as the source and
   includes a `uk-national` VITS injection on line 19 so White SNR remains
   measurable by orc-gui.
3. Add `tests/projects/ntsc_dropout_ramp.yaml` — 9 sections × 8 frames, NTSC,
   same degradation progression. Follows `tests/projects/ntsc_noise_ramp.yaml`:
   uses `videosynth-assets/assets/exr/720x486/75_BARS.exr`, `ntc7-composite` on
   line 17, and `virs` on line 21.
4. Output paths follow the existing convention:
   - PAL video: `tests/projects/output/videosynth_pal_dropout_ramp.composite`
   - PAL metadata: `tests/projects/output/videosynth_pal_dropout_ramp.meta`
   - PAL sidecar: `tests/projects/output/videosynth_pal_dropout_ramp.dropouts.meta`
   - NTSC equivalents with `ntsc` in place of `pal`.

Both files are placed in `tests/projects/` so that `run-projects.sh` picks them
up automatically alongside all other project fixtures.

**Degradation progression (both example projects, 8 frames per section):**

| Section | Name                | `noise_db` | `random.scale` | `scratch.scale` |
|---------|---------------------|:----------:|:--------------:|:---------------:|
| 1       | CleanReference      | —          | —              | —               |
| 2       | NearPristine        | 56.0       | 2              | —               |
| 3       | GoodLaserdisc       | 50.0       | 5              | 3               |
| 4       | FairLaserdisc       | 45.0       | 7              | 5               |
| 5       | GoodVhs             | 40.0       | 9              | 6               |
| 6       | AverageVhs          | 36.0       | 11             | 7               |
| 7       | PoorVhs             | 32.0       | 13             | 9               |
| 8       | WornTape            | 27.0       | 16             | 11              |
| 9       | SeverelyDegraded    | 22.0       | 19             | 13              |

Sidecar severity is computed automatically from sample position for all sections.

**Acceptance criteria:**

- HLD accurately reflects the implemented behaviour with no mismatches.
- Both projects pass `--validate-only`.
- `run-projects.sh` runs both new projects without error alongside all existing
  fixtures; sidecar files are created at the expected output paths.
- Sidecar files satisfy schema version 5 constraints (verified via
  `PRAGMA integrity_check` on the generated `.dropouts.meta` files).
- All existing tests continue to pass.
