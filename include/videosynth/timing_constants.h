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
        .lines_per_frame = 625,
        .samples_per_line_4fsc = 1135,
        .frame_rate_hz = 25.0,
        .sample_rate_4fsc_hz = 17734475.0,
    };
  }

  if (standard == Standard::kNtsc) {
    return TimingConstants{
        .lines_per_frame = 525,
        .samples_per_line_4fsc = 910,
        .frame_rate_hz = 30000.0 / 1001.0,
        .sample_rate_4fsc_hz = 14318180.0,
    };
  }

  throw std::invalid_argument("Timing constants requested for unknown standard");
}

inline SignalLevels GetSignalLevels(Standard standard) {
  if (standard == Standard::kPal) {
    return SignalLevels{
        .sync_tip_mv = -300.0,
        .blanking_mv = 0.0,
        .black_mv = 0.0,
        .white_mv = 700.0,
    };
  }

  if (standard == Standard::kNtsc) {
    return SignalLevels{
        .sync_tip_mv = -285.7,
        .blanking_mv = 0.0,
        .black_mv = 53.6,
        .white_mv = 714.3,
    };
  }

  throw std::invalid_argument("Signal levels requested for unknown standard");
}

}  // namespace videosynth
