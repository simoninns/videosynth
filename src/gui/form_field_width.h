/*
 * File:        form_field_width.h
 * Module:      gui
 * Purpose:     Width capping helpers for numeric form entry fields
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <QFontMetrics>
#include <QString>
#include <QWidget>
#include <algorithm>

namespace videosynth::gui {

// Caps a form field at its own size hint so form and grid layouts stop
// stretching it across the editor panel. For spin boxes the hint already
// fits the widest in-range value plus any suffix or special-value text
// (e.g. "10000000 frames"), so this is the natural numeric-field width.
inline void CapFieldWidthAtSizeHint(QWidget* field) {
  field->setMaximumWidth(field->sizeHint().width());
}

// Fixes a form field at the width needed to render `sample` (plus frame
// chrome), for line edits whose sensible width is set by a sample or
// placeholder text rather than the widget's generic size hint. A fixed (not
// merely maximum) width keeps the field packed against its neighbours:
// layouts centre an under-maximum widget inside a stretched cell, which
// would float the field into the middle of the panel.
inline void CapFieldWidthToText(QWidget* field, const QString& sample) {
  // Allowance for the line-edit frame and text margins.
  constexpr int kFieldChromeWidth = 24;
  const int text_width =
      field->fontMetrics().horizontalAdvance(sample) + kFieldChromeWidth;
  field->setFixedWidth(std::max(text_width, field->minimumSizeHint().width()));
}

}  // namespace videosynth::gui
