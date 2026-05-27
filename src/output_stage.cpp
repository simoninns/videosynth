#include "videosynth/output_stage.h"

#include <cstdint>
#include <fstream>

#include "videosynth/model.h"

namespace videosynth {

bool OutputStage::Write(const Project& project,
                        const std::vector<double>& y_mv,
                        const std::vector<double>& c_mv,
                        const std::string& output_path,
                        const std::string& metadata_path,
                        std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }

  if (y_mv.size() != c_mv.size()) {
    errors->push_back("Internal error: Y and C sample vectors must be same size.");
    return false;
  }

  std::ofstream video_stream(output_path, std::ios::binary);
  if (!video_stream) {
    errors->push_back("Failed to open output video file: " + output_path);
    return false;
  }

  // Phase 0 scaffold: emit composite-only placeholder samples.
  for (std::size_t i = 0; i < y_mv.size(); ++i) {
    (void)y_mv[i];
    (void)c_mv[i];
    const std::uint16_t composite_code = 512;
    video_stream.write(reinterpret_cast<const char*>(&composite_code),
                       sizeof(composite_code));
  }

  std::ofstream metadata_stream(metadata_path);
  if (!metadata_stream) {
    errors->push_back("Failed to open metadata file: " + metadata_path);
    return false;
  }

  metadata_stream << "format=videosynth_phase0\n";
  metadata_stream << "standard=" << StandardToString(project.cvbs_presets.standard) << "\n";
  metadata_stream << "sample_rate=" << project.cvbs_presets.sample_rate << "\n";
  metadata_stream << "subcarrier_lock="
                  << (project.cvbs_presets.subcarrier_lock ? "true" : "false") << "\n";
  metadata_stream << "sample_count=" << y_mv.size() << "\n";

  return true;
}

}  // namespace videosynth
