/*
 * File:        generation_preferences.cpp
 * Module:      gui
 * Purpose:     Persisted generation run preferences mapped to RunOptions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "generation_preferences.h"

#include <algorithm>

namespace videosynth::gui {

namespace {

constexpr const char* kThreadsKey = "generation/threads";
constexpr const char* kLogLevelKey = "generation/log_level";
constexpr const char* kLogToFileKey = "generation/log_to_file";
constexpr const char* kLogFilePathKey = "generation/log_file_path";

}  // namespace

bool operator==(const GenerationPreferences& a,
                const GenerationPreferences& b) {
  return a.threads == b.threads && a.log_level == b.log_level &&
         a.log_to_file == b.log_to_file && a.log_file_path == b.log_file_path;
}

QString SanitizedLogLevel(const QString& level) {
  if (level == QLatin1String("debug") || level == QLatin1String("trace") ||
      level == QLatin1String("info")) {
    return level;
  }
  return QStringLiteral("info");
}

GenerationPreferences LoadGenerationPreferences(const QSettings& settings) {
  GenerationPreferences preferences;
  preferences.threads =
      std::max(0, settings.value(QLatin1String(kThreadsKey), 0).toInt());
  preferences.log_level = SanitizedLogLevel(
      settings.value(QLatin1String(kLogLevelKey), QStringLiteral("info"))
          .toString());
  preferences.log_to_file =
      settings.value(QLatin1String(kLogToFileKey), false).toBool();
  preferences.log_file_path =
      settings.value(QLatin1String(kLogFilePathKey)).toString();
  return preferences;
}

void SaveGenerationPreferences(const GenerationPreferences& preferences,
                               QSettings* settings) {
  settings->setValue(QLatin1String(kThreadsKey), preferences.threads);
  settings->setValue(QLatin1String(kLogLevelKey),
                     SanitizedLogLevel(preferences.log_level));
  settings->setValue(QLatin1String(kLogToFileKey), preferences.log_to_file);
  settings->setValue(QLatin1String(kLogFilePathKey), preferences.log_file_path);
}

RunOptions MakeRunOptions(const GenerationPreferences& preferences,
                          bool validate_only) {
  RunOptions options;
  options.validate_only = validate_only;
  options.threads = std::max(0, preferences.threads);
  options.log_level = SanitizedLogLevel(preferences.log_level).toStdString();
  if (preferences.log_to_file && !preferences.log_file_path.isEmpty()) {
    options.log_file = preferences.log_file_path.toStdString();
  }
  return options;
}

}  // namespace videosynth::gui
