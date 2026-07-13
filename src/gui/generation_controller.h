/*
 * File:        generation_controller.h
 * Module:      gui
 * Purpose:     Runs the generation pipeline on a worker thread for the GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QObject>
#include <QString>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "forwarding_logger.h"
#include "videosynth/interfaces.h"
#include "videosynth/model.h"

namespace videosynth::gui {

// Returns a copy of `project` with relative paths (section sources and
// output video/metadata targets) resolved against `base_dir`. Absolute paths
// and empty fields are left unchanged; an empty base_dir returns the project
// untouched. The pipeline resolves relative paths against the process
// working directory, so the GUI must anchor them to the project file's
// directory before starting a worker-thread run.
//
// Thread-safety: thread-safe (pure function).
Project ResolveProjectPaths(Project project, const std::string& base_dir);

// Runs VideoSynthPipeline::RunProject on a dedicated worker thread and
// bridges IPipelineObserver callbacks to queued Qt signals, so the GUI
// thread never executes pipeline code. One run may be active at a time;
// StartGeneration returns false while a run is in progress. Cancellation is
// cooperative via the pipeline's CancellationToken (a fresh token per run).
//
// Thread-safety: all public members must be called from the owning (GUI)
// thread, which must run an event loop for signals to be delivered. The
// pipeline runner executes on the worker thread; the injected runner must
// therefore be safe to run off the GUI thread (the default constructs fresh
// pipeline stages per run, which satisfies this).
class GenerationController : public QObject {
  Q_OBJECT

 public:
  // Terminal status of a generation run (mirrors PipelineRunStatus).
  enum class RunStatus {
    kSucceeded,
    kCancelled,
    kFailed,
  };
  Q_ENUM(RunStatus)

  // Callable executed on the worker thread for each run. Must report the
  // terminal status exactly once through the observer's OnRunFinished (the
  // pipeline's RunProject contract) and honour the cancellation token.
  using RunnerFn = std::function<bool(
      const Project& project, const RunOptions& options,
      IPipelineObserver* observer, CancellationToken* cancellation)>;

  // runner: callable executed on the worker thread; when empty, a default
  // that constructs the full concrete pipeline (parser, validator,
  // generation, noise, dropout, output, audio) with a logger forwarding to
  // the LogMessageReceived signal is used.
  explicit GenerationController(RunnerFn runner = {},
                                QObject* parent = nullptr);
  ~GenerationController() override;

  // True from a successful StartGeneration until RunFinished has been
  // emitted.
  bool is_running() const { return running_; }

  // Starts a run over a snapshot of `project`. Relative paths must already
  // be resolved (see ResolveProjectPaths). Returns false — without side
  // effects — when a run is already active.
  bool StartGeneration(const Project& project, const RunOptions& options);

  // Requests cooperative cancellation of the active run; no-op when idle.
  // The pipeline stops at its next cancellation check and removes all
  // in-progress output artefacts before RunFinished(kCancelled) is emitted.
  void RequestCancellation();

 signals:
  // A run has been accepted and its worker thread started.
  void RunStarted();

  // Pipeline stage transition ("validate", "generate", "finalize").
  void StageStarted(const QString& stage_name);

  // Frame progress; frames_completed is monotonic non-decreasing and
  // reaches frames_total on an uncancelled run.
  void FrameProgress(qulonglong frames_completed, qulonglong frames_total);

  // Non-fatal pipeline warning (for example a validation warning).
  void WarningReported(const QString& message);

  // Log line captured from the run's logger (default runner only).
  // severity is a LogSeverity value.
  void LogMessageReceived(int severity, const QString& message);

  // Terminal status; emitted exactly once per started run, after which
  // is_running() is false again.
  void RunFinished(GenerationController::RunStatus status);

 private:
  // Bridges pipeline observer callbacks (worker thread) onto the
  // controller's thread via queued invocations.
  class ObserverBridge;

  // Default runner: constructs the concrete pipeline stages and executes
  // RunProject. Runs on the worker thread.
  void RunDefaultPipeline(const Project& project, const RunOptions& options,
                          IPipelineObserver* observer,
                          CancellationToken* cancellation);

  void HandleRunFinished(RunStatus status);
  void JoinWorker();

  RunnerFn runner_;
  std::thread worker_;
  bool running_ = false;
  // One-shot token for the active run; replaced (never reset) per run.
  std::unique_ptr<CancellationToken> cancellation_;
};

}  // namespace videosynth::gui
