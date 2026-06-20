# Noise Injection Design

**Scope**: Per-section additive noise injection for videosynth, targeting the
White SNR and Black PSNR metrics as measured by orc-gui.

---

## orc-gui Measurement Analysis

### Black PSNR (`BlackPSNRObserver`)

orc-gui measures Black PSNR from the blanking-level region of specific VBI
lines, where the nominal signal level is 0 IRE (blanking = black):

| Standard | Field-relative line | Start | Window |
|----------|--------------------:|------:|-------:|
| PAL      | 22                  | 12 µs | 50 µs  |
| NTSC     | 1                   | 10 µs | 20 µs  |

Source: `orc/core/observers/black_psnr_observer.cpp`

Formula:

```
PSNR_dB = 20 × log₁₀(100 / σ_ire)
```

where `σ_ire` is the standard deviation of the samples in that window converted
to IRE, and 100 IRE is the fixed peak-signal reference. Capped at 80 dB when
`σ_ire ≤ 0.001 IRE`.

PAL line 22 is a blanking-level VBI line (between post-equalising pulses and
the first VITS/injection line). NTSC line 1 is the first post-vsync blanking
line. Both regions carry no active signal content, so only noise contributes to
the standard deviation.

### White SNR (`WhiteSNRObserver`)

orc-gui measures White SNR from a VITS white-flag region where the nominal
signal level is ≈ 100 IRE:

| Standard | Field-relative line | Start | Window |
|----------|--------------------:|------:|-------:|
| PAL      | 19                  | 12 µs |  8 µs  |
| NTSC     | 20 (first match)    | 14 µs | 12 µs  |
| NTSC     | 20 (second try)     | 52 µs |  8 µs  |
| NTSC     | 13 (third try)      | 13 µs | 15 µs  |

Source: `orc/core/observers/white_snr_observer.cpp`

orc-gui validates that the measured mean lies within 90–110 IRE before
accepting the reading. If no window qualifies, the metric is not reported for
that field.

Formula:

```
SNR_dB = 20 × log₁₀(μ_ire / σ_ire)
```

where `μ_ire` is the mean and `σ_ire` the standard deviation of the window
samples in IRE. Because the white flag is nominally at 100 IRE, `μ_ire ≈ 100`
and the formula is functionally identical to the Black PSNR formula. Capped at
80 dB when `σ_ire ≤ 0.001 IRE`.

### Sample-to-IRE Conversion

Both observers convert 10-bit CVBS_U10_4FSC codes to IRE using the blanking
and white levels stored in `SourceParameters`:

```
ire = (code − blanking_level_10b) × 100 / (white_level_10b − blanking_level_10b)
```

Normative level anchors (from `cvbs_signal_constants.h`):

| Standard | Blanking code | White code | mV/IRE |
|----------|:-------------:|:----------:|-------:|
| PAL      | 256           | 844        | 7.000  |
| NTSC     | 240           | 800        | 7.143  |

---

## Noise Model

### Physical Motivation

With uniform zero-mean Gaussian noise of fixed standard deviation σ applied to
all samples, both White SNR and Black PSNR would yield the same dB value.
Real analogue recordings show White SNR < Black PSNR (white is noisier) because
noise in RF-demodulated composite video has a signal-proportional component
(bias current noise, RF detector non-linearity) in addition to the baseband
noise floor.

The model therefore uses two components:

- **Floor noise** (σ_f, mV): uniform zero-mean Gaussian, independent of signal
  level, applied to all samples (Y and C). Parameterised by `noise_db`.
- **Proportional noise** (coefficient k, dimensionless): zero-mean Gaussian
  whose standard deviation scales with the instantaneous Y-channel amplitude.
  Parameterised by `noise_spread_db`.

Because the proportional component can only add noise on top of the floor,
white is always at least as noisy as black. "Perfect white with noisy black"
is physically impossible in this model and in real analogue sources.

### Correlated Y/C Noise

In a real source (tape, disc) both Y and C channels originate from the same
physical medium read at the same point in time. The noise on the two channels
is therefore **correlated** — the same noise realisation is added to both
rather than independent samples drawn separately for each channel.

The implementation generates a **single noise sample per output sample
position** and adds it to both the Y and C buffers at that position. The
proportional component (which models RF-path effects on the luma carrier) is
derived from the Y level and added to the shared noise value before it is
applied to both channels.

Noise standard deviation at sample position `i`, Y level `Y_mV`:

```
σ_total_mV = sqrt(σ_f² + (k × Y_mV)²)
n_i         = N(0, σ_total_mV)    // single draw per sample position
Y_i        += n_i
C_i        += n_i
```

C samples therefore receive the same noise realisation as the corresponding Y
sample (floor component plus proportional component driven by Y level).

### Parameter Derivation

Given `noise_db` (the floor) and `noise_spread_db` (how many dB noisier white
is than black):

```
// 1. Floor noise — sets Black PSNR
σ_f_ire  = 100 / pow(10, noise_db / 20)
σ_f_mV   = σ_f_ire × (white_mV / 100)        // PAL: ×7.000  NTSC: ×7.143

// 2. White SNR target = noise_db − noise_spread_db
target_white_snr = noise_db - noise_spread_db
σ_w_ire          = 100 / pow(10, target_white_snr / 20)

// 3. Proportional noise coefficient
// σ_w² = σ_f² + (k × 100)²  →  solve for k
if σ_w_ire > σ_f_ire:
    σ_prop_ire = sqrt(σ_w_ire² - σ_f_ire²)
    k = σ_prop_ire / 100
else:
    k = 0   // spread = 0: pure floor noise
```

When `noise_spread_db = 0`, k = 0 and both White SNR and Black PSNR equal
`noise_db`.

### Expected orc-gui Readings

| Parameter          | orc-gui metric    | Expected reading        |
|--------------------|-------------------|-------------------------|
| `noise_db`         | Black PSNR        | ≈ `noise_db`            |
| `noise_db − noise_spread_db` | White SNR | ≈ `noise_db − noise_spread_db` |

The relationship is exact for an infinite number of samples. With a finite
measurement window orc-gui introduces statistical variance, particularly for the
PAL white window (8 µs × 277 kHz ≈ 125 samples at 4fsc). The injected values
should therefore be treated as targets; measured values will vary by
±0.5–1.0 dB per field at typical noise levels.

---

## YAML Schema Addition

Noise parameters are optional and per-section. They sit under a `noise:` sub-key:

```yaml
sections:
  - name: "Noisy section"
    type: progressive
    source: "assets/source.mkv"
    duration_frames: 100
    noise:
      noise_db: 48.0          # Noise floor: sets Black PSNR target (dB)
      noise_spread_db: 4.0    # White is 4 dB noisier than black; White SNR = 44 dB
```

Both keys are optional:

- Neither present → no noise injection (pass-through).
- Only `noise_db` present → floor noise only (`noise_spread_db` defaults to 0,
  k = 0); White SNR = Black PSNR = `noise_db`.
- Both present → two-component noise model.
- `noise_spread_db` without `noise_db` → validation error.

### Valid Range

**`noise_db`: 20.0–61.0 dB**

- **Lower bound — 20.0 dB (sync detection limit)**: at 20 dB,
  `σ_noise ≈ 10 IRE ≈ 70 mV` (PAL). The PAL sync pulse is 300 mV (42.9 IRE)
  deep; below 20 dB the noise amplitude approaches the sync pulse amplitude and
  sync separators in downstream decoders begin to fail.
- **Upper bound — 61.0 dB (10-bit quantisation floor)**: 1 LSB = 1.1905 mV
  (PAL), giving `σ_q ≈ 0.5 LSB ≈ 0.085 IRE` and a quantisation-limited PSNR
  of `20 × log₁₀(100 / 0.085) ≈ 61 dB`. Any `noise_db` above 61 dB would
  require injected noise smaller than the quantisation noise already present in
  the output and would have no measurable effect in orc-gui.

**`noise_spread_db`: 0.0 – (`noise_db` − 20.0) dB**

- Must be ≥ 0 (white cannot be cleaner than black).
- `noise_db − noise_spread_db` must be ≥ 20.0 (White SNR floor limit).
- Maximum useful spread in practice is ~10 dB; real recordings rarely exceed
  this difference between the two metrics.

**Practical reference points:**

| Source / condition        | `noise_db` (Black PSNR) | Typical `noise_spread_db` |
|---------------------------|:-----------------------:|:-------------------------:|
| Near-quantisation clean   | ~58–61 dB               | ~4 dB                     |
| Excellent laserdisc       | 52–58 dB                | 4–6 dB                    |
| Good laserdisc            | 46–52 dB                | 4–6 dB                    |
| Fair laserdisc / good VHS | 40–46 dB                | 4–6 dB                    |
| Poor VHS / worn tape      | 32–40 dB                | 4–8 dB                    |
| Barely watchable          | 22–32 dB                | 0–4 dB                    |
| Sync borderline           | ~20 dB                  | 0 dB                      |

---

## Pipeline Placement

Noise injection occurs **after `GenerateFrameBatch`** and **before
`WriteFrameBatch`**, operating on the fixed-point mV Y and C buffers. This
ensures:

- Noise is present in all regions orc-gui inspects (VBI lines, active picture,
  blanking), because all regions are synthesised in the generation stage.
- Quantisation happens after noise injection, so the injected noise is present
  in the output 10-bit codes that orc-gui reads.
- The generation stage remains noise-free and testable independently.

Pipeline call sequence in `pipeline.cpp`:

```
generate_->GenerateFrameBatch(...)  // fills y_mv, c_mv
noise_->InjectNoise(project, schedule, frame_offset, frame_count, &y_mv, &c_mv)
output_->WriteFrameBatch(y_mv, c_mv)
```

`NoiseInjectionStage` is a concrete class (no virtual interface required, since
it has no external dependencies beyond the RNG). It is constructed in `main.cpp`
and passed to `VideoSynthPipeline` by pointer.

---

## Class Design

### `NoiseParameters` (in `model.h`)

Added to `Section`:

```cpp
struct NoiseParameters {
  bool enabled = false;
  double noise_db        = 61.0;  // noise floor; valid range [20.0, 61.0]
  double noise_spread_db = 0.0;   // white noisier than black by this many dB; ≥ 0
};
```

`Section` gains:

```cpp
NoiseParameters noise = {};
```

### `NoiseInjectionStage` (`noise_injection_stage.h/.cpp`)

```
Module:  noise_injection
Purpose: Applies per-section two-component Gaussian noise to fixed-point mV
         Y/C frame buffers before output quantisation.
```

Public interface:

```cpp
class NoiseInjectionStage {
 public:
  explicit NoiseInjectionStage(ILogger* logger);

  void InjectNoise(
      const Project& project,
      const std::vector<IGenerationStage::FrameScheduleItem>& schedule,
      std::size_t frame_offset,
      std::size_t frame_count,
      std::vector<SampleFixed>* y_mv,
      std::vector<SampleFixed>* c_mv);
};
```

Internal per-batch algorithm:

1. Determine which `Section` owns each frame in the batch
   (`schedule[frame_offset + i].section`).
2. Skip frames whose section has `noise.enabled == false`.
3. For each frame in the batch:
   a. Derive `σ_f_mv` and `k` from `noise.noise_db` and `noise.noise_spread_db`
      using the parameter derivation above.
   b. Seed `std::mt19937_64` with `hash(section_index, frame_global_index)` for
      deterministic, reproducible output.
   c. Iterate over every sample position in the frame. At each position draw a
      **single** noise value and add it to both channels:
      - Compute `σ_total_mV = sqrt(σ_f_mv² + (k × Y_mV)²)` where `Y_mV` is
        the current Y sample converted from fixed-point.
      - Draw `n = N(0, σ_total_mV)`.
      - Add `n` to both `Y[i]` and `C[i]`.
4. Clamp results to the legal fixed-point range to prevent downstream
   quantisation overflow.

### RNG Seeding

Seed per frame (not per batch) to ensure reproducibility regardless of batch
size:

```
seed = (section_index * 2654435761ULL) ^ (frame_global_index * 2246822519ULL)
```

The seed is deterministic given project content, so the same project file always
produces the same output.

---

## Validation Rules

Added to `ProjectValidator::Validate`:

1. `noise.noise_db` must be in [20.0, 61.0].
   - Below 20.0 dB: noise amplitude approaches sync pulse amplitude; sync
     detection becomes unreliable.
   - Above 61.0 dB: injected noise would be smaller than the 10-bit
     quantisation noise floor and has no measurable effect.
2. `noise.noise_spread_db` must be ≥ 0.0.
3. `noise.noise_db − noise.noise_spread_db` must be ≥ 20.0 (White SNR floor
   limit); violation yields a validation error.
4. `noise_spread_db` without `noise_db` yields a validation error.
5. If the section has no VITS white-flag injection on the lines orc-gui
   inspects, White SNR cannot be measured by orc-gui. Emit a validation warning
   that the white SNR target will not be verifiable without a suitable VITS
   injection on the correct line for the standard (PAL: line 19; NTSC: line 20).

---

## Implementation Phases

### Phase 1 — Data Model and YAML

**Tasks:**

1. Add `NoiseParameters` struct and `noise` field to `Section` in `model.h`.
2. Extend `YamlProjectParser` to parse the `noise:` block from section YAML.
3. Extend `ProjectValidator` with the noise-specific validation rules above.
4. Unit tests: parser round-trips for all key combinations; validator accepts
   valid configs; validator rejects `noise_spread_db` without `noise_db`,
   out-of-range `noise_db`, negative `noise_spread_db`, and spread that drives
   white SNR below 20.0 dB.

**Acceptance criteria:**
- Parser populates `Section::noise` correctly for all key combinations (neither,
  `noise_db` only, both).
- Validator produces correct errors and warnings for all rule violations.
- All existing tests continue to pass.

---

### Phase 2 — Noise Injection Stage

**Tasks:**

1. Implement `NoiseInjectionStage` in `src/noise_injection_stage.cpp` and
   `include/videosynth/noise_injection_stage.h`.
2. Wire into `VideoSynthPipeline::Run` between `GenerateFrameBatch` and
   `WriteFrameBatch`.
3. Unit tests:
   - With `noise.enabled = false`, output equals input exactly.
   - With `noise_spread_db = 0` (k = 0), measured noise std on both Y and C
     buffers at blanking level matches `σ_f_mV` within 5 % for ≥ 100 000
     samples; Y and C noise at each position are equal (correlated).
   - With `noise_spread_db > 0`, noise std on Y at white level matches
     `sqrt(σ_f_mV² + (k × white_mV)²)` within 5 %; C noise at white matches
     Y (same draw).
   - Same project file and frame index always produces identical output (RNG
     determinism).
   - Clamping: output samples remain within the legal fixed-point range.

**Acceptance criteria:**
- `InjectNoise` produces statistically correct noise amplitudes as verified by
  the unit tests above.
- Pipeline integration: a project with `noise:` set produces output that, when
  opened in orc-gui, reports White SNR and Black PSNR within ±1 dB of the
  target values when at least 50 frames are present and suitable VITS is active.

---

### Phase 3 — HLD Update and Example Projects

**Tasks:**

1. Update `docs/design/high-level-design.md`:
   - Add noise injection to the pipeline responsibilities table in Section 4.
   - Add `NoiseInjectionStage` to the architecture overview in Section 3.
   - Document the `noise:` YAML block in Section 7 (YAML Project File).
   - Add noise validation rules to Section 13 (Error Handling and Validation).
2. Add `docs/examples/pal_noise_ramp.yaml` — 10 sections × 8 frames, PAL,
   noise ramping from clean to near-sync-limit (see table below).
3. Add `docs/examples/ntsc_noise_ramp.yaml` — 10 sections × 8 frames, NTSC,
   same noise progression.

Both example projects include VITS injections on the lines orc-gui inspects
for White SNR measurement (PAL: `uk-national` on line 19; NTSC: `ntc7-composite`
on line 17 and `virs` on line 21 as per the existing NTSC VITS convention), so
all sections can be measured end-to-end in orc-gui.

**Noise progression (both projects):**

| Section | Name               | `noise_db` | `noise_spread_db` | Black PSNR | White SNR |
|---------|--------------------|:----------:|:-----------------:|:----------:|:---------:|
| 1       | Clean reference    | —          | —                 | —          | —         |
| 2       | Near-quantisation  | 58.0       | 4.0               | ~58 dB     | ~54 dB    |
| 3       | Excellent source   | 53.0       | 4.0               | ~53 dB     | ~49 dB    |
| 4       | Good source        | 48.0       | 4.0               | ~48 dB     | ~44 dB    |
| 5       | Fair source        | 43.0       | 4.0               | ~43 dB     | ~39 dB    |
| 6       | Typical VHS        | 38.0       | 4.0               | ~38 dB     | ~34 dB    |
| 7       | Poor tape          | 33.0       | 4.0               | ~33 dB     | ~29 dB    |
| 8       | Very poor          | 28.0       | 4.0               | ~28 dB     | ~24 dB    |
| 9       | Barely watchable   | 24.0       | 4.0               | ~24 dB     | ~20 dB    |
| 10      | Sync borderline    | 20.0       | 0.0               | ~20 dB     | ~20 dB    |

Section 1 has no `noise:` block (clean pass-through). Sections 2–9 use a
consistent `noise_spread_db` of 4.0 dB, representative of typical analogue
source recordings. Section 10 uses `noise_spread_db = 0` because white SNR is
already at the 20.0 dB floor.

**Acceptance criteria:**
- HLD accurately reflects the implemented behaviour with no mismatches.
- Both example projects pass `--validate-only`.
- Both example projects complete full generation without errors.
- All existing tests continue to pass.
