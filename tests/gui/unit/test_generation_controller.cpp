/*
 * File:        test_generation_controller.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for the worker-thread generation controller
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "forwarding_logger.h"
#include "generation_controller.h"
#include "videosynth/interfaces.h"
#include "videosynth/model.h"

namespace videosynth::gui {
namespace {

using RunStatus = GenerationController::RunStatus;

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

// Records every controller signal in emission order for sequence assertions.
struct SignalRecorder {
  explicit SignalRecorder(GenerationController* controller) {
    QObject::connect(controller, &GenerationController::RunStarted,
                     [this] { events.push_back("started"); });
    QObject::connect(controller, &GenerationController::StageStarted,
                     [this](const QString& stage) {
                       events.push_back("stage:" + stage.toStdString());
                     });
    QObject::connect(
        controller, &GenerationController::FrameProgress,
        [this](qulonglong completed, qulonglong total) {
          events.push_back("progress:" + std::to_string(completed) + "/" +
                           std::to_string(total));
          progress.emplace_back(completed, total);
        });
    QObject::connect(controller, &GenerationController::WarningReported,
                     [this](const QString& message) {
                       events.push_back("warning:" + message.toStdString());
                     });
    QObject::connect(controller, &GenerationController::RunFinished,
                     [this](RunStatus status) {
                       events.push_back("finished");
                       finish_statuses.push_back(status);
                       finished = true;
                     });
  }

  std::vector<std::string> events;
  std::vector<std::pair<qulonglong, qulonglong>> progress;
  std::vector<RunStatus> finish_statuses;
  bool finished = false;
};

TEST(GenerationControllerTest, SuccessfulRunEmitsSignalSequenceInOrder) {
  GenerationController controller([](const Project&, const RunOptions&,
                                     IPipelineObserver* observer,
                                     CancellationToken*) {
    observer->OnStageStarted("validate");
    observer->OnWarning("example warning");
    observer->OnStageStarted("generate");
    observer->OnFrameProgress(1, 2);
    observer->OnFrameProgress(2, 2);
    observer->OnStageStarted("finalize");
    observer->OnRunFinished(PipelineRunStatus::kSucceeded);
    return true;
  });
  SignalRecorder recorder(&controller);

  ASSERT_TRUE(controller.StartGeneration(Project{}, RunOptions{}));
  EXPECT_TRUE(controller.is_running());
  ASSERT_TRUE(PumpUntil([&] { return recorder.finished; }, 5000));

  const std::vector<std::string> expected = {
      "started",        "stage:validate", "warning:example warning",
      "stage:generate", "progress:1/2",   "progress:2/2",
      "stage:finalize", "finished",
  };
  EXPECT_EQ(recorder.events, expected);
  ASSERT_EQ(recorder.finish_statuses.size(), 1U);
  EXPECT_EQ(recorder.finish_statuses[0], RunStatus::kSucceeded);
  EXPECT_FALSE(controller.is_running());
}

TEST(GenerationControllerTest, PipelineExecutesOffTheUiThread) {
  const std::thread::id ui_thread = std::this_thread::get_id();
  std::thread::id pipeline_thread;

  GenerationController controller([&](const Project&, const RunOptions&,
                                      IPipelineObserver* observer,
                                      CancellationToken*) {
    pipeline_thread = std::this_thread::get_id();
    observer->OnRunFinished(PipelineRunStatus::kSucceeded);
    return true;
  });
  SignalRecorder recorder(&controller);

  ASSERT_TRUE(controller.StartGeneration(Project{}, RunOptions{}));
  ASSERT_TRUE(PumpUntil([&] { return recorder.finished; }, 5000));
  EXPECT_NE(pipeline_thread, ui_thread);
}

TEST(GenerationControllerTest, OnlyOneRunMayBeActiveAtATime) {
  std::atomic<bool> release{false};
  GenerationController controller([&](const Project&, const RunOptions&,
                                      IPipelineObserver* observer,
                                      CancellationToken*) {
    while (!release.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    observer->OnRunFinished(PipelineRunStatus::kSucceeded);
    return true;
  });
  SignalRecorder recorder(&controller);

  ASSERT_TRUE(controller.StartGeneration(Project{}, RunOptions{}));
  // A second start while the first run is active must be rejected without
  // side effects.
  EXPECT_FALSE(controller.StartGeneration(Project{}, RunOptions{}));

  release = true;
  ASSERT_TRUE(PumpUntil([&] { return recorder.finished; }, 5000));
  ASSERT_EQ(recorder.finish_statuses.size(), 1U);

  // After the run finished a new one is accepted again.
  recorder.finished = false;
  ASSERT_TRUE(controller.StartGeneration(Project{}, RunOptions{}));
  ASSERT_TRUE(PumpUntil([&] { return recorder.finished; }, 5000));
  EXPECT_EQ(recorder.finish_statuses.size(), 2U);
}

TEST(GenerationControllerTest, CancellationPropagatesToTokenAndReportsStatus) {
  GenerationController controller([](const Project&, const RunOptions&,
                                     IPipelineObserver* observer,
                                     CancellationToken* cancellation) {
    std::size_t frame = 0;
    while (!cancellation->IsCancellationRequested() && frame < 10000U) {
      observer->OnFrameProgress(++frame, 10000U);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    observer->OnRunFinished(cancellation->IsCancellationRequested()
                                ? PipelineRunStatus::kCancelled
                                : PipelineRunStatus::kSucceeded);
    return false;
  });
  SignalRecorder recorder(&controller);

  ASSERT_TRUE(controller.StartGeneration(Project{}, RunOptions{}));
  ASSERT_TRUE(PumpUntil([&] { return !recorder.progress.empty(); }, 5000));
  controller.RequestCancellation();

  ASSERT_TRUE(PumpUntil([&] { return recorder.finished; }, 5000));
  ASSERT_EQ(recorder.finish_statuses.size(), 1U);
  EXPECT_EQ(recorder.finish_statuses[0], RunStatus::kCancelled);
  EXPECT_FALSE(controller.is_running());
}

TEST(GenerationControllerTest, FailedRunReportsFailedStatus) {
  GenerationController controller([](const Project&, const RunOptions&,
                                     IPipelineObserver* observer,
                                     CancellationToken*) {
    observer->OnStageStarted("validate");
    observer->OnRunFinished(PipelineRunStatus::kFailed);
    return false;
  });
  SignalRecorder recorder(&controller);

  ASSERT_TRUE(controller.StartGeneration(Project{}, RunOptions{}));
  ASSERT_TRUE(PumpUntil([&] { return recorder.finished; }, 5000));
  ASSERT_EQ(recorder.finish_statuses.size(), 1U);
  EXPECT_EQ(recorder.finish_statuses[0], RunStatus::kFailed);
}

TEST(GenerationControllerTest, ProgressIsMonotonicNonDecreasing) {
  GenerationController controller([](const Project&, const RunOptions&,
                                     IPipelineObserver* observer,
                                     CancellationToken*) {
    for (std::size_t frame = 1; frame <= 25U; ++frame) {
      observer->OnFrameProgress(frame, 25U);
    }
    observer->OnRunFinished(PipelineRunStatus::kSucceeded);
    return true;
  });
  SignalRecorder recorder(&controller);

  ASSERT_TRUE(controller.StartGeneration(Project{}, RunOptions{}));
  ASSERT_TRUE(PumpUntil([&] { return recorder.finished; }, 5000));

  ASSERT_FALSE(recorder.progress.empty());
  for (std::size_t i = 1; i < recorder.progress.size(); ++i) {
    EXPECT_GE(recorder.progress[i].first, recorder.progress[i - 1].first);
  }
  EXPECT_EQ(recorder.progress.back().first, recorder.progress.back().second);
}

// --- ForwardingLogger -------------------------------------------------------

class RecordingLogger final : public ILogger {
 public:
  void Info(const std::string& message) override { Record("info", message); }
  void Warning(const std::string& message) override {
    Record("warning", message);
  }
  void Error(const std::string& message) override { Record("error", message); }
  void Debug(const std::string& message) override { Record("debug", message); }
  void Trace(const std::string& message) override { Record("trace", message); }

  std::vector<std::string> messages;

 private:
  void Record(const std::string& level, const std::string& message) {
    messages.push_back(level + ":" + message);
  }
};

TEST(ForwardingLoggerTest, ForwardsToInnerLoggerAndCallback) {
  RecordingLogger inner;
  std::vector<std::pair<LogSeverity, std::string>> forwarded;
  ForwardingLogger logger(&inner, LogLevel::kInfo,
                          [&](LogSeverity severity, const std::string& text) {
                            forwarded.emplace_back(severity, text);
                          });

  logger.Info("i");
  logger.Warning("w");
  logger.Error("e");

  const std::vector<std::string> expected_inner = {"info:i", "warning:w",
                                                   "error:e"};
  EXPECT_EQ(inner.messages, expected_inner);
  ASSERT_EQ(forwarded.size(), 3U);
  EXPECT_EQ(forwarded[0].first, LogSeverity::kInfo);
  EXPECT_EQ(forwarded[1].first, LogSeverity::kWarning);
  EXPECT_EQ(forwarded[2].first, LogSeverity::kError);
}

TEST(ForwardingLoggerTest, LevelFilterSuppressesDebugAndTraceCallbacks) {
  RecordingLogger inner;
  std::vector<std::pair<LogSeverity, std::string>> forwarded;
  ForwardingLogger logger(&inner, LogLevel::kInfo,
                          [&](LogSeverity severity, const std::string& text) {
                            forwarded.emplace_back(severity, text);
                          });

  logger.Debug("d");
  logger.Trace("t");

  // Inner logger always receives the call (it applies its own level);
  // the GUI callback is filtered at the configured level.
  EXPECT_EQ(inner.messages.size(), 2U);
  EXPECT_TRUE(forwarded.empty());
}

TEST(ForwardingLoggerTest, DebugLevelForwardsDebugButNotTrace) {
  RecordingLogger inner;
  std::vector<LogSeverity> severities;
  ForwardingLogger logger(&inner, LogLevel::kDebug,
                          [&](LogSeverity severity, const std::string&) {
                            severities.push_back(severity);
                          });

  logger.Debug("d");
  logger.Trace("t");

  ASSERT_EQ(severities.size(), 1U);
  EXPECT_EQ(severities[0], LogSeverity::kDebug);
}

TEST(ForwardingLoggerTest, TraceLevelForwardsEverything) {
  RecordingLogger inner;
  std::vector<LogSeverity> severities;
  ForwardingLogger logger(&inner, LogLevel::kTrace,
                          [&](LogSeverity severity, const std::string&) {
                            severities.push_back(severity);
                          });

  logger.Debug("d");
  logger.Trace("t");

  ASSERT_EQ(severities.size(), 2U);
  EXPECT_EQ(severities[0], LogSeverity::kDebug);
  EXPECT_EQ(severities[1], LogSeverity::kTrace);
}

}  // namespace
}  // namespace videosynth::gui
