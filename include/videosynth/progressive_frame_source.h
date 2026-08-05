/*
 * File:        progressive_frame_source.h
 * Module:      progressive_frame_source
 * Purpose:     Defines fixed-format frame-source buffers for frame-based
 * content.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "videosynth/interfaces.h"
#include "videosynth/model.h"

namespace videosynth {

struct YCbCr444Pixel {
  std::int16_t y = 64;
  std::int16_t cb = 512;
  std::int16_t cr = 512;
};

struct FrameSourceImage {
  int width = 0;
  int height = 0;
  int active_x = 0;
  int active_y = 0;
  int active_width = 0;
  int active_height = 0;
  std::vector<YCbCr444Pixel> pixels;

  const YCbCr444Pixel& PixelAt(int x, int y) const;
};

// Thread-safety: ProgressiveFrameSource IS thread-safe, satisfying the
// IProgressiveFrameProvider contract. The mutable decode cache
// (cached_sources_) is guarded by an internal mutex; GenerateFrame,
// ResolveFrameCount, and ClearCache may be called concurrently from multiple
// threads. Cache misses decode while holding the mutex, so concurrent readers
// of an uncached source serialise on the decode rather than duplicating it.
//
// Decoded frames are immutable and handed out as shared_ptr<const ...>, so a
// cache hit costs a reference count instead of an image copy (~2.4 MiB for a
// PAL raster) and workers may keep using a frame after the cache evicts it.
class ProgressiveFrameSource final : public IProgressiveFrameProvider {
 public:
  // Checks if this source supports the given section.
  //
  // Args:
  //   section: The project section to check.
  //
  // Returns:
  //   true if the section is supported, false otherwise.
  bool SupportsSection(const Section& section) const;

  // Clears any cached frame data.
  void ClearCache() const;

  // Determines the frame count for a section.
  //
  // Ownership: out_frame_count and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  //
  // Args:
  //   section: The project section.
  //   standard: The video standard.
  //   out_frame_count: Output pointer for the resolved frame count.
  //   error: Output pointer for any error message.
  //
  // Returns:
  //   true on success, false on any error.
  bool ResolveFrameCount(const Section& section, Standard standard,
                         int* out_frame_count, std::string* error) const;

  // Provides a frame of image data for the given section and frame index.
  //
  // Ownership: out_image and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The decoded image is shared with the internal cache and must not be
  // mutated; it stays alive for as long as the caller holds its reference.
  //
  // Args:
  //   section: The project section.
  //   frame_index: Index of the frame to generate.
  //   standard: The video standard.
  //   out_image: Output pointer receiving the shared decoded frame.
  //   error: Output pointer for any error message.
  //
  // Returns:
  //   true on success, false on any error.
  bool GenerateFrame(const Section& section, int frame_index, Standard standard,
                     std::shared_ptr<const FrameSourceImage>* out_image,
                     std::string* error) const override;

 private:
  // One decoded source held by the cache. EXR sources are a single complete
  // frame; MKV sources hold the decoded prefix of the clip, and is_complete
  // records whether that prefix is the whole file.
  struct DecodedSource {
    std::string source;
    Standard standard = Standard::kUnknown;
    bool is_complete = false;
    std::vector<std::shared_ptr<const FrameSourceImage>> frames;
  };

  // Cache depth in distinct sources. Two entries keep a worker that straddles
  // a section boundary from evicting - and so re-decoding - the section it is
  // still finishing.
  static constexpr std::size_t kMaxCachedSources = 2;

  // Returns the cached entry for (source, standard), promoting it to the front
  // of the most-recently-used order, or nullptr when absent.
  // Complexity: O(kMaxCachedSources). Caller must hold cache_mutex_.
  DecodedSource* FindCachedSource(const std::string& source,
                                  Standard standard) const;

  // Inserts an entry at the front of the most-recently-used order, evicting the
  // least-recently-used entry when the cache is full, and returns it.
  // Caller must hold cache_mutex_.
  DecodedSource* InsertCachedSource(DecodedSource entry) const;

  // Returns the cached decode of an MKV source, decoding it when the cache
  // holds neither the whole clip nor at least min_required_frames of it.
  // Returns nullptr and sets error on decode failure.
  // Caller must hold cache_mutex_.
  const DecodedSource* EnsureMkvSource(const Section& section,
                                       Standard standard,
                                       int min_required_frames,
                                       bool require_complete,
                                       std::string* error) const;

  // Guards all mutable cache members below.
  mutable std::mutex cache_mutex_;

  // Most-recently-used first; at most kMaxCachedSources entries.
  mutable std::vector<DecodedSource> cached_sources_;
};

}  // namespace videosynth