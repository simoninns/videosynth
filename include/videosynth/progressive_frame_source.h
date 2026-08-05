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

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
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
// IProgressiveFrameProvider contract. GenerateFrame, ResolveFrameCount,
// PrefetchSection, and ClearCache may be called concurrently from multiple
// threads. The cache table is guarded by cache_mutex_, which is held only for
// the lookup; each entry carries its own lock that is held across its decode.
// Two requests for the same source therefore share one decode, while a decode
// of one source never blocks requests for another — which is what lets a
// prefetch of the next section run alongside the current section's synthesis.
//
// Decoded frames are immutable and handed out as shared_ptr<const ...>, so a
// cache hit costs a reference count instead of an image copy (~2.4 MiB for a
// PAL raster) and workers may keep using a frame after the cache evicts it.
class ProgressiveFrameSource final : public IProgressiveFrameProvider {
 public:
  ProgressiveFrameSource() = default;

  // Stops and joins the prefetch thread, if one was started.
  ~ProgressiveFrameSource() override;

  // Owns a worker thread and a decode cache; neither copying nor moving is
  // meaningful.
  ProgressiveFrameSource(const ProgressiveFrameSource&) = delete;
  ProgressiveFrameSource& operator=(const ProgressiveFrameSource&) = delete;
  ProgressiveFrameSource(ProgressiveFrameSource&&) = delete;
  ProgressiveFrameSource& operator=(ProgressiveFrameSource&&) = delete;

  // Checks if this source supports the given section.
  //
  // Args:
  //   section: The project section to check.
  //
  // Returns:
  //   true if the section is supported, false otherwise.
  bool SupportsSection(const Section& section) const;

  // Requests a background decode of the section's source, so that a later
  // GenerateFrame for that section finds the decode already done instead of
  // stalling every caller at a section boundary.
  //
  // Returns immediately. The request is dropped when the source is already
  // queued or unsupported, and decode failures are ignored here: the real
  // GenerateFrame call repeats the decode and reports the error.
  //
  // Args:
  //   section: The section whose source should be decoded ahead of time.
  //   frame_index: First source frame index that section will request; it
  //     determines how much of a clip source the decode must cover.
  //   standard: The video standard the decode targets.
  void PrefetchSection(const Section& section, int frame_index,
                       Standard standard) const;

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
  //
  // The entry is created empty under cache_mutex_ and filled under its own
  // decode_mutex, which every reader also holds while copying a frame
  // reference out. Entries are shared_ptr-owned, so an entry evicted from the
  // table stays valid for whoever is still decoding from or reading it.
  struct DecodedSource {
    std::string source;
    Standard standard = Standard::kUnknown;
    // Held across this entry's decode and across every read of the fields
    // below. Never taken while cache_mutex_ is held.
    std::mutex decode_mutex;
    bool is_complete = false;
    std::vector<std::shared_ptr<const FrameSourceImage>> frames;
  };

  using DecodedSourcePtr = std::shared_ptr<DecodedSource>;

  // Cache depth in distinct sources: the section being rendered, the previous
  // section a straddling worker may still be finishing, and the next section
  // a prefetch has decoded ahead of time.
  static constexpr std::size_t kMaxCachedSources = 3;

  // Returns the cache entry for (source, standard), creating an empty one when
  // absent, and promotes it to the front of the most-recently-used order.
  // Complexity: O(kMaxCachedSources). Takes cache_mutex_ internally.
  DecodedSourcePtr AcquireCacheEntry(const std::string& source,
                                     Standard standard) const;

  // Decodes the MKV source into entry unless it already holds the whole clip
  // or at least min_required_frames of it. Returns false and sets error on
  // decode failure. Caller must hold entry->decode_mutex.
  static bool EnsureMkvDecoded(DecodedSource* entry, const Section& section,
                               Standard standard, int min_required_frames,
                               bool require_complete, std::string* error);

  // Decodes the section's source into the cache, ignoring failures. Runs on
  // the prefetch thread.
  void DecodeSectionIntoCache(const Section& section, int frame_index,
                              Standard standard) const;

  // Prefetch thread body: decodes queued requests until the source is
  // destroyed.
  void PrefetchLoop() const;

  // Guards the cache table only; never held across a decode.
  mutable std::mutex cache_mutex_;

  // Most-recently-used first; at most kMaxCachedSources entries.
  mutable std::vector<DecodedSourcePtr> cached_sources_;

  // Guards the prefetch queue and thread lifecycle below.
  mutable std::mutex prefetch_mutex_;
  mutable std::condition_variable prefetch_cv_;
  // Queued prefetches: the section, the first frame index it will request,
  // and the target standard.
  struct PrefetchRequest {
    Section section;
    int frame_index = 0;
    Standard standard = Standard::kUnknown;
  };
  mutable std::vector<PrefetchRequest> prefetch_queue_;
  mutable std::thread prefetch_thread_;
  mutable bool prefetch_stopping_ = false;
};

}  // namespace videosynth