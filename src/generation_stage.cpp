#include "videosynth/generation_stage.h"

#include "videosynth/timing_constants.h"

namespace videosynth {

bool GenerationStage::Generate(const Project& project,
                               std::vector<double>* out_y_mv,
                               std::vector<double>* out_c_mv,
                               std::vector<std::string>* errors) {
  if (out_y_mv == nullptr || out_c_mv == nullptr || errors == nullptr) {
    return false;
  }

  const TimingConstants timing = GetTimingConstants(project.cvbs_presets.standard);
  const std::size_t sample_count =
      static_cast<std::size_t>(timing.lines_per_frame * timing.samples_per_line_4fsc);

  // Phase 0 scaffold: deterministic blanking-level frame skeleton in separate Y/C paths.
  out_y_mv->assign(sample_count, 0.0);
  out_c_mv->assign(sample_count, 0.0);
  return true;
}

}  // namespace videosynth
