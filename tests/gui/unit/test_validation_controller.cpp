/*
 * File:        test_validation_controller.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for debounced background validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "validation_controller.h"
#include "videosynth/model.h"
#include "videosynth/results.h"

namespace videosynth::gui {
namespace {

// Pumps the owning thread's event loop until `predicate` is true or the
// timeout elapses. Returns the predicate's final value.
bool PumpUntil(const std::function<bool()>& predicate, int timeout_msec) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_msec) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return predicate();
}

Project MakeProjectNamed(const std::string& name) {
  Project project;
  project.name = name;
  return project;
}

TEST(ValidationControllerTest, RunsValidatorOffTheCallingThread) {
  const std::thread::id main_thread = std::this_thread::get_id();
  std::atomic<bool> ran{false};
  std::thread::id validation_thread;

  ValidationController controller([&](const Project&) -> ValidationResult {
    validation_thread = std::this_thread::get_id();
    ran = true;
    return {};
  });
  controller.SetDebounceInterval(0);
  controller.RequestValidation(Project{});

  ASSERT_TRUE(PumpUntil([&] { return controller.has_result(); }, 5000));
  EXPECT_TRUE(ran);
  EXPECT_NE(validation_thread, main_thread);
}

TEST(ValidationControllerTest, SlowValidatorDoesNotBlockCaller) {
  constexpr int kValidatorDelayMsec = 200;
  ValidationController controller([&](const Project&) -> ValidationResult {
    std::this_thread::sleep_for(std::chrono::milliseconds(kValidatorDelayMsec));
    return {};
  });
  controller.SetDebounceInterval(0);

  QElapsedTimer timer;
  timer.start();
  controller.RequestValidation(Project{});
  // Scheduling must return immediately even though the run takes 200 ms.
  EXPECT_LT(timer.elapsed(), kValidatorDelayMsec / 2);

  ASSERT_TRUE(PumpUntil([&] { return controller.has_result(); }, 5000));
  EXPECT_GE(timer.elapsed(), kValidatorDelayMsec);
}

TEST(ValidationControllerTest, InvalidProjectProducesIssuesAndFixClearsThem) {
  // Default validator: a default-constructed Project is structurally
  // invalid (unknown standard, no output paths, no sections).
  ValidationController controller;
  controller.SetDebounceInterval(0);

  controller.RequestValidation(Project{});
  ASSERT_TRUE(PumpUntil([&] { return controller.has_result(); }, 5000));
  EXPECT_FALSE(controller.issues().empty());

  // Fixing the project clears the issues.
  Project valid;
  valid.cvbs_presets.video_standard_preset = Standard::kPal;
  valid.output.video_path = "out/video.cvbs";
  valid.output.metadata_path = "out/metadata.meta";
  Section section;
  section.name = "Bars";
  section.type = "progressive";
  section.source = "assets/bars.exr";
  section.duration_frames = 10;
  valid.sections.push_back(section);

  int results_seen = 0;
  QObject::connect(&controller, &ValidationController::IssuesChanged,
                   [&results_seen] { ++results_seen; });
  controller.RequestValidation(valid);
  ASSERT_TRUE(PumpUntil([&] { return results_seen > 0; }, 5000));
  EXPECT_TRUE(controller.issues().empty());
}

TEST(ValidationControllerTest, RapidRequestsCoalesceToLatestProject) {
  std::vector<std::string> validated_names;
  ValidationController controller(
      [&](const Project& project) -> ValidationResult {
        validated_names.push_back(project.name);
        return {};
      });
  controller.SetDebounceInterval(50);

  // All three requests land within one debounce window; only the last
  // snapshot must be validated.
  controller.RequestValidation(MakeProjectNamed("first"));
  controller.RequestValidation(MakeProjectNamed("second"));
  controller.RequestValidation(MakeProjectNamed("third"));

  ASSERT_TRUE(PumpUntil([&] { return controller.has_result(); }, 5000));
  ASSERT_TRUE(PumpUntil([&] { return !controller.is_validating(); }, 5000));
  ASSERT_EQ(validated_names.size(), 1U);
  EXPECT_EQ(validated_names[0], "third");
}

TEST(ValidationControllerTest, RequestDuringActiveRunValidatesNewestAfter) {
  std::atomic<int> runs{0};
  ValidationController controller([&](const Project&) -> ValidationResult {
    ++runs;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return {};
  });
  controller.SetDebounceInterval(0);

  controller.RequestValidation(MakeProjectNamed("first"));
  ASSERT_TRUE(PumpUntil([&] { return controller.is_validating(); }, 5000));

  // Arrives while the first run is still sleeping; must trigger a second
  // (and only a second) run after the first publishes.
  controller.RequestValidation(MakeProjectNamed("second"));

  int results_seen = 0;
  QObject::connect(&controller, &ValidationController::IssuesChanged,
                   [&results_seen] { ++results_seen; });
  ASSERT_TRUE(PumpUntil(
      [&] { return results_seen >= 2 && !controller.is_validating(); }, 5000));
  EXPECT_EQ(runs, 2);
}

}  // namespace
}  // namespace videosynth::gui
