/*
 * File:        asset_roots.h
 * Module:      gui
 * Purpose:     Builds the GUI's logical asset-root map (bundled/user) using
 *              QStandardPaths so installed and Flatpak builds resolve assets.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/path_resolution.h"

namespace videosynth::gui {

// Returns the asset-root map for the GUI: the core DefaultAssetRoots() with the
// "bundled" root overridden to an installed `videosynth/assets` directory when
// QStandardPaths finds one on the XDG data path (e.g. /app/share, /usr/share);
// otherwise the compile-time dev/submodule default is kept. The "user" root is
// the core default ($XDG_DATA_HOME/videosynth/assets), matching the CLI.
//
// Thread-safety: call from the GUI thread (QStandardPaths reads app metadata).
AssetRootMap GuiAssetRoots();

}  // namespace videosynth::gui
