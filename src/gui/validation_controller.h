/*
 * File:        validation_controller.h
 * Module:      gui
 * Purpose:     Debounced background project validation for the GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QObject>
#include <QTimer>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

#include "validation_issues_model.h"
#include "videosynth/model.h"
#include "videosynth/results.h"

namespace videosynth::gui {

// Runs project validation off the GUI thread with debouncing and
// latest-wins coalescing: RequestValidation stores a snapshot of the
// project and (re)starts the debounce timer; when it fires, the snapshot is
// validated on a dedicated worker thread. Requests arriving while a run is
// active are queued as the single pending snapshot and validated when the
// active run finishes, so rapid edits collapse to one trailing validation.
//
// Thread-safety: all public members must be called from the owning (GUI)
// thread, which must run an event loop for results to be delivered. The
// validator function itself executes on a worker thread and must therefore
// be safe to run off the GUI thread (the default constructs a fresh
// ProjectValidator per run, which satisfies this).
class ValidationController : public QObject {
  Q_OBJECT

 public:
  using ValidatorFn = std::function<ValidationResult(const Project&)>;

  // validator: callable executed on the worker thread for each run; when
  // empty, a default that runs ProjectValidator (without a source probe, so
  // no filesystem access) is used.
  explicit ValidationController(ValidatorFn validator = {},
                                QObject* parent = nullptr);
  ~ValidationController() override;

  // Debounce delay between the last RequestValidation call and the worker
  // start. Default 300 ms; use 0 in tests for immediate scheduling.
  void SetDebounceInterval(int msec);

  // Schedules validation of a snapshot of `project` (latest-wins).
  void RequestValidation(const Project& project);

  // True from worker start until its result has been published.
  bool is_validating() const { return worker_active_; }

  // True once at least one validation run has completed.
  bool has_result() const { return has_result_; }

  // Issues from the most recent completed run.
  const std::vector<ValidationIssue>& issues() const { return issues_; }

 signals:
  // Worker run started (after debounce).
  void ValidationStarted();

  // issues() has been replaced with the newest run's results.
  void IssuesChanged();

 private:
  void StartWorker();
  void PublishResult(std::vector<ValidationIssue> issues);
  void JoinWorker();

  ValidatorFn validator_;
  QTimer debounce_timer_;
  std::optional<Project> pending_project_;
  std::thread worker_;
  bool worker_active_ = false;
  bool has_result_ = false;
  std::vector<ValidationIssue> issues_;
};

}  // namespace videosynth::gui
