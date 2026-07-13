/*
 * File:        generation_preferences.h
 * Module:      gui
 * Purpose:     Persisted generation run preferences mapped to RunOptions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QSettings>
#include <QString>

#include "videosynth/interfaces.h"

namespace videosynth::gui {

// User preferences applied to every GUI pipeline run, persisted via
// QSettings under the "generation/" group.
struct GenerationPreferences {
  // Frame synthesis worker threads; RunOptions convention (0 = auto,
  // 1 = sequential, N = N workers).
  int threads = 0;
  // "info", "debug", or "trace" (unknown values fall back to "info").
  QString log_level = QStringLiteral("info");
  bool log_to_file = false;
  QString log_file_path;
};

bool operator==(const GenerationPreferences& a, const GenerationPreferences& b);
inline bool operator!=(const GenerationPreferences& a,
                       const GenerationPreferences& b) {
  return !(a == b);
}

// Returns `level` when it is a recognised log level, otherwise "info".
//
// Thread-safety: thread-safe (pure function).
QString SanitizedLogLevel(const QString& level);

// Reads preferences from `settings`, sanitising out-of-range values (a
// negative thread count becomes auto, unknown log levels become "info").
//
// Thread-safety: NOT thread-safe (QSettings access); owning thread only.
GenerationPreferences LoadGenerationPreferences(const QSettings& settings);

// Ownership: `settings` must be valid (non-null); the caller retains
// ownership.
//
// Thread-safety: NOT thread-safe (QSettings access); owning thread only.
void SaveGenerationPreferences(const GenerationPreferences& preferences,
                               QSettings* settings);

// Maps preferences to pipeline RunOptions. The log file path is only
// applied when log_to_file is enabled and the path is non-empty.
//
// Thread-safety: thread-safe (pure function).
RunOptions MakeRunOptions(const GenerationPreferences& preferences,
                          bool validate_only = false);

}  // namespace videosynth::gui
