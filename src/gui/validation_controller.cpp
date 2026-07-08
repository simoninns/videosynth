/*
 * File:        validation_controller.cpp
 * Module:      gui
 * Purpose:     Debounced background project validation for the GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "validation_controller.h"

#include <utility>

#include "videosynth/project_validator.h"

namespace videosynth::gui {

namespace {

constexpr int kDefaultDebounceMsec = 300;

// Fresh validator per run: ProjectValidator instances are not thread-safe,
// but a worker-local instance with no probe (no filesystem access) is.
ValidationResult RunStructuralValidation(const Project& project) {
  ProjectValidator validator;
  return validator.Validate(project);
}

}  // namespace

ValidationController::ValidationController(ValidatorFn validator,
                                           QObject* parent)
    : QObject(parent), validator_(std::move(validator)) {
  if (!validator_) {
    validator_ = RunStructuralValidation;
  }
  debounce_timer_.setSingleShot(true);
  debounce_timer_.setInterval(kDefaultDebounceMsec);
  connect(&debounce_timer_, &QTimer::timeout, this,
          &ValidationController::StartWorker);
}

ValidationController::~ValidationController() { JoinWorker(); }

void ValidationController::SetDebounceInterval(int msec) {
  debounce_timer_.setInterval(msec);
}

void ValidationController::RequestValidation(const Project& project) {
  pending_project_ = project;
  debounce_timer_.start();
}

void ValidationController::StartWorker() {
  if (worker_active_ || !pending_project_.has_value()) {
    // An active run publishes via PublishResult, which re-enters here to
    // pick up the pending snapshot.
    return;
  }

  Project project = std::move(*pending_project_);
  pending_project_.reset();
  JoinWorker();
  worker_active_ = true;
  emit ValidationStarted();

  worker_ = std::thread([this, project = std::move(project)] {
    const ValidationResult result = validator_(project);
    std::vector<ValidationIssue> issues =
        BuildValidationIssues(project, result);
    // Marshal back to the owning thread; if the controller is destroyed
    // before delivery, Qt drops the queued call with the context object.
    QMetaObject::invokeMethod(
        this,
        [this, issues = std::move(issues)]() mutable {
          PublishResult(std::move(issues));
        },
        Qt::QueuedConnection);
  });
}

void ValidationController::PublishResult(std::vector<ValidationIssue> issues) {
  worker_active_ = false;
  has_result_ = true;
  issues_ = std::move(issues);
  emit IssuesChanged();

  // A request arrived while the run was active: validate the newest
  // snapshot immediately (the debounce already elapsed for it).
  if (pending_project_.has_value()) {
    StartWorker();
  }
}

void ValidationController::JoinWorker() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

}  // namespace videosynth::gui
