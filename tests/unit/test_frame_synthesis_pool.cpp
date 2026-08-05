/*
 * File:        test_frame_synthesis_pool.cpp
 * Module:      frame_synthesis_pool_tests
 * Purpose:     Unit tests for the ordered frame reassembly buffer and the
 *              worker-pool frame synthesis executor: ordering, bounded
 *              capacity, cancellation draining, and error propagation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "videosynth/frame_synthesis_pool.h"

namespace videosynth {
namespace {

SynthesizedFrame MakeMarkerFrame(std::size_t frame_index) {
  SynthesizedFrame frame;
  frame.y_mv.assign(4, static_cast<SampleFixed>(frame_index));
  frame.c_mv.assign(4, 0);
  return frame;
}

// ---------------------------------------------------------------------------
// OrderedFrameReassemblyBuffer
// ---------------------------------------------------------------------------

TEST(OrderedFrameReassemblyTest, DeliversOutOfOrderPushesInFrameOrder) {
  OrderedFrameReassemblyBuffer buffer(4U, 4U);

  ASSERT_TRUE(buffer.BeginFrame(3U));
  ASSERT_TRUE(buffer.Push(3U, MakeMarkerFrame(3U)));
  ASSERT_TRUE(buffer.BeginFrame(1U));
  ASSERT_TRUE(buffer.Push(1U, MakeMarkerFrame(1U)));
  ASSERT_TRUE(buffer.BeginFrame(0U));
  ASSERT_TRUE(buffer.Push(0U, MakeMarkerFrame(0U)));
  ASSERT_TRUE(buffer.BeginFrame(2U));
  ASSERT_TRUE(buffer.Push(2U, MakeMarkerFrame(2U)));

  for (std::size_t expected = 0U; expected < 4U; ++expected) {
    std::size_t frame_index = 99U;
    SynthesizedFrame frame;
    ASSERT_TRUE(buffer.Pop(&frame_index, &frame));
    EXPECT_EQ(frame_index, expected);
    ASSERT_FALSE(frame.y_mv.empty());
    EXPECT_EQ(frame.y_mv[0], static_cast<SampleFixed>(expected));
  }

  // All frames delivered: further pops report completion.
  std::size_t frame_index = 0U;
  SynthesizedFrame frame;
  EXPECT_FALSE(buffer.Pop(&frame_index, &frame));
}

TEST(OrderedFrameReassemblyTest, BeginFrameBlocksOutsideBoundedWindow) {
  OrderedFrameReassemblyBuffer buffer(8U, 2U);

  // Window is [0, 2): frame 2 must wait until frame 0 is consumed.
  ASSERT_TRUE(buffer.BeginFrame(0U));
  ASSERT_TRUE(buffer.Push(0U, MakeMarkerFrame(0U)));

  std::atomic<bool> begin_returned{false};
  std::thread producer([&]() {
    EXPECT_TRUE(buffer.BeginFrame(2U));
    begin_returned.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(begin_returned.load())
      << "BeginFrame(2) returned while the window still covered frames 0-1.";

  // Consuming frame 0 slides the window to [1, 3) and releases the producer.
  std::size_t frame_index = 0U;
  SynthesizedFrame frame;
  ASSERT_TRUE(buffer.Pop(&frame_index, &frame));
  producer.join();
  EXPECT_TRUE(begin_returned.load());
}

TEST(OrderedFrameReassemblyTest, AbortReleasesBlockedProducerAndConsumer) {
  OrderedFrameReassemblyBuffer buffer(8U, 1U);

  std::atomic<bool> producer_result{true};
  std::atomic<bool> consumer_result{true};
  std::thread producer([&]() { producer_result.store(buffer.BeginFrame(5U)); });
  std::thread consumer([&]() {
    std::size_t frame_index = 0U;
    SynthesizedFrame frame;
    consumer_result.store(buffer.Pop(&frame_index, &frame));
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  buffer.Abort();
  producer.join();
  consumer.join();

  EXPECT_FALSE(producer_result.load());
  EXPECT_FALSE(consumer_result.load());
  EXPECT_TRUE(buffer.aborted());
  EXPECT_FALSE(buffer.Push(0U, MakeMarkerFrame(0U)));
}

// ---------------------------------------------------------------------------
// FrameSynthesisPool
// ---------------------------------------------------------------------------

TEST(FrameSynthesisPoolTest, ResolveThreadCountMapsRunOptionsConvention) {
  EXPECT_GE(FrameSynthesisPool::ResolveThreadCount(0), 1U);
  EXPECT_EQ(FrameSynthesisPool::ResolveThreadCount(1), 1U);
  EXPECT_EQ(FrameSynthesisPool::ResolveThreadCount(6), 6U);
  EXPECT_GE(FrameSynthesisPool::ResolveThreadCount(-3), 1U);
}

TEST(FrameSynthesisPoolTest, ConsumerReceivesEveryFrameInOrder) {
  constexpr std::size_t kFrameCount = 64U;
  FrameSynthesisPool pool(4U);

  auto job = [](std::size_t frame_index, SynthesizedFrame* out_frame) {
    // Vary per-frame latency so completion order differs from frame order.
    std::this_thread::sleep_for(
        std::chrono::microseconds((frame_index * 7U) % 5U * 100U));
    *out_frame = MakeMarkerFrame(frame_index);
  };

  std::vector<std::size_t> consumed;
  auto consumer = [&](std::size_t frame_index, SynthesizedFrame&& frame) {
    EXPECT_EQ(frame.y_mv[0], static_cast<SampleFixed>(frame_index));
    consumed.push_back(frame_index);
    return true;
  };

  std::vector<std::string> errors;
  EXPECT_TRUE(pool.RunOrdered(kFrameCount, job, consumer, nullptr, &errors));
  EXPECT_TRUE(errors.empty());

  ASSERT_EQ(consumed.size(), kFrameCount);
  for (std::size_t i = 0; i < kFrameCount; ++i) {
    EXPECT_EQ(consumed[i], i);
  }
}

TEST(FrameSynthesisPoolTest, InFlightFramesStayWithinBoundedCapacity) {
  constexpr std::size_t kFrameCount = 48U;
  constexpr unsigned kThreads = 3U;
  FrameSynthesisPool pool(kThreads);

  // A frame is "in flight" from job start until consumption. The pool bounds
  // its reassembly window to 2 × thread count, but the counter below peaks one
  // higher, because the window slides inside Pop while the matching decrement
  // happens later in the consumer callback:
  //
  //   with P frames popped, BeginFrame admits only indices < P + 2 × kThreads,
  //   so at most P + 2 × kThreads increments have run; RunOrdered pops and
  //   consumes strictly in turn, so at least P - 1 decrements have run. The
  //   counter therefore tops out at 2 × kThreads + 1, reached whenever a worker
  //   is admitted in the gap between a pop and its consumer callback.
  //
  // Asserting 2 × kThreads made this fail intermittently under load. The exact
  // window bound is covered deterministically by
  // OrderedFrameReassemblyTest.BeginFrameBlocksOutsideBoundedWindow.
  constexpr int kMaxObservableInFlight = static_cast<int>(kThreads) * 2 + 1;

  std::atomic<int> in_flight{0};
  std::atomic<int> max_in_flight{0};

  auto job = [&](std::size_t frame_index, SynthesizedFrame* out_frame) {
    const int current = in_flight.fetch_add(1) + 1;
    int observed_max = max_in_flight.load();
    while (current > observed_max &&
           !max_in_flight.compare_exchange_weak(observed_max, current)) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    *out_frame = MakeMarkerFrame(frame_index);
  };

  auto consumer = [&](std::size_t, SynthesizedFrame&&) {
    in_flight.fetch_sub(1);
    return true;
  };

  std::vector<std::string> errors;
  EXPECT_TRUE(pool.RunOrdered(kFrameCount, job, consumer, nullptr, &errors));
  EXPECT_LE(max_in_flight.load(), kMaxObservableInFlight);
}

TEST(FrameSynthesisPoolTest, CancellationStopsRunAndDrainsWorkers) {
  constexpr std::size_t kFrameCount = 100U;
  FrameSynthesisPool pool(4U);
  CancellationToken cancellation;

  auto job = [](std::size_t frame_index, SynthesizedFrame* out_frame) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    *out_frame = MakeMarkerFrame(frame_index);
  };

  std::size_t consumed = 0U;
  auto consumer = [&](std::size_t, SynthesizedFrame&&) {
    ++consumed;
    if (consumed == 3U) {
      cancellation.RequestCancellation();
    }
    return true;
  };

  std::vector<std::string> errors;
  // RunOrdered joining its workers proves the pool drains cleanly on
  // cancellation; a hang here fails via the test timeout.
  EXPECT_FALSE(
      pool.RunOrdered(kFrameCount, job, consumer, &cancellation, &errors));
  EXPECT_TRUE(errors.empty()) << "Cancellation must not report errors.";
  EXPECT_LT(consumed, kFrameCount);
}

TEST(FrameSynthesisPoolTest, JobErrorStopsRunAndReportsMessages) {
  constexpr std::size_t kFrameCount = 32U;
  FrameSynthesisPool pool(4U);

  auto job = [](std::size_t frame_index, SynthesizedFrame* out_frame) {
    if (frame_index == 5U) {
      out_frame->ok = false;
      out_frame->errors.push_back("synthetic frame failure");
      return;
    }
    *out_frame = MakeMarkerFrame(frame_index);
  };

  std::size_t consumed = 0U;
  auto consumer = [&](std::size_t, SynthesizedFrame&&) {
    ++consumed;
    return true;
  };

  std::vector<std::string> errors;
  EXPECT_FALSE(pool.RunOrdered(kFrameCount, job, consumer, nullptr, &errors));
  ASSERT_EQ(errors.size(), 1U);
  EXPECT_EQ(errors[0], "synthetic frame failure");
  // Frames before the failing index were consumed in order; nothing after.
  EXPECT_EQ(consumed, 5U);
}

TEST(FrameSynthesisPoolTest, WorkerExceptionPropagatesAsPipelineError) {
  constexpr std::size_t kFrameCount = 16U;
  FrameSynthesisPool pool(2U);

  auto job = [](std::size_t frame_index, SynthesizedFrame* out_frame) {
    if (frame_index == 2U) {
      throw std::runtime_error("worker blew up");
    }
    *out_frame = MakeMarkerFrame(frame_index);
  };

  auto consumer = [](std::size_t, SynthesizedFrame&&) { return true; };

  std::vector<std::string> errors;
  EXPECT_FALSE(pool.RunOrdered(kFrameCount, job, consumer, nullptr, &errors));
  ASSERT_EQ(errors.size(), 1U);
  EXPECT_NE(errors[0].find("worker blew up"), std::string::npos);
}

TEST(FrameSynthesisPoolTest, ConsumerFailureStopsRun) {
  constexpr std::size_t kFrameCount = 24U;
  FrameSynthesisPool pool(3U);

  auto job = [](std::size_t frame_index, SynthesizedFrame* out_frame) {
    *out_frame = MakeMarkerFrame(frame_index);
  };

  std::size_t consumed = 0U;
  auto consumer = [&](std::size_t, SynthesizedFrame&&) {
    ++consumed;
    return consumed < 4U;
  };

  std::vector<std::string> errors;
  EXPECT_FALSE(pool.RunOrdered(kFrameCount, job, consumer, nullptr, &errors));
  EXPECT_EQ(consumed, 4U);
}

TEST(FrameSynthesisPoolTest, ZeroFramesSucceedsImmediately) {
  FrameSynthesisPool pool(4U);
  auto job = [](std::size_t, SynthesizedFrame*) { FAIL() << "no jobs"; };
  auto consumer = [](std::size_t, SynthesizedFrame&&) { return true; };
  std::vector<std::string> errors;
  EXPECT_TRUE(pool.RunOrdered(0U, job, consumer, nullptr, &errors));
}

}  // namespace
}  // namespace videosynth
