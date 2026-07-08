/*
 * File:        frame_synthesis_pool.cpp
 * Module:      frame_synthesis_pool
 * Purpose:     Worker-thread pool that synthesises frames out of order and an
 *              ordered reassembly buffer that delivers results in frame order
 *              with bounded memory.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/frame_synthesis_pool.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <thread>
#include <utility>

namespace videosynth {

OrderedFrameReassemblyBuffer::OrderedFrameReassemblyBuffer(
    std::size_t total_frames, std::size_t max_in_flight)
    : total_frames_(total_frames),
      max_in_flight_(std::max<std::size_t>(1U, max_in_flight)) {}

bool OrderedFrameReassemblyBuffer::BeginFrame(std::size_t frame_index) {
  std::unique_lock<std::mutex> lock(mutex_);
  producer_cv_.wait(lock, [&]() {
    return aborted_ || frame_index < next_consume_index_ + max_in_flight_;
  });
  return !aborted_;
}

bool OrderedFrameReassemblyBuffer::Push(std::size_t frame_index,
                                        SynthesizedFrame&& frame) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (aborted_) {
      return false;
    }
    completed_frames_.emplace(frame_index, std::move(frame));
  }
  consumer_cv_.notify_one();
  return true;
}

bool OrderedFrameReassemblyBuffer::Pop(std::size_t* out_frame_index,
                                       SynthesizedFrame* out_frame) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (next_consume_index_ >= total_frames_) {
    return false;
  }
  consumer_cv_.wait(lock, [&]() {
    return aborted_ || completed_frames_.count(next_consume_index_) != 0U;
  });
  if (aborted_) {
    return false;
  }

  const auto it = completed_frames_.find(next_consume_index_);
  *out_frame_index = next_consume_index_;
  *out_frame = std::move(it->second);
  completed_frames_.erase(it);
  ++next_consume_index_;

  // Consuming a frame slides the bounded window forward, releasing producers
  // waiting in BeginFrame.
  lock.unlock();
  producer_cv_.notify_all();
  return true;
}

void OrderedFrameReassemblyBuffer::Abort() {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    aborted_ = true;
    completed_frames_.clear();
  }
  producer_cv_.notify_all();
  consumer_cv_.notify_all();
}

bool OrderedFrameReassemblyBuffer::aborted() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return aborted_;
}

// static
unsigned FrameSynthesisPool::ResolveThreadCount(int requested_threads) {
  if (requested_threads <= 0) {
    return std::max(1U, std::thread::hardware_concurrency());
  }
  return static_cast<unsigned>(requested_threads);
}

FrameSynthesisPool::FrameSynthesisPool(unsigned thread_count)
    : thread_count_(std::max(1U, thread_count)) {}

bool FrameSynthesisPool::RunOrdered(std::size_t frame_count,
                                    const FrameJob& job,
                                    const FrameConsumer& consumer,
                                    CancellationToken* cancellation,
                                    std::vector<std::string>* errors) {
  if (frame_count == 0U) {
    return true;
  }

  // Bound in-flight frames to twice the worker count so peak memory scales
  // with the pool size, not the project length.
  OrderedFrameReassemblyBuffer buffer(
      frame_count, static_cast<std::size_t>(thread_count_) * 2U);

  std::atomic<std::size_t> next_claim{0U};
  auto IsCancelled = [cancellation]() {
    return cancellation != nullptr && cancellation->IsCancellationRequested();
  };

  auto WorkerLoop = [&]() {
    for (;;) {
      if (buffer.aborted() || IsCancelled()) {
        return;
      }
      const std::size_t frame_index =
          next_claim.fetch_add(1U, std::memory_order_relaxed);
      if (frame_index >= frame_count) {
        return;
      }
      if (!buffer.BeginFrame(frame_index)) {
        return;
      }

      SynthesizedFrame frame;
      try {
        job(frame_index, &frame);
      } catch (const std::exception& exception) {
        frame.ok = false;
        frame.errors.push_back(
            std::string("Frame synthesis worker exception: ") +
            exception.what());
      } catch (...) {
        frame.ok = false;
        frame.errors.push_back(
            "Frame synthesis worker raised an unknown exception.");
      }

      if (!buffer.Push(frame_index, std::move(frame))) {
        return;
      }
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(thread_count_);
  for (unsigned i = 0; i < thread_count_; ++i) {
    workers.emplace_back(WorkerLoop);
  }

  // Consume on the calling thread in strict frame order. Any failure aborts
  // the buffer, which drains the workers.
  bool run_ok = true;
  std::size_t frames_consumed = 0U;
  while (frames_consumed < frame_count) {
    if (IsCancelled()) {
      run_ok = false;
      break;
    }

    std::size_t frame_index = 0U;
    SynthesizedFrame frame;
    if (!buffer.Pop(&frame_index, &frame)) {
      run_ok = false;
      break;
    }

    if (!frame.ok) {
      if (errors != nullptr) {
        errors->insert(errors->end(), frame.errors.begin(), frame.errors.end());
      }
      run_ok = false;
      break;
    }

    if (!consumer(frame_index, std::move(frame))) {
      run_ok = false;
      break;
    }
    ++frames_consumed;
  }

  buffer.Abort();
  for (std::thread& worker : workers) {
    worker.join();
  }

  return run_ok && frames_consumed == frame_count;
}

}  // namespace videosynth
