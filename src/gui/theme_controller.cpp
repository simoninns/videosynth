/*
 * File:        theme_controller.cpp
 * Module:      gui
 * Purpose:     Applies and persists the application theme at runtime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "theme_controller.h"

#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>

namespace videosynth::gui {

ThemeController::ThemeController(QApplication* app, QObject* parent)
    : QObject(parent), app_(app) {}

void ThemeController::Initialize() {
  const QSettings settings;
  mode_ = ThemeManager::LoadMode(settings);
  Apply();
  UpdateSystemTracking();
}

void ThemeController::SetMode(ThemeManager::Mode mode) {
  QSettings settings;
  ThemeManager::SaveMode(&settings, mode);

  if (mode_ == mode) {
    return;
  }

  mode_ = mode;
  Apply();
  UpdateSystemTracking();
}

void ThemeController::Apply() {
  Qt::ColorScheme system_scheme = Qt::ColorScheme::Unknown;
  if (app_->styleHints() != nullptr) {
    system_scheme = app_->styleHints()->colorScheme();
  }

  const ThemeManager::Resolution resolution = ThemeManager::ResolveScheme(
      mode_, system_scheme, ThemeManager::IsPaletteDark(app_->palette()));

  is_dark_ = resolution.is_dark;
  ApplyPaletteAndStyleSheet(is_dark_);
  app_->setProperty("isDarkTheme", is_dark_);
  app_->setProperty("themeMode", ThemeManager::ModeToString(mode_));

  emit ThemeChanged(is_dark_);
}

void ThemeController::UpdateSystemTracking() {
  if (tracking_connection_) {
    QObject::disconnect(tracking_connection_);
    tracking_connection_ = QMetaObject::Connection();
  }

  if (!ThemeManager::ShouldTrackSystemChanges(mode_) ||
      app_->styleHints() == nullptr) {
    return;
  }

  tracking_connection_ =
      connect(app_->styleHints(), &QStyleHints::colorSchemeChanged, this,
              [this](Qt::ColorScheme) { Apply(); });
}

void ThemeController::ApplyPaletteAndStyleSheet(bool is_dark) {
  if (is_dark) {
    QPalette dark_palette;

    const QColor dark_gray(53, 53, 53);
    const QColor darkest_gray(25, 25, 25);
    const QColor disabled_gray(127, 127, 127);
    const QColor highlight_blue(42, 130, 218);

    dark_palette.setColor(QPalette::Window, dark_gray);
    dark_palette.setColor(QPalette::WindowText, Qt::white);
    dark_palette.setColor(QPalette::Base, darkest_gray);
    dark_palette.setColor(QPalette::AlternateBase, dark_gray);
    dark_palette.setColor(QPalette::ToolTipBase, dark_gray);
    dark_palette.setColor(QPalette::ToolTipText, Qt::white);
    dark_palette.setColor(QPalette::Text, Qt::white);
    dark_palette.setColor(QPalette::Button, dark_gray);
    dark_palette.setColor(QPalette::ButtonText, Qt::white);
    dark_palette.setColor(QPalette::BrightText, Qt::red);
    dark_palette.setColor(QPalette::Link, highlight_blue);
    dark_palette.setColor(QPalette::Highlight, highlight_blue);
    dark_palette.setColor(QPalette::HighlightedText, Qt::black);

    dark_palette.setColor(QPalette::Disabled, QPalette::WindowText,
                          disabled_gray);
    dark_palette.setColor(QPalette::Disabled, QPalette::Text, disabled_gray);
    dark_palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                          disabled_gray);
    dark_palette.setColor(QPalette::Disabled, QPalette::Highlight,
                          QColor(80, 80, 80));
    dark_palette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                          disabled_gray);

    app_->setPalette(dark_palette);
  } else {
    app_->setPalette(app_->style()->standardPalette());
  }

  const QString disabled_menu_text_color =
      is_dark ? QStringLiteral("rgb(127, 127, 127)")
              : QStringLiteral("palette(mid)");

  // Menus and message boxes do not fully restyle from the palette alone, so
  // the relevant roles are pinned through a stylesheet as well.
  app_->setStyleSheet(
      QStringLiteral(
          "QMenuBar { background-color: palette(window); color: "
          "palette(window-text); }"
          "QMenuBar::item:selected { background-color: palette(highlight); "
          "color: palette(highlighted-text); }"
          "QMenuBar::item:disabled { color: %1; }"
          "QMenu { background-color: palette(window); color: "
          "palette(window-text); }"
          "QMenu::item:selected { background-color: palette(highlight); "
          "color: palette(highlighted-text); }"
          "QMenu::item:disabled { color: %1; }"
          "QMessageBox { background-color: palette(window); color: "
          "palette(window-text); }"
          "QMessageBox QLabel { color: palette(window-text); }"
          "QMessageBox QPushButton { background-color: palette(button); "
          "color: palette(button-text); border: 1px solid palette(mid); "
          "padding: 4px 12px; border-radius: 3px; min-width: 60px; }"
          "QMessageBox QPushButton:hover { background-color: "
          "palette(highlight); color: palette(highlighted-text); }"
          "QMessageBox QPushButton:pressed { background-color: "
          "palette(dark); color: palette(button-text); }")
          .arg(disabled_menu_text_color));
}

}  // namespace videosynth::gui
