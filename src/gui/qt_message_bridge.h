/*
 * File:        qt_message_bridge.h
 * Module:      gui
 * Purpose:     Routes Qt's own log messages into the application logger and
 *              suppresses known Wayland/XCB platform noise.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QString>
#include <QtGlobal>

#include "videosynth/interfaces.h"

namespace videosynth::gui {

// Returns true when a Qt log message is known UI-backend noise that should be
// dropped rather than forwarded to the application log (e.g. qt.qpa.* platform
// chatter, the Wayland popup mouse-grab notice). Fatal messages are never
// suppressed because they precede an abort.
bool ShouldSuppressQtMessage(QtMsgType type, const char* category,
                             const QString& message);

// Forwards a single Qt message to `logger`, mapping the Qt severity to the
// matching ILogger method and prefixing a meaningful category when present.
void LogQtMessage(ILogger& logger, QtMsgType type, const char* category,
                  const QString& message);

// Installs a process-wide Qt message handler that drops known UI-backend noise
// (see ShouldSuppressQtMessage) and routes every other message to `logger`.
// Pass nullptr to detach the bridge and restore Qt's default handler (e.g.
// before the logger is destroyed).
//
// Thread-safety: the installed handler may fire from any thread; `logger` must
// be thread-safe and must outlive the bridge. Only the pointer is stored.
void InstallQtMessageBridge(ILogger* logger);

}  // namespace videosynth::gui
