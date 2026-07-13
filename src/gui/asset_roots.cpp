/*
 * File:        asset_roots.cpp
 * Module:      gui
 * Purpose:     Builds the GUI's logical asset-root map (bundled/user) using
 *              QStandardPaths so installed and Flatpak builds resolve assets.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "asset_roots.h"

#include <QStandardPaths>
#include <QString>

namespace videosynth::gui {

AssetRootMap GuiAssetRoots() {
  AssetRootMap map = DefaultAssetRoots();

  // Prefer an installed asset library found on the XDG data path
  // (~/.local/share, /usr/share, /app/share in Flatpak, $XDG_DATA_DIRS). Keep
  // the compile-time dev/submodule default when nothing is installed.
  const QString installed = QStandardPaths::locate(
      QStandardPaths::GenericDataLocation, QStringLiteral("videosynth/assets"),
      QStandardPaths::LocateDirectory);
  if (!installed.isEmpty()) {
    map.roots["bundled"] = installed.toStdString();
  }

  return map;
}

}  // namespace videosynth::gui
