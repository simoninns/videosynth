# Biphase Line Injection Implementation Plan

**Document ID:** VS-DESIGN-BIPHASE-001  
**Related HLD Section:** 8.2  
**Related Specifications:** IEC 60856 (PAL), IEC 60857 (NTSC)

---

## Background

HLD Section 8.2 specifies laserdisc biphase encoding with CAV/CLV modes and section types (lead-in, lead-out, programme area).

**IEC Requirements:**
- PAL (60856): 24-bit biphase in VBI lines 6-18, 319-331; bit cell 2.0 μs ± 0.01 μs; 30%-100% white (700mV)
- NTSC (60857): 24-bit biphase in VBI lines 10-18, 273-281; 40-bit FM on lines 10/273, 11/274

**Current State:**
- ✅ YAML parser supports `laserdisc` injection type
- ✅ Data model includes `disc_type` and `codes` list
- ✅ Validation enforces laserdisc line ranges
- ✅ Validation prevents VITC + laserdisc coexistence
- ❌ Biphase signal generation not implemented
- ❌ Code type processing not implemented
- ❌ Line placement logic not implemented
- ❌ Field-aware injection not implemented

---

## Compliance Rules Summary

**Critical IEC-Specified Constraints (Must Be Enforced):**

1. **Section Type Restrictions:** Code types are strictly bound to specific section types (lead-in, programme_area, lead-out)
2. **Disc Type Restrictions:** Some code types are exclusive to CAV or CLV discs
3. **Timecode Continuity:** Timecodes (picture_number, programme_time_code, clv_picture_number) run continuously from programme area start to end without resetting
4. **Chapter Independence:** Chapter codes do NOT reset timecode counters; all timecodes continue across chapter boundaries
5. **NTSC Frozen Values:** Picture numbers and programme time codes are frozen during lead-in (zero) and lead-out (last value)
6. **Chapter Stop-Bit:** Chapters have a stop-bit (first bit after key) that is 0 for first 400 tracks, then 1 for subsequent tracks; first chapter after lead-in MUST have stop-bit = 1
7. **Minimum Lengths:** Chapters ≥ 30 tracks, lead-in ≥ 1.5mm, lead-out ≥ 2mm (PAL) or 600 tracks (NTSC)
8. **Value Range Constraints:** Each code type has specific hex digit range constraints (e.g., picture_number digits 0-9, users_code X₁=0-7)
9. **40-bit FM Differences:** Separate transition times (135ns ± 15ns), bit layout, and bit cell specifications

---

## Phase 1: Foundation and Data Model

**Dependencies:** None

**Purpose:** Establish the fundamental data structures and YAML parsing support for biphase injection.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 1.1 | Add `section_type` enum (lead_in, programme_area, lead_out) to model.h | Extended model.h |
| 1.2 | Add biphase configuration structure to CvbsPresets | Updated model.h |
| 1.3 | Define code type enums for CAV and CLV | New biphase_types.h |
| 1.4 | Add section type field to YAML parser | Updated yaml_project_parser.cpp |
| 1.5 | Update validation rules for section types | Updated project_validator.cpp |
| 1.6 | Add section type to line injection data model | Updated model.h |

**Validation:**
- All existing tests pass
- New data structures compile without warnings
- YAML parser handles section_type field
- Validator rejects invalid section type combinations

---

## Phase 2A: Biphase Encoder Core

**Dependencies:** Phase 1

**Purpose:** Implement the 24-bit biphase signal generation with IEC-compliant timing and levels.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 2.1 | Create BiphaseEncoder class with signal generation | New biphase_encoder.h/cpp |
| 2.2 | Implement bit cell timing (2μs ± 0.01μs) | BiphaseEncoder methods |
| 2.3 | Integrate signal_shaping.h for compliant transitions | BiphaseEncoder methods |
| 2.4 | Implement transition generation (225ns ± 25ns) using ShapedPulseLevel/ShapedGateEnvelope | BiphaseEncoder methods |
| 2.5 | Implement level mapping (30%-100% white / 0-100 IRE) | BiphaseEncoder methods |
| 2.6 | Create hex-to-biphase conversion utility | New biphase_utils.h/cpp |
| 2.7 | Implement key nibble generation (starts with 1) | BiphaseEncoder method |
| 2.8 | Unit tests for biphase encoding | test_biphase_encoder.cpp |

**Technical Details:**
```cpp
class BiphaseEncoder {
public:
    BiphaseEncoder(int sample_rate,
                   double bit_cell_duration_us = 2.0,
                   double transition_duration_ns = 225.0);
    
    std::vector<Sample> GenerateLine(
        const std::string& hex_code,
        Standard standard,
        double baseline_level_mv,
        double peak_level_mv) const;
    
    std::vector<Sample> Generate24BitCode(
        uint32_t code_value,
        Standard standard,
        double baseline_level_mv,
        double peak_level_mv) const;
    
    std::vector<Sample> GenerateBit(
        bool bit_value,
        double baseline_level_mv,
        double peak_level_mv) const;

private:
    int ramp_samples_;       // From TransitionTimeToRampSamples()
    int bit_cell_samples_;   // Calculated from bit_cell_duration_us
};
```

**Implementation Notes:**
- Must use `TransitionTimeToRampSamples()` from `signal_shaping.h`
- Must use `ShapedPulseLevel()` for bit transitions
- Must use `ShapedGateEnvelope()` for bit cell envelopes
- Transition duration: 225ns ± 25ns
- Bit cell duration: 2.0μs ± 0.01μs

**Validation:**
- Biphase waveform matches IEC specifications
- Timing accurate to within ±1% (bit cell) and ±10% (transitions)
- Level mapping correct for both PAL and NTSC
- All unit tests pass

---

## Phase 2B: FM Encoder Core

**Dependencies:** Phase 1

**Purpose:** Implement the 40-bit FM signal generation for NTSC with IEC-compliant timing.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 2.9 | Create FmEncoder class for 40-bit FM signal generation | New fm_encoder.h/cpp |
| 2.10 | Implement 40-bit FM transition generation (135ns ± 15ns) | FmEncoder methods |
| 2.11 | Implement white flag generation (100 IRE constant level) | FmEncoder method |
| 2.12 | Unit tests for FM encoding | test_fm_encoder.cpp |

**Technical Details:**
```cpp
class FmEncoder {
public:
    FmEncoder(int sample_rate,
              double bit_cell_duration_us = 2.0,
              double transition_duration_ns = 135.0);  // 40-bit FM uses 135ns
    
    std::vector<Sample> Generate40BitCode(
        const FmData& data,
        Standard standard) const;
    
    std::vector<Sample> GenerateWhiteFlag(
        Standard standard) const;

private:
    int fm_ramp_samples_;    // From TransitionTimeToRampSamples(135e-9, ...)
    int fm_bit_cell_samples_;
};
```

**Implementation Notes:**
- Must use `TransitionTimeToRampSamples(135e-9, sample_rate, 0.1, 0.9)` for 10%-90% transitions
- Transition duration: **135ns ± 15ns** (IEC 60857 Figure 13)
- Bit cell duration: 2.0μs ± 0.01μs
- White flag: 100 IRE level (constant, not modulated)

**Validation:**
- 40-bit FM waveform matches IEC specifications
- Transition timing accurate to within ±10%
- All unit tests pass

---

## Phase 3A: CAV Code Type Implementation

**Dependencies:** Phase 2A

**Purpose:** Implement all CAV-specific code types with their constraints and validation.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 3.1 | Create CodeGenerator base class | New code_generator.h |
| 3.2 | Implement CAV code types (lead_in, lead_out, picture_number, picture_stop) | cav_code_generator.h/cpp |
| 3.4 | Add auto-increment logic for picture numbers | CodeGenerator methods |
| 3.9 | Implement chapter stop-bit generation (0 for first 400 tracks, 1 for subsequent) | CodeGenerator methods |
| 3.10 | Ensure first chapter after lead-in has stop-bit = 1 | CodeGenerator validation |
| 3.12 | Implement picture number range validation (PAL: 0-99999, NTSC: 0-79999) | CodeGenerator validation |
| 3.13 | Implement picture number decimal digit validation (X₁-X₅ = 0-9 hex digits) | PictureNumberGenerator |
| 3.14 | Implement chapter number encoding formula: Chapter = (X₁ & 7)×16 + X₂ | ChapterCodeGenerator |
| 3.15 | Implement chapter stop-bit extraction: Stop-bit = (X₁ & 8) >> 3 | ChapterCodeGenerator |
| 3.28 | Unit tests for CAV code types | test_cav_code_generators.cpp |

**CAV Code Types:**
- lead_in (88FFFF) - **lead_in section only**
- lead_out (80EEEE) - **lead_out section only**
- picture_number (FX₁X₂X₃X₄X₅) - auto-increment, **programme_area only, CAV only**
  - **Constraints**: X₁-X₅ = 0-9 (decimal digits as hex), Max: **99,999** (PAL) / **79,999** (NTSC)
- picture_stop (82CFFF) - **programme_area only, CAV only**
- chapter_number (8X₁X₂DDD) - **programme_area only**, max 79
  - **Encoding**: Chapter = (X₁ & 7)×16 + X₂, Stop-bit = (X₁ & 8) >> 3
- programme_status (8DC/BA X₃X₄X₅) - **programme_area only**
  - **Note**: DC = CX on, BA = CX off (see Appendix C for Hamming code)
- users_code (8X₁DX₃X₄X₅) - **lead_in or lead_out only**
  - **Constraints**: **X₁ = 0-7 only** (not 0-F)

**Validation:**
- All CAV code types generate correct hex values
- Auto-increment logic works correctly
- Chapter stop-bit logic complies with IEC specifications
- First chapter after lead-in has stop-bit = 1
- All value range constraints are enforced
- All unit tests pass

---

## Phase 3B: CLV Code Type Implementation

**Dependencies:** Phase 2A

**Purpose:** Implement all CLV-specific code types with their constraints and validation.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 3.3 | Implement CLV code types (lead_in, lead_out, programme_time_code, clv_code, clv_picture_number) | clv_code_generator.h/cpp |
| 3.5 | Add auto-increment logic for CLV time codes | ClvCodeGenerator methods |
| 3.11 | Enforce minimum chapter length (30 tracks) validation | Validator methods |
| 3.16 | Implement CLV picture number format validation (X₁=A-F, X₃=0-9, X₄=0-2, X₅=0-9) | ClvPictureNumberGenerator |
| 3.17 | Implement CLV picture number time calculation: Seconds = (X₁-'A')×10+X₃, Frame = X₄×10+X₅ | ClvPictureNumberGenerator |
| 3.18 | Implement programme time code BCD validation (X₂=0-5, X₃=0-9) | TimeCodeGenerator |
| 3.19 | Implement users code X₁ range validation (0-7 only) | UsersCodeGenerator |
| 3.29 | Unit tests for CLV code types | test_clv_code_generators.cpp |

**CLV Code Types:**
- lead_in (88FFFF) - **lead_in section only**
- lead_out (80EEEE) - **lead_out section only**
- programme_time_code (FX₁DDX₂X₃) - auto-increment, **programme_area only, CLV only**
  - **Constraints**: X₁ = hours (0-F), **X₂ = minutes tens (0-5)**, **X₃ = minutes units (0-9)** (BCD-encoded)
- clv_code (87FFFF) - **programme_area only, CLV only**
- clv_picture_number (8X₁EX₃X₄X₅) - auto-increment, **programme_area only, CLV only**
  - **Constraints**: **X₁ = A-F**, **X₃ = 0-9**, **X₄ = 0-2**, **X₅ = 0-9**
  - **Time Calculation**: Seconds = (X₁ - 'A')×10 + X₃, Frame within second = X₄×10 + X₅ (0-29)
- chapter_number (8X₁X₂DDD) - **programme_area only**, max 79
  - **Encoding**: Chapter = (X₁ & 7)×16 + X₂, Stop-bit = (X₁ & 8) >> 3
- programme_status (8DC/BA X₃X₄X₅) - **programme_area only**
- users_code (8X₁DX₃X₄X₅) - **lead_in or lead_out only**
  - **Constraints**: **X₁ = 0-7 only** (not 0-F)

**Validation:**
- All CLV code types generate correct hex values
- Auto-increment logic works correctly for programme_time_code and clv_picture_number
- All value range constraints are enforced
- All unit tests pass

---

## Phase 3C: FM Code Types and Validation

**Dependencies:** Phase 2B, Phase 3A, Phase 3B

**Purpose:** Implement NTSC 40-bit FM code types and comprehensive validation across all code types.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 3.6 | Implement programme status code with Hamming code | status_code_generator.h/cpp |
| 3.7 | Implement 40-bit FM coded signal (NTSC only) | fm_code_generator.h/cpp |
| 3.20 | Implement 40-bit FM bit layout per IEC Figure 13 | FmCodeGenerator |
| 3.21 | Implement white flag automatic placement for duplicate fields | FmCodeGenerator |
| 3.22 | Implement continuous timecode counters that start at programme area beginning | CodeGenerator state management |
| 3.23 | Ensure timecodes do NOT reset at chapter boundaries | CodeGenerator logic |
| 3.24 | Implement NTSC frozen values: picture_number=0 during lead-in, frozen at last value during lead-out | FmCodeGenerator methods |
| 3.25 | Implement NTSC frozen values: programme_time=0:00 during lead-in, frozen at last value during lead-out | FmCodeGenerator methods |
| 3.26 | Track chapter stop-bit state across frames | ChapterCodeGenerator state |
| 3.27 | Unit tests for timecode continuity across chapters | test_timecode_continuity.cpp |
| 3.30 | Unit tests for chapter stop-bit behavior | test_chapter_stopbit.cpp |
| 3.31 | Unit tests for NTSC frozen values | test_ntsc_frozen.cpp |

**NTSC 40-bit FM:**
- fm_picture_number - CAV picture numbers, lines 10/273, **programme_area only**
  - **Format**: 40-bit FM with bit layout per Appendix G
  - **Max**: 99,999
- fm_programme_time - CLV programme time, lines 10/273, **programme_area only**
  - **Format**: 40-bit FM with bit layout per Appendix G
  - **X₅ mode**: A=lead-in, B=lead-in+100f, D=picture, C=lead-out
- fm_white_flag - First field marker, lines 11/274, **programme_area only**
  - **Auto-placement**: Automatically placed on first field of next picture when fields are identical

**Historical Note (Legacy Support):**
- During the first years after introduction, picture stop was also indicated by the first bit of X₁ in the picture number code FX₁X₂X₃X₄X₅ (0=stop, 1=normal). This is **obsolete** but may be needed for compatibility with early players.

**Timecode Continuity Rules (IEC Compliant):**

1. **Continuous Counting:** picture_number (CAV), programme_time_code (CLV), and clv_picture_number (CLV) counters **MUST** start at the first frame of the programme area and increment continuously through to the last frame **without resetting**.

2. **Chapter Independence:** Chapter codes do **NOT** reset or affect any timecode counters. All timecodes continue incrementing across chapter boundaries.

3. **NTSC Frozen Behavior:**
   - During **lead-in**: picture_number = 0, programme_time = 0:00, fm_picture_number = 0, fm_programme_time = 0:00
   - During **lead-out**: picture_number and programme_time are **frozen** at their last programme area values
   - White flag continues to be generated during programme area only

4. **First Picture Number:** CAV picture_number starts at **1** at the beginning of the active programme (first frame of programme_area)

5. **Programme Time Code Start:** CLV programme_time_code starts at **0:00** (hours: 0, minutes: 0) at the beginning of the active programme

**Chapter Stop-Bit Rules (IEC Compliant):**

1. Each chapter number has a **stop-bit** (the first bit after the key nibble, which is bit 4 of the 24-bit code = MSB of X₁)
2. Stop-bit = **0** for the first **400 tracks** of the chapter
3. Stop-bit = **1** for all subsequent tracks until the next chapter
4. **Exception:** The first chapter directly after the lead-in area **MUST** have stop-bit = **1** (no zero stop-bit period)
5. On disks with chapters shorter than 800 tracks, the stop-bit of each chapter number **shall have the logic value "one"** (i.e., no stop-bit = 0 period)
6. **Minimum chapter length:** 30 tracks

**Implementation Formula:**
```cpp
// For chapter code 8X₁X₂DDD:
uint8_t stop_bit = (X₁ & 0x08) >> 3;      // Extract MSB of X₁ (bit 4 of 24-bit code)
uint8_t chapter_number = (X₁ & 0x07) * 16 + X₂;  // Use lower 3 bits of X₁ + full X₂
```

**Validation:**
- Programme status code includes valid Hamming code (Appendix C)
- 40-bit FM codes work for NTSC with correct bit layout
- Chapter stop-bit logic complies with IEC specifications
- Timecodes run continuously without resetting
- Chapters do not reset timecodes
- All value range constraints are enforced
- All unit tests pass

---

## Phase 4: Field-Aware Line Placement

**Dependencies:** Phase 3C

**Purpose:** Implement intelligent line placement that respects field awareness, code type priorities, and IEC line allocations.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 4.1 | Create LinePlacementEngine class | line_placement_engine.h/cpp |
| 4.2 | Implement field detection (first vs second) | LinePlacementEngine methods |
| 4.3 | Map code types to lines for PAL CAV | PAL placement tables |
| 4.4 | Map code types to lines for PAL CLV | PAL placement tables |
| 4.5 | Map code types to lines for NTSC CAV | NTSC placement tables |
| 4.6 | Map code types to lines for NTSC CLV | NTSC placement tables |
| 4.7 | Implement priority system | LinePlacementEngine logic |
| 4.8 | Handle lead-in/lead-out section transitions | LinePlacementEngine methods |
| 4.9 | Unit tests for line placement | test_line_placement.cpp |

**Line Allocation:**

**PAL:**
- Field 1: Lines 6-18 reserved (active biphase: 16-18)
- Field 2: Lines 319-331 reserved (active biphase: 329-331)
- Lines 6-15, 320-328: Reserved, video content at blanking level

**NTSC:**
- Field 1: Lines 10-18 reserved (active biphase: 16-18)
- Field 2: Lines 273-281 reserved (active biphase: 279-281)
- Lines 10/273: 40-bit FM coded signal
- Lines 11/274: White flag (100 IRE)
- Lines 12-15, 275-278: Reserved, video content at blanking level

**Priority Rules (IEC Compliant):**
1. **Absolute Priority:** Lead-in/lead-out codes have **absolute priority** in their respective sections (override all other codes)
2. **Picture Stop Priority:** Picture stop code > programme status code on **all** lines (not just 329/279)
3. **Users Code Restriction:** Users code **only** appears in lead-in/lead-out areas (never in programme area) - enforced by section type validation
4. **Chapter Code Exclusion (CAV):** Chapter codes **cannot** share lines with picture_number on CAV discs
5. **Chapter Code Exclusion (CLV):** Chapter codes **cannot** share lines with programme_time_code or clv_picture_number on CLV discs
6. **Chapter Yields to Picture:** Chapter codes yield to picture numbers on same lines (when both allowed)
7. **CLV Priority:** Programme time code > chapter codes on lines 17,18,280,281 (CLV)
8. **CLV Picture Priority:** CLV picture number > chapter codes on lines 16,279 (CLV)

**Line Placement Logic (Conditional):**

**PAL CAV:**
- picture_number: lines 17,18 OR 330,331 (depending on which field is first of the picture)
- picture_stop: lines 16,17 OR 329,330 (field immediately following picture number field)
- chapter_number: lines 17,18,330,331 **where no picture_number is inserted**; for lines 17 and 330, picture_stop has priority
- programme_status: lines 16,329 (picture_stop has priority)
- users_code: lines 16,329 **in lead-in/lead-out only**

**PAL CLV:**
- programme_time_code: lines 17,18 OR 330,331 (depending on which field is first)
- clv_code: line 17 OR 330 **where no programme_time_code or clv_picture_number is inserted**
- clv_picture_number: line 16 OR 329 (depending on which field is first)
- chapter_number: line 18 OR 331 **where no programme_time_code or clv_picture_number is inserted**
- programme_status: line 16 OR 329 **in same fields where CLV code is inserted**
- users_code: lines 16,329 **in lead-in/lead-out only**

**NTSC CAV:**
- picture_number: lines 17,18 OR 280,281 (depending on which field is first)
- picture_stop: lines 16,17 OR 279,280 (field immediately following picture number field)
- chapter_number: lines 17,18,280,281 **where no picture_number is inserted**; for lines 17 and 280, picture_stop has priority
- programme_status: lines 16,279 (picture_stop has priority)
- users_code: lines 16,279 **in lead-in/lead-out only**
- fm_picture_number: lines 10,273 (40-bit FM)
- fm_white_flag: lines 11,274 (100 IRE)

**NTSC CLV:**
- programme_time_code: lines 17,18 OR 280,281 (depending on which field is first)
- clv_code: line 17 OR 280 **where no programme_time_code or clv_picture_number is inserted**
- clv_picture_number: line 16 OR 279 (depending on which field is first)
- chapter_number: line 18 OR 281 **where no programme_time_code or clv_picture_number is inserted**
- programme_status: line 16 OR 279 **in same fields where CLV code is inserted**
- users_code: lines 16,279 **in lead-in/lead-out only**
- fm_programme_time: lines 10,273 (40-bit FM)
- fm_white_flag: lines 11,274 (100 IRE)

**Validation:**
- Correct line assignment for all code types
- Field-aware placement works correctly
- Priority system resolves conflicts properly
- Conditional line placement based on code type presence
- All unit tests pass

---

## Phase 5A: Biphase Injection Manager

**Dependencies:** Phase 4

**Purpose:** Create the central manager class that orchestrates biphase injection into the video pipeline.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 5.1 | Create BiphaseInjectionManager class | biphase_injection_manager.h/cpp |
| 5.2 | Integrate with GenerationStage | Updated generation_stage.cpp |
| 5.3 | Add biphase injection to line processing | GenerationStage methods |
| 5.6 | Handle NTSC 40-bit FM + biphase coexistence | GenerationStage logic |
| 5.7 | Handle PAL pilot burst + biphase compatibility | GenerationStage checks |
| 5.32 | Unit tests for injection manager | test_biphase_injection_manager.cpp |

**BiphaseInjectionManager Interface:**
```cpp
class BiphaseInjectionManager {
public:
    void ProcessFrame(
        FrameBuffer* y_buffer,
        FrameBuffer* c_buffer,
        const Section& section,
        int frame_index,
        Standard standard,
        int sample_rate) const;
    
    void ProcessLine(
        LineBuffer* line_buffer,
        int line_number,
        int field_index,
        const Section::LineInjection& injection,
        Standard standard,
        int sample_rate) const;
    
    bool ValidateSection(
        const Section& section,
        Standard standard,
        std::vector<std::string>* errors) const;
};
```

**Integration Points:**
- GenerationStage calls BiphaseInjectionManager for each frame
- Line injections applied after sync/burst but before VBI content
- Biphase signals added to luma channel at appropriate levels
- Section type transitions handled correctly (lead-in → programme → lead-out)
- 40-bit FM and 24-bit biphase generated simultaneously for NTSC

**Validation:**
- Biphase codes appear on correct lines
- Integration with existing pipeline works
- All unit tests pass

---

## Phase 5B: Validation Integration

**Dependencies:** Phase 5A

**Purpose:** Extend the project validator to enforce all biphase-specific rules and constraints.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 5.4 | Extend validator for section types | Updated project_validator.cpp |
| 5.5 | Add validation for code type parameters | Validator methods |
| 5.8 | Integration tests | test_biphase_integration.cpp |
| 5.9 | Validate minimum section durations (lead-in ≥ 1.5mm, lead-out ≥ 2mm/600 tracks) | Validator methods |
| 5.10 | Implement track-to-frame conversion for duration validation | Validator helper functions |
| 5.33 | Unit tests for validation | test_biphase_validation.cpp |

**Track-to-Frame Conversion (IEC 60856 §4.15):**
- Track pitch: **1.4 μm to 2.0 μm** (nominal)
- For validation purposes, use **1.6 μm** as nominal pitch
- **CAV discs**: 1 track = 1 frame (constant angular velocity)
  - Lead-in: ≥ 1.5mm = ≥ 1500μm / 1.6μm/track ≈ **938 tracks = 938 frames**
  - Lead-out (PAL): ≥ 2mm = ≥ 2000μm / 1.6μm/track ≈ **1250 tracks = 1250 frames**
  - Lead-out (NTSC): ≥ **600 tracks = 600 frames** (specified directly)
- **CLV discs**: Track density varies, so duration must be validated in **tracks**, not frames
  - Validator must accept track-based duration for CLV lead-in/lead-out

**Validation:**
- Validation catches invalid configurations
- All integration tests pass
- All validator tests pass

---

## Phase 6A: System Testing

**Dependencies:** Phase 5B

**Purpose:** Comprehensive testing of the biphase injection system against IEC specifications.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 6.1 | System-level tests for biphase generation | test_biphase_system.cpp |
| 6.2 | Create reference test vectors | Test data files |
| 6.3 | Validate against IEC specifications | Test reports |
| 6.4 | Performance benchmarking | Performance report |
| 6.9 | Final integration test suite | test_biphase_final.cpp |
| 6.13 | Tests for 40-bit FM transition times (135ns) | test_fm_transitions.cpp |
| 6.14 | Tests for code value range constraints | test_value_constraints.cpp |
| 6.15 | Tests for chapter encoding/decoding | test_chapter_encoding.cpp |

**Test Strategy:**
- **Unit Tests:** Encoding accuracy, code type generation, line placement, auto-increment, timecode continuity, chapter stop-bit, value constraints, chapter encoding, FM transitions
- **Integration Tests:** Pipeline integration, conflict resolution, multi-section projects, section transitions
- **System Tests:** End-to-end generation, specification compliance, performance
- **Compliance Tests:** IEC 60856/60857 rule validation (section types, disc types, continuity, frozen values, value ranges, bit layouts)
- **Reference Vectors:** Known-good waveforms, IEC-compliant patterns, edge cases

**Validation:**
- All tests pass (unit, integration, system, compliance)
- Performance meets requirements (< 10% overhead)
- 100% code coverage for new components

---

## Phase 6B: Documentation and Examples

**Dependencies:** Phase 6A

**Purpose:** Create comprehensive user documentation and example projects for biphase injection.

**Tasks:**
| ID | Description | Output |
|----|-------------|--------|
| 6.5 | Create Biphase User Design Document | New docs/user/biphase-design.md |
| 6.6 | Update user documentation | Updated docs/user/*.md |
| 6.7 | Create example YAML projects | Example files |
| 6.8 | Update HLD with implementation details | Updated high-level-design.md |
| 6.10 | Tests for timecode continuity across chapters | test_timecode_continuity.cpp |
| 6.11 | Tests for chapter stop-bit behavior | test_chapter_stopbit.cpp |
| 6.12 | Tests for NTSC frozen values | test_ntsc_frozen.cpp |

**Task 6.5: Biphase User Design Document**

**Purpose:** Create a comprehensive, user-facing design document that explains biphase line injection concepts, rules, and usage patterns in an accessible manner.

**Deliverable:** `docs/user/biphase-design.md`

**Required Content:**

1. **Introduction and Overview**
   - Purpose of biphase codes on LaserDisc
   - CAV vs CLV disc types explained
   - Section types (lead-in, programme area, lead-out) and their purposes
   - Overview of 24-bit biphase and 40-bit FM systems

2. **Disc Type Fundamentals**
   - CAV (Constant Angular Velocity) and CLV (Constant Linear Velocity) explained
   - PAL vs NTSC Differences
   - Code types available for each disc type

3. **Section Type Rules (The Matrix)**
   - Clear visual matrix showing which code types are allowed in which section types
   - Separate matrices for CAV and CLV discs
   - Examples of valid and invalid configurations

4. **Code Type Reference**
   - For each code type: Purpose, Hex Format, Value Constraints, Auto-increment, Lines, Section Restrictions, Disc Type, Special Behavior

5. **Timecode Continuity Explained**
   - Concept of continuous counting from programme start to end
   - Chapters do NOT reset timecodes
   - NTSC: Frozen values during lead-in/lead-out

6. **Chapter System Deep Dive**
   - Chapter numbering (0-79)
   - Stop-bit explained (controls player search behavior)
   - Minimum chapter length: 30 tracks

7. **Line Placement Logic**
   - Field-aware line numbering
   - Priority rules explained with examples

8. **NTSC 40-bit FM System**
   - Overview and differences from 24-bit biphase
   - White flag purpose and automatic placement

9. **Configuration Examples**
   - Complete CAV disc example
   - Complete CLV disc example
   - NTSC disc example
   - Multi-chapter example

10. **YAML Configuration Guide**
    - Step-by-step guide to configuring biphase injection
    - Required and optional fields
    - Validation rules

11. **Quick Reference Tables**
    - Code types at a glance
    - Section type compatibility matrix
    - Line allocation by standard and disc type

12. **Troubleshooting Guide**
    - Common issues and solutions
    - Understanding error messages

**Design Document Structure:**
```
docs/user/biphase-design.md
├── 1. Introduction
│   ├── Overview of LaserDisc Biphase Codes
│   ├── CAV vs CLV: Key Differences
│   └── When to Use Biphase Injection
├── 2. Core Concepts
│   ├── Section Types Explained
│   ├── Disc Types (CAV/CLV)
│   ├── Standards (PAL/NTSC)
│   └── Code Type Categories
├── 3. Rule Matrices
│   ├── Section Type Validation Matrix
│   ├── Code Type Compatibility Matrix
│   └── Quick Reference Card
├── 4. Code Type Reference
│   ├── CAV Code Types
│   ├── CLV Code Types
│   └── NTSC 40-bit FM Codes
├── 5. Advanced Topics
│   ├── Timecode Continuity
│   ├── Chapter Stop-Bit System
│   ├── Line Placement Rules
│   └── NTSC-Specific Behaviors
├── 6. Configuration Examples
│   ├── Complete Disc Examples
│   ├── Multi-Chapter Example
│   └── Edge Cases
├── 7. YAML Configuration Guide
│   ├── Required Fields
│   ├── Optional Fields
│   └── Validation Rules
├── 8. Troubleshooting
│   ├── Common Errors
│   └── Debugging Tips
└── Appendices
    ├── Quick Reference Tables
    └── Glossary of Terms
```

**Format Requirements:**
- User-friendly language, avoiding implementation details
- Copious examples and diagrams
- Clear visual matrices and tables
- Cross-references to implementation plan for technical details

**Validation:**
- Documentation is complete and accurate
- Examples work correctly

---

## Constraints

1. **Sample Rate:** Only 4fsc supported. Timing calculations must account for 4fsc lattice.

2. **Signal Level:** Fixed-point mV representation. Must fit within existing headroom.
   - PAL: -600mV to 0mV (pilot burst) already supported
   - NTSC: 0-100 IRE mapping required

3. **Signal Shaping:** Must use existing shaping infrastructure.
   - Must use `signal_shaping.h` helper functions for all transitions
   - **24-bit biphase**: Transition times (225ns ± 25ns) must use `TransitionTimeToRampSamples(225e-9, sample_rate, 0.1, 0.9)`
   - **40-bit FM**: Transition times (135ns ± 15ns) must use `TransitionTimeToRampSamples(135e-9, sample_rate, 0.1, 0.9)`
   - Pulse shaping must use `ShapedPulseLevel()` and `ShapedGateEnvelope()`
   - Quintic smootherstep S-curve provides zero slope at endpoints

4. **Line Timing:** Must integrate with current line generation flow. Must respect sync/burst placement.

5. **Field Handling:** Must handle field-aware line numbering. Must track first/second field state.

---

## Success Criteria

**Functional:**
- Both CAV and CLV modes work correctly
- All code types are supported with correct section type restrictions
- Section types are properly handled (lead-in, programme_area, lead-out)
- Auto-increment works for picture numbers and time codes
- Field-aware placement is correct
- Timecodes run continuously without resetting
- Chapters do not reset timecodes
- Chapter stop-bit behavior complies with IEC
- NTSC frozen values implemented correctly
- 40-bit FM generated with correct 135ns transitions
- All value range constraints enforced
- Chapter encoding/decoding per IEC formula
- White flag auto-placement for duplicate fields

**Quality:**
- 100% code coverage for new components
- No critical defects in production code
- All tests pass (unit, integration, system, compliance)
- Performance meets requirements (< 10% overhead)

**Compliance:**
- Output matches IEC 60856/60857 specifications
- All section type restrictions enforced
- All disc type restrictions enforced
- All value range constraints enforced
- Timing accurate to within specification tolerances
- Level mapping is correct for both standards
- Transitions use signal_shaping.h for PAL/NTSC compliance
- 24-bit biphase: 225ns ± 25ns transitions
- 40-bit FM: 135ns ± 15ns transitions
- Rise/fall times match specification for both systems

**Documentation:**
- User documentation updated
- Example projects provided
- HLD updated with implementation details
- All code is documented

**Maintainability:**
- Clean, modular code
- Follows existing code style
- Proper error handling
- Good separation of concerns

---

## Appendices

### Appendix A: Biphase Signal Specification

**24-bit Biphase System (PAL and NTSC):**
- Bit cell: 2.0 μs ± 0.01 μs
- **PAL Digital level**: 30% to 100% of white level (700mV)
- **NTSC Digital level**: 0 to 100 IRE (0 to 714.3mV)
- **24-bit Transition time**: 225 ns ± 25 ns (10%-90%)
- First nibble: Key, starts with logic-1

**40-bit FM System (NTSC Only):**
- Bit cell: 2.0 μs ± 0.01 μs (same as 24-bit biphase)
- Digital level: 0 to 100 IRE
- **Transition time: 135 ns ± 15 ns** (10%-90%) ← **DIFFERENT from 24-bit biphase**
- Bit transitions in center of bit cell represent logical '1's
- White flag: 100 IRE level (constant, not modulated)

**Encoding Rule (24-bit biphase):**
- Positive transition at center = '1'
- Negative transition at center = '0'

**Signal Shaping Requirements:**
- Must use `signal_shaping.h` functions for all transition generation
- **24-bit biphase**: Use `TransitionTimeToRampSamples(225e-9, sample_rate, 0.1, 0.9)` for 10%-90% transitions
- **40-bit FM**: Use `TransitionTimeToRampSamples(135e-9, sample_rate, 0.1, 0.9)` for 10%-90% transitions
- Use `ShapedPulseLevel()` for bit transitions
- Use `ShapedGateEnvelope()` for bit cell envelopes

---

### Appendix B: Code Type Matrix

| Disc Type | Code Type | Hex Format | Lines (PAL) | Lines (NTSC) | Auto-Increment | **Allowed Section Types** | **Value Constraints** | **Notes** |
|-----------|-----------|-------------|--------------|---------------|----------------|----------------------------|------------------------|-----------|
| CAV | lead_in | 88FFFF | 17,18,330,331 | 17,18,280,281 | ❌ | lead_in | Fixed | ≥ 1.5mm before programme |
| CAV | lead_out | 80EEEE | 17,18,330,331 | 17,18,280,281 | ❌ | lead_out | Fixed | ≥ 2mm after programme |
| CAV | picture_number | FX₁X₂X₃X₄X₅ | 17,18 or 330,331 | 17,18 or 280,281 | ✅ | programme_area | **X₁-X₅=0-9, Max: 99,999** (PAL) / **79,999** (NTSC) | Starts at 1, continuous, decimal digits |
| CAV | picture_stop | 82CFFF | 16,17 or 329,330 | 16,17 or 279,280 | ❌ | programme_area | Fixed | CAV only, following field |
| CAV | chapter_number | 8X₁X₂DDD | 17,18,330,331 | 17,18,280,281 | ❌ | programme_area | **Chapter=(X₁&7)×16+X₂, Stop-bit=(X₁&8)>>3, Max: 79** | Where no picture_number |
| CAV | programme_status | 8DC/BA X₃X₄X₅ | 16,329 | 16,279 | ❌ | programme_area | DC=CX on, BA=CX off | Picture stop has priority |
| CAV | users_code | 8X₁DX₃X₄X₅ | 16,329 | 16,279 | ❌ | lead_in, lead_out | **X₁=0-7 only** | Only in lead-in/lead-out |
| CLV | lead_in | 88FFFF | 17,18,330,331 | 17,18,280,281 | ❌ | lead_in | Fixed | ≥ 1.5mm before programme |
| CLV | lead_out | 80EEEE | 17,18,330,331 | 17,18,280,281 | ❌ | lead_out | Fixed | ≥ 2mm after programme |
| CLV | programme_time_code | FX₁DDX₂X₃ | 17,18 or 330,331 | 17,18 or 280,281 | ✅ (minutes) | programme_area | **X₁=hours(0-F), X₂=min tens(0-5), X₃=min units(0-9)** | Hours and minutes, continuous, BCD minutes |
| CLV | clv_code | 87FFFF | 17 or 330 | 17 or 280 | ❌ | programme_area | Fixed | Where no programme_time_code or clv_picture_number |
| CLV | clv_picture_number | 8X₁EX₃X₄X₅ | 16 or 329 | 16 or 279 | ✅ | programme_area | **X₁=A-F, X₃=0-9, X₄=0-2, X₅=0-9, Sec=(X₁-'A')×10+X₃, Frame=X₄×10+X₅** | Identifies frames, continuous |
| CLV | chapter_number | 8X₁X₂DDD | 18 or 331 | 18 or 281 | ❌ | programme_area | **Chapter=(X₁&7)×16+X₂, Stop-bit=(X₁&8)>>3, Max: 79** | Where no programme_time_code or clv_picture_number |
| CLV | programme_status | 8DC/BA X₃X₄X₅ | 16 or 329 | 16 or 279 | ❌ | programme_area | DC=CX on, BA=CX off | Same fields as CLV code |
| CLV | users_code | 8X₁DX₃X₄X₅ | 16,329 | 16,279 | ❌ | lead_in, lead_out | **X₁=0-7 only** | Only in lead-in/lead-out |
| NTSC | fm_picture_number | (40-bit FM) | N/A | 10,273 | ✅ | programme_area | **Bit layout per Appendix G, Max: 99,999** | CAV only; frozen in lead-in/out |
| NTSC | fm_programme_time | (40-bit FM) | N/A | 10,273 | ✅ | programme_area | **Bit layout per Appendix G, X₅=A/B/C/D** | CLV only; frozen in lead-in/out |
| NTSC | fm_white_flag | (100 IRE) | N/A | 11,274 | ❌ | programme_area | **Auto-placed for duplicate fields** | First field marker |

---

### Appendix C: YAML Schema Extensions

```yaml
sections:
  - name: "CAV Lead-in"
    type: progressive
    source: "assets/lead-in.mkv"
    duration_frames: 938  # ≥ 1.5mm / 1.6μm = ~938 tracks/frames (CAV)
    section_type: lead_in  # NEW: lead_in, programme_area, lead_out
    line_injections:
      - type: laserdisc
        disc_type: CAV  # NEW: CAV or CLV
        codes:
          - code_type: lead_in
          - code_type: users_code
            users_code: "0x801234"  # X₁=0 (valid: 0-7)

  - name: "CAV Programme"
    type: progressive
    source: "assets/video.mkv"
    duration_frames: 10000
    section_type: programme_area
    line_injections:
      - type: laserdisc
        disc_type: CAV
        codes:
          - code_type: picture_number
            start_value: 1  # Auto-increments from 1, max 99999 (PAL)
          - code_type: chapter_number
            chapter: 1  # Chapter number (0-79)
            # stop_bit_zero_tracks: 400  # OPTIONAL: default 400, except first chapter
          - code_type: picture_stop
            frame: 500  # OPTIONAL: specific frame to insert picture stop
          - code_type: programme_status
            programme_status: "0x8F0000"  # 24-bit hex with Hamming code

  - name: "CLV Programme"
    type: progressive
    source: "assets/video.mkv"
    duration_frames: 30000
    section_type: programme_area
    line_injections:
      - type: laserdisc
        disc_type: CLV
        codes:
          - code_type: programme_time_code
            start_hours: 0
            start_minutes: 0  # Auto-increments minutes, BCD-encoded
          - code_type: clv_picture_number
            start_value: 1  # Auto-increments, X₁=A-F, X₃=0-9, X₄=0-2, X₅=0-9
          - code_type: clv_code
          - code_type: chapter_number
            chapter: 1

  - name: "NTSC Lead-out"
    type: progressive
    source: "assets/lead-out.mkv"
    duration_frames: 600  # NTSC: ≥ 600 tracks = 600 frames (CAV/CLV)
    section_type: lead_out
    line_injections:
      - type: laserdisc
        disc_type: CAV  # or CLV
        codes:
          - code_type: lead_out
          - code_type: users_code
            users_code: "0x81ABCD"  # X₁=1 (valid: 0-7)
```

**Validation Rules:**
- `section_type` must be one of: `lead_in`, `programme_area`, `lead_out`
- `disc_type` must be one of: `CAV`, `CLV`
- `code_type` must be valid for the `disc_type`
- `code_type` must be valid for the `section_type` (see Appendix D)
- Auto-incrementing codes require `start_value`
- Chapter codes require `chapter` (0-79)
- Programme status codes require `programme_status` (24-bit hex with valid Hamming code, see Appendix C)
- NTSC-only codes (`fm_*`) must not be used with PAL
- CAV-only codes (`picture_number`, `picture_stop`) must not be used with CLV
- CLV-only codes (`programme_time_code`, `clv_code`, `clv_picture_number`) must not be used with CAV
- `users_code` must NOT be used in `programme_area` sections
- `lead_in` code must ONLY be used in `lead_in` sections
- `lead_out` code must ONLY be used in `lead_out` sections
- All programme area codes must NOT be used in lead_in or lead_out sections
- **Picture number value must be ≤ 99999 (PAL) or ≤ 79999 (NTSC)**
- **Picture number X₁-X₅ must be decimal digits (0-9 hex values)**
- **Chapter number must be ≤ 79**
- **Chapter X₁ stop-bit must be correctly encoded per IEC formula**
- **CLV picture number X₁ must be A-F, X₃ must be 0-9, X₄ must be 0-2, X₅ must be 0-9**
- **Programme time code X₂ must be 0-5, X₃ must be 0-9 (BCD minutes)**
- **Users code X₁ must be 0-7**
- Minimum chapter length: 30 tracks
- Lead-in section duration (CAV): ≥ 938 frames (at 1.6μm track pitch)
- Lead-out section duration (PAL CAV): ≥ 1250 frames (at 1.6μm track pitch)
- Lead-out section duration (NTSC): ≥ 600 frames
- First chapter after lead-in must have stop-bit = 1 (enforced automatically if not specified)

---

### Appendix D: Section Type Validation Matrix

**This matrix defines the ONLY valid combinations of disc_type, code_type, and section_type.**

#### CAV Discs

| Code Type | lead_in | programme_area | lead_out |
|-----------|---------|----------------|----------|
| lead_in | ✅ | ❌ | ❌ |
| lead_out | ❌ | ❌ | ✅ |
| picture_number | ❌ | ✅ | ❌ |
| picture_stop | ❌ | ✅ | ❌ |
| chapter_number | ❌ | ✅ | ❌ |
| programme_status | ❌ | ✅ | ❌ |
| users_code | ✅ | ❌ | ✅ |
| fm_picture_number (NTSC) | ❌ | ✅ | ❌ |
| fm_white_flag (NTSC) | ❌ | ✅ | ❌ |

#### CLV Discs

| Code Type | lead_in | programme_area | lead_out |
|-----------|---------|----------------|----------|
| lead_in | ✅ | ❌ | ❌ |
| lead_out | ❌ | ❌ | ✅ |
| programme_time_code | ❌ | ✅ | ❌ |
| clv_code | ❌ | ✅ | ❌ |
| clv_picture_number | ❌ | ✅ | ❌ |
| chapter_number | ❌ | ✅ | ❌ |
| programme_status | ❌ | ✅ | ❌ |
| users_code | ✅ | ❌ | ✅ |
| fm_programme_time (NTSC) | ❌ | ✅ | ❌ |
| fm_white_flag (NTSC) | ❌ | ✅ | ❌ |

**Validation Implementation:**
The validator MUST check that every `code_type` in a section's `codes` list is marked ✅ for that section's `section_type` and the section's `disc_type`. Any ❌ combination is an error.

**Example Valid Configurations:**
```yaml
# VALID: Users code in lead_in
- section_type: lead_in
  disc_type: CAV
  codes:
    - code_type: users_code

# VALID: Picture number in programme_area
- section_type: programme_area
  disc_type: CAV
  codes:
    - code_type: picture_number

# INVALID: Users code in programme_area (will be rejected)
- section_type: programme_area
  disc_type: CAV
  codes:
    - code_type: users_code  # ERROR: users_code not allowed in programme_area

# INVALID: Picture number in lead_in (will be rejected)
- section_type: lead_in
  disc_type: CAV
  codes:
    - code_type: picture_number  # ERROR: picture_number not allowed in lead_in

# INVALID: Picture number > 99999 (PAL) (will be rejected)
- section_type: programme_area
  disc_type: CAV
  codes:
    - code_type: picture_number
      start_value: 100000  # ERROR: exceeds PAL max of 99999

# INVALID: Users code X₁=8 (will be rejected)
- section_type: lead_in
  disc_type: CAV
  codes:
    - code_type: users_code
      users_code: "0x881234"  # ERROR: X₁=8, must be 0-7
```

---

### Appendix E: Timecode Continuity and Chapter Rules

#### Timecode Continuity Rules (IEC 60856/60857 Compliant)

1. **Continuous Counting:** picture_number (CAV), programme_time_code (CLV), and clv_picture_number (CLV) counters MUST start at the first frame of the programme area and increment continuously through to the last frame **without resetting** for any reason.

2. **Chapter Independence:** Chapter codes do **NOT** reset or affect any timecode counters. All timecodes continue incrementing across chapter boundaries. The chapter number itself increments, but timecodes are unaffected.

3. **First Picture Number:** CAV picture_number starts at **1** at the first frame of the programme area (frame 0 of programme_area section).

4. **Programme Time Code Start:** CLV programme_time_code starts at **0:00** (hours: 0, minutes: 0) at the first frame of the programme area.

5. **CLV Picture Number:** Starts at a value derived from the programme_time_code and increments for each frame. The X₁ and X₃ fields indicate seconds together with the programme time code.

6. **NTSC Frozen Values:**
   - **During lead-in**: picture_number = 0, programme_time = 0:00, fm_picture_number = 0, fm_programme_time = 0:00
   - **During lead-out**: picture_number and programme_time are **frozen** at their last programme area values
   - The frozen values are maintained for the entire lead-out section

#### Chapter Stop-Bit Rules (IEC 60856 §10.1.5, IEC 60857 §10.1.5)

1. Each chapter number has a **stop-bit** which is the first bit after the key nibble (bit 4 of the 24-bit code = **MSB of X₁**).

2. **Stop-bit = 0** for the first **400 tracks** of the chapter
3. **Stop-bit = 1** for all tracks after the first 400 tracks until the next chapter
4. **Exception:** The first chapter directly after the lead-in area **MUST** have stop-bit = **1**. It does NOT have a 400-track period with stop-bit = 0.
5. On disks with chapters shorter than 800 tracks, the stop-bit of each chapter number **shall have the logic value "one"** (i.e., no stop-bit = 0 period).
6. **Minimum Chapter Length:** Each chapter must be at least **30 tracks** long.
7. **Stop-Bit Purpose:** The zero value stop-bit is intended to disable the search action of the player. The one value enables search.

**Implementation Formula (Critical):**
```cpp
// For chapter code hex value 8X₁X₂DDD:
// X₁ is a hex nibble (4 bits): bits [7:4] of byte 1
uint8_t x1_nibble = (code_value >> 16) & 0x0F;  // Extract X₁ (nibble 2 of 6)

// Stop-bit is MSB of X₁ (bit 4 of the full 24-bit code):
uint8_t stop_bit = (x1_nibble & 0x08) >> 3;  // 1 if x1_nibble >= 8, else 0

// Chapter number is lower 3 bits of X₁ + full X₂:
uint8_t chapter_number = (x1_nibble & 0x07) * 16 + ((code_value >> 12) & 0x0F);
```

**Encoding Formula:**
```cpp
// To encode chapter number with stop-bit:
uint8_t x1_nibble = (stop_bit << 3) | ((chapter_number / 16) & 0x07);
uint32_t code_value = 0x800000 | (x1_nibble << 16) | ((chapter_number % 16) << 12) | 0xDDD;
```

---

### Appendix F: NTSC-Specific Behaviors

#### Lead-In and Lead-Out Behavior

1. **Lead-In Duration:** At least a number of tracks corresponding to **1.5mm** prior to the active programme start.

2. **Lead-Out Duration:** At least **600 tracks** after the active programme stops (IEC 60857 §10.1.2).

3. **Frozen Picture Numbers (CAV):**
   - During lead-in: picture_number is **always zero**
   - During lead-out: picture_number is **frozen** on the last picture number of the active programme
   - This applies to both 24-bit biphase and 40-bit FM picture numbers

4. **Frozen Programme Time (CLV):**
   - During lead-in: programme_time is **preset to 0 min and 0 s**
   - During lead-out: programme_time is **frozen** to that of the end of the active programme
   - This applies to both 24-bit biphase and 40-bit FM programme time codes

#### 40-bit FM System (NTSC Only)

1. **White Flag:**
   - Inserted on either line 11 (field 1) or 274 (field 2)
   - 100 IRE level (maximum white)
   - Indicates the position of a complete picture during the active programme
   - **Automatic Placement**: If there are two or more fields scanned from the same photographic picture, or if there are two fields which are made equal by electronic processing, the white flag **MUST** be automatically placed on the first field of the next picture. The transfer equipment controls this automatically.

2. **fm_picture_number:**
   - Always present on CAV disks during active programme
   - Inserted in lines 10 and 273
   - Maximum value: **99,999**
   - Data format: X₁X₂X₃X₄X₅ (X₅ is least significant digit)
   - **Value Constraints**: X₁-X₅ are decimal digits (0-9 as hex values)
   - Frozen at 0 during lead-in, frozen at last value during lead-out

3. **fm_programme_time:**
   - Always present on CLV disks during active programme
   - Inserted in lines 10 and 273
   - Format: X₁X₂ (minutes), X₃X₄ (seconds), X₅ (mode indicator)
   - **Value Constraints**: X₁X₂ and X₃X₄ are BCD-encoded
   - X₅ mode indicator values:
     - Lead-in: **A**
     - End of lead-in to lead-in + 100 frames: **B**
     - Picture (active programme): **D**
     - Lead-out: **C**
   - Frozen at 0:00 during lead-in, frozen at last value during lead-out

#### 40-bit FM Bit Layout (IEC 60857 §10.2, Figure 13)

**See Appendix G for complete bit-level specification.**

#### Coexistence with 24-bit Biphase

1. NTSC discs have **both** 24-bit biphase codes (lines 16-18, 279-281) **and** 40-bit FM codes (lines 10-11, 273-274)
2. The 40-bit FM system provides redundant/alternative encoding of picture and time information
3. Both systems must be generated **simultaneously** for NTSC discs
4. The white flag (100 IRE) is unique to the 40-bit FM system

#### Historical Note (Legacy Compatibility)

During the first years after introduction, a picture stop was indicated twice on CAV discs:
1. By the dedicated picture_stop code (82CFFF) as specified in §10.1.4
2. **Also** by the value of the first bit of X₁ in the picture number code FX₁X₂X₃X₄X₅:
   - First bit of X₁ = **0** → picture stop
   - First bit of X₁ = **1** → picture number without stop

**Implementation Note**: This legacy encoding is **obsolete** but may be required for compatibility with early LaserDisc players. The primary picture_stop code (82CFFF) should always be generated; the X₁ bit encoding is optional for legacy support.

---

### Appendix G: 40-bit FM Code Bit-Level Specification (NTSC)

**Reference**: IEC 60857 §10.2, Figure 13

The 40-bit FM coded signal provides television field information, 20 data bits, and a parity bit. The remaining bits are used for clock synchronization and data recognition.

#### Bit Assignment (MSB = bit 1, LSB = bit 40):

| Bit# | Range | Purpose | Fixed Value | Data Source | Notes |
|------|-------|---------|--------------|-------------|-------|
| 1-4 | | **Receiver clock synchronizing bits** | **0011** | Fixed | Must match exactly for synchronization |
| 5 | | **Video field indicator bit** | 1=field 1, 0=field 2 | Auto-detected | Logic '1' = first field |
| 6-12 | | **Leading data recognition bits** | **1110010** | Fixed | Must match exactly |
| 13-16 | | **Data bits - X₅** | Variable | LSB = bit 13 | Picture/time data bit 0 |
| 17-20 | | **Data bits - X₄** | Variable | LSB = bit 17 | Picture/time data bit 1 |
| 21-24 | | **Data bits - X₃** | Variable | LSB = bit 21 | Picture/time data bit 2 |
| 25-28 | | **Data bits - X₂** | Variable | LSB = bit 25 | Picture/time data bit 3 |
| 29-32 | | **Data bits - X₁** | Variable | LSB = bit 29 | Picture/time data bit 4 |
| 33 | | **Data parity bit** | Variable | **Odd parity** | Parity over bits 1-32 |
| 34-40 | | **Trailing data recognition bits** | **0001101** | Fixed | Must match exactly |

#### Bit Cell Specification:
- **Bit cell length**: 2.0 μs ± 0.01 μs (same as 24-bit biphase)
- **Digital level**: 0 to 100 IRE
- **Transition time**: **135 ns ± 15 ns** (10%-90%) ← **DIFFERENT from 24-bit biphase (225ns)**
- **Transition rule**: Transitions in center of bit cell represent logical '1's (same as biphase rule)

#### Data Encoding:

**For fm_picture_number (CAV):**
- X₁-X₅ encode the picture number as decimal digits (0-9 each)
- Maximum: 99,999
- Data bits represent: X₁ (MSB) ... X₅ (LSB)

**For fm_programme_time (CLV):**
- X₁-X₂ encode minutes (BCD: tens and units)
- X₃-X₄ encode seconds (BCD: tens and units)
- X₅ encodes mode indicator:
  - **A** (0xA) = Lead-in
  - **B** (0xB) = End of lead-in to lead-in + 100 frames
  - **D** (0xD) = Picture (active programme)
  - **C** (0xC) = Lead-out

#### White Flag Specification:
- **Level**: 100 IRE (constant, not modulated)
- **Lines**: 11 (field 1) or 274 (field 2)
- **Purpose**: Indicates first field of a complete picture
- **Automatic control**: When consecutive fields are identical (from same photographic source or electronic processing), white flag is automatically placed on first field of next picture

#### Implementation Requirements:
1. **Transition calculation**: Must use `TransitionTimeToRampSamples(135e-9, sample_rate, 0.1, 0.9)` for 10%-90% transitions
2. **Bit timing**: Each bit cell is exactly 2.0 μs ± 0.01 μs
3. **Fixed patterns**: Clock sync and data recognition bits must match exactly
4. **Parity**: Bit 33 must be odd parity over bits 1-32
5. **Field detection**: Bit 5 must reflect the current video field

#### Example: fm_picture_number = 12345 (CAV)
```
Picture number: 12345 = 0x003039 (BCD)
X₁ = 0x1, X₂ = 0x2, X₃ = 0x3, X₄ = 0x4, X₅ = 0x5

Bits 1-4:   0011 (clock sync)
Bit 5:      1 or 0 (field indicator)
Bits 6-12:  1110010 (leading recognition)
Bits 13-16: X₅ = 0x5 → 0101 (LSB first)
Bits 17-20: X₄ = 0x4 → 0100
Bits 21-24: X₃ = 0x3 → 0011
Bits 25-28: X₂ = 0x2 → 0010
Bits 29-32: X₁ = 0x1 → 0001
Bit 33:     Odd parity over bits 1-32
Bits 34-40: 0001101 (trailing recognition)
```

---
