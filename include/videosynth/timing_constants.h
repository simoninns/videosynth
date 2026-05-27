/*
 * File:        timing_constants.h
 * Module:      timing_constants
 * Purpose:     Provides PAL and NTSC timing and signal level constants.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <stdexcept>

#include "videosynth/model.h"

namespace videosynth {

struct TimingConstants {
  int lines_per_frame = 0;
  int samples_per_line_4fsc = 0;
  double frame_rate_hz = 0.0;
  double sample_rate_4fsc_hz = 0.0;
};

struct SignalLevels {
  double sync_tip_mv = 0.0;
  double blanking_mv = 0.0;
  double black_mv = 0.0;
  double white_mv = 0.0;
};

inline TimingConstants GetTimingConstants(Standard standard) {
  if (standard == Standard::kPal) {
    return TimingConstants{
        // ITU-R BT.1700 Annex 1 Part B Table 1 item 1 (625-line PAL).
        .lines_per_frame = 625,
        // EBU Tech. 3280-E Section 1.2: 1135.0064 samples/line nominal, modelled
        // here as 1135 integer samples plus phase-slip handling elsewhere.
        .samples_per_line_4fsc = 1135,
        // ITU-R BT.1700 Annex 1 Part B Table 1 item 3: 2fH/625 = 25 frames/s.
        .frame_rate_hz = 25.0,
        // EBU Tech. 3280-E Section 1.1.1 Table 1: 4fsc = 17.734475 MHz.
        .sample_rate_4fsc_hz = 17734475.0,
    };
  }

  if (standard == Standard::kNtsc) {
    return TimingConstants{
        // SMPTE 170M-2004 Section 11.3: 525 lines/frame, 2:1 interlace.
        .lines_per_frame = 525,
        // SMPTE 244M-2003 Section 4.1.1: 910 samples/line.
        .samples_per_line_4fsc = 910,
        // SMPTE 170M-2004 Section 11.3: 30000/1001 frames/s.
        .frame_rate_hz = 30000.0 / 1001.0,
        // SMPTE 244M-2003 Section 3.4: 4fsc clock of 14.31818 MHz.
        .sample_rate_4fsc_hz = 14318180.0,
    };
  }

  throw std::invalid_argument("Timing constants requested for unknown standard");
}

inline SignalLevels GetSignalLevels(Standard standard) {
  if (standard == Standard::kPal) {
    return SignalLevels{
        // ITU-R BT.1700 Annex 1 Part B Table 2 item 3 (625 PAL sync level).
        .sync_tip_mv = -300.0,
        // ITU-R BT.1700 Annex 1 Part B Table 2 item 1 (blanking reference).
        .blanking_mv = 0.0,
        // ITU-R BT.1700 Annex 1 Part B Table 2 item 4 (625 PAL setup = 0 mV).
        .black_mv = 0.0,
        // ITU-R BT.1700 Annex 1 Part B Table 2 item 2 (white level).
        .white_mv = 700.0,
    };
  }

  if (standard == Standard::kNtsc) {
    return SignalLevels{
        // SMPTE 170M-2004 Section 12.1/12.3 Table 1: sync is -40 IRE, and
        // 140 IRE = 1 V, so sync is -40/140 * 1000 = -285.7 mV.
        .sync_tip_mv = -285.7,
        // SMPTE 170M-2004 Section 12.2: blanking reference is 0 IRE.
        .blanking_mv = 0.0,
        // SMPTE 170M-2004 Section 12.3 Table 1: black is +7.5 IRE, and
        // 140 IRE = 1 V, so black is 7.5/140 * 1000 = 53.6 mV.
        .black_mv = 53.6,
        // SMPTE 170M-2004 Section 12.3 Table 1: white is +100 IRE, and
        // 140 IRE = 1 V, so white is 100/140 * 1000 = 714.3 mV.
        .white_mv = 714.3,
    };
  }

  throw std::invalid_argument("Signal levels requested for unknown standard");
}

}  // namespace videosynth
