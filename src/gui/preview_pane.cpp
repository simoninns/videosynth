/*
 * File:        preview_pane.cpp
 * Module:      gui
 * Purpose:     Central preview tab: frame/field navigator, source and
 *              encoded picture views, and the line waveform scope
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "preview_pane.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedLayout>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "asset_roots.h"
#include "generation_controller.h"
#include "preview_render.h"
#include "videosynth/path_resolution.h"
#include "waveform_mapping.h"

namespace videosynth::gui {

namespace {

constexpr int kDefaultRefreshDebounceMsec = 400;

// Wraps a picture view in a resizable scroll area so fixed zooms can pan.
QScrollArea* WrapInScrollArea(PictureViewWidget* view, QWidget* parent) {
  auto* scroll = new QScrollArea(parent);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(view);
  return scroll;
}

}  // namespace

PreviewPane::PreviewPane(ProjectDocument* document,
                         ThemeController* theme_controller, QWidget* parent)
    : QWidget(parent),
      document_(document),
      theme_controller_(theme_controller),
      service_(new PreviewFrameService(
          {}, PreviewFrameService::kDefaultCacheCapacity, this)) {
  BuildUi();

  refresh_timer_.setSingleShot(true);
  refresh_timer_.setInterval(kDefaultRefreshDebounceMsec);
  connect(&refresh_timer_, &QTimer::timeout, this,
          &PreviewPane::OnRefreshTimeout);

  connect(document_, &ProjectDocument::DocumentChanged, this,
          &PreviewPane::OnDocumentChanged);
  connect(document_, &ProjectDocument::DocumentReset, this,
          &PreviewPane::OnDocumentChanged);

  connect(service_, &PreviewFrameService::ScheduleInfoChanged, this,
          &PreviewPane::OnScheduleInfoChanged);
  connect(service_, &PreviewFrameService::FrameReady, this,
          &PreviewPane::OnFrameReady);
  connect(service_, &PreviewFrameService::PreviewFailed, this,
          &PreviewPane::OnPreviewFailed);

  scope_->SetDarkTheme(theme_controller_->is_dark());
  connect(theme_controller_, &ThemeController::ThemeChanged, this,
          [this](bool is_dark) { scope_->SetDarkTheme(is_dark); });
}

void PreviewPane::SetDebounceInterval(int msec) {
  refresh_timer_.setInterval(msec);
}

void PreviewPane::BuildUi() {
  auto* layout = new QVBoxLayout(this);

  banner_label_ = new QLabel(this);
  banner_label_->setWordWrap(true);
  banner_label_->setVisible(false);
  banner_label_->setAutoFillBackground(true);
  banner_label_->setContentsMargins(6, 4, 6, 4);
  banner_label_->setFrameShape(QFrame::StyledPanel);
  layout->addWidget(banner_label_);

  // Navigator row.
  auto* navigator_layout = new QHBoxLayout();
  navigator_layout->addWidget(new QLabel(tr("Frame:"), this));
  frame_slider_ = new QSlider(Qt::Horizontal, this);
  frame_slider_->setRange(0, 0);
  navigator_layout->addWidget(frame_slider_, 1);
  frame_spinbox_ = new QSpinBox(this);
  frame_spinbox_->setRange(0, 0);
  navigator_layout->addWidget(frame_spinbox_);
  frame_total_label_ = new QLabel(tr("of 0"), this);
  navigator_layout->addWidget(frame_total_label_);
  field_combo_ = new QComboBox(this);
  field_combo_->addItem(tr("Field 1"));
  field_combo_->addItem(tr("Field 2"));
  navigator_layout->addWidget(field_combo_);
  noise_checkbox_ = new QCheckBox(tr("Noise"), this);
  navigator_layout->addWidget(noise_checkbox_);
  dropouts_checkbox_ = new QCheckBox(tr("Dropouts"), this);
  navigator_layout->addWidget(dropouts_checkbox_);
  layout->addLayout(navigator_layout);

  connect(frame_slider_, &QSlider::valueChanged, frame_spinbox_,
          &QSpinBox::setValue);
  connect(frame_spinbox_, &QSpinBox::valueChanged, frame_slider_,
          &QSlider::setValue);
  connect(frame_slider_, &QSlider::valueChanged, this,
          &PreviewPane::OnFrameIndexChanged);
  connect(field_combo_, &QComboBox::currentIndexChanged, this,
          &PreviewPane::OnFieldOrModeChanged);
  connect(noise_checkbox_, &QCheckBox::toggled, this,
          &PreviewPane::OnDegradationToggled);
  connect(dropouts_checkbox_, &QCheckBox::toggled, this,
          &PreviewPane::OnDegradationToggled);

  // Picture views.
  view_tabs_ = new QTabWidget(this);

  auto* source_page = new QWidget(view_tabs_);
  auto* source_layout = new QVBoxLayout(source_page);
  source_layout->setContentsMargins(0, 0, 0, 0);
  source_view_ = new PictureViewWidget(source_page);
  source_placeholder_ =
      new QLabel(tr("No decoded source frame available."), source_page);
  source_placeholder_->setAlignment(Qt::AlignCenter);
  auto* source_stack = new QStackedLayout();
  source_stack->addWidget(source_view_);
  source_stack->addWidget(source_placeholder_);
  source_layout->addLayout(source_stack);
  view_tabs_->addTab(source_page, tr("Source"));

  auto* encoded_page = new QWidget(view_tabs_);
  auto* encoded_layout = new QVBoxLayout(encoded_page);
  encoded_layout->setContentsMargins(0, 0, 0, 0);
  auto* encoded_controls = new QHBoxLayout();
  encoded_controls->addWidget(new QLabel(tr("Mode:"), encoded_page));
  encoded_mode_combo_ = new QComboBox(encoded_page);
  encoded_mode_combo_->addItem(tr("Composite"));
  encoded_mode_combo_->addItem(tr("Y/C"));
  encoded_controls->addWidget(encoded_mode_combo_);
  encoded_controls->addWidget(new QLabel(tr("Zoom:"), encoded_page));
  zoom_combo_ = new QComboBox(encoded_page);
  zoom_combo_->addItem(tr("Fit"));
  zoom_combo_->addItem(tr("100%"));
  zoom_combo_->addItem(tr("200%"));
  encoded_controls->addWidget(zoom_combo_);
  encoded_controls->addStretch();
  encoded_layout->addLayout(encoded_controls);

  encoded_splitter_ = new QSplitter(Qt::Vertical, encoded_page);
  encoded_primary_view_ = new PictureViewWidget(encoded_splitter_);
  encoded_secondary_view_ = new PictureViewWidget(encoded_splitter_);
  encoded_splitter_->addWidget(
      WrapInScrollArea(encoded_primary_view_, encoded_splitter_));
  encoded_splitter_->addWidget(
      WrapInScrollArea(encoded_secondary_view_, encoded_splitter_));
  encoded_layout->addWidget(encoded_splitter_, 1);
  view_tabs_->addTab(encoded_page, tr("Encoded"));

  layout->addWidget(view_tabs_, 1);

  connect(encoded_mode_combo_, &QComboBox::currentIndexChanged, this,
          &PreviewPane::OnFieldOrModeChanged);
  connect(
      zoom_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto mode = index == 1 ? PictureViewWidget::ZoomMode::k100Percent
                          : index == 2
                              ? PictureViewWidget::ZoomMode::k200Percent
                              : PictureViewWidget::ZoomMode::kFit;
        encoded_primary_view_->SetZoomMode(mode);
        encoded_secondary_view_->SetZoomMode(mode);
      });
  connect(encoded_primary_view_, &PictureViewWidget::RowClicked, this,
          &PreviewPane::OnEncodedRowClicked);
  connect(encoded_secondary_view_, &PictureViewWidget::RowClicked, this,
          &PreviewPane::OnEncodedRowClicked);

  frame_info_label_ = new QLabel(this);
  layout->addWidget(frame_info_label_);

  // Line waveform scope.
  auto* scope_group = new QGroupBox(tr("Line Waveform"), this);
  auto* scope_layout = new QVBoxLayout(scope_group);
  auto* scope_controls = new QHBoxLayout();
  scope_controls->addWidget(new QLabel(tr("Line:"), scope_group));
  line_spinbox_ = new QSpinBox(scope_group);
  line_spinbox_->setRange(1, 1);
  scope_controls->addWidget(line_spinbox_);
  scope_controls->addWidget(new QLabel(tr("Trace:"), scope_group));
  trace_combo_ = new QComboBox(scope_group);
  trace_combo_->addItem(tr("Composite"));
  trace_combo_->addItem(tr("Y"));
  trace_combo_->addItem(tr("C"));
  trace_combo_->addItem(tr("Y+C overlay"));
  scope_controls->addWidget(trace_combo_);
  readout_label_ = new QLabel(scope_group);
  scope_controls->addWidget(readout_label_, 1);
  scope_layout->addLayout(scope_controls);
  scope_ = new WaveformScopeWidget(scope_group);
  scope_layout->addWidget(scope_);
  layout->addWidget(scope_group);

  connect(line_spinbox_, &QSpinBox::valueChanged, this,
          &PreviewPane::OnScopeLineChanged);
  connect(
      trace_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        switch (index) {
          case 1:
            scope_->SetTraceMode(WaveformScopeWidget::TraceMode::kLuma);
            break;
          case 2:
            scope_->SetTraceMode(WaveformScopeWidget::TraceMode::kChroma);
            break;
          case 3:
            scope_->SetTraceMode(WaveformScopeWidget::TraceMode::kOverlay);
            break;
          default:
            scope_->SetTraceMode(WaveformScopeWidget::TraceMode::kComposite);
            break;
        }
      });
  connect(scope_, &WaveformScopeWidget::CursorMoved, this,
          &PreviewPane::OnCursorMoved);
}

void PreviewPane::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (needs_refresh_) {
    refresh_timer_.start();
  }
}

void PreviewPane::OnDocumentChanged() {
  needs_refresh_ = true;
  if (current_frame_ != nullptr) {
    ShowBanner(tr("Preview is out of date — updating…"));
  }
  if (isVisible()) {
    refresh_timer_.start();
  }
}

void PreviewPane::OnRefreshTimeout() { PushProjectToService(); }

void PreviewPane::PushProjectToService() {
  needs_refresh_ = false;

  // Relative paths are anchored to the project file's directory because
  // synthesis executes on a worker thread (matches OnGenerate).
  const QString base_dir =
      document_->file_path().isEmpty()
          ? QDir::currentPath()
          : QFileInfo(document_->file_path()).absolutePath();
  service_->SetProject(videosynth::ResolveProjectPaths(
      document_->project(), GuiAssetRoots(), base_dir.toStdString(),
      /*anchor_unset=*/true));
  RequestCurrentFrame();
}

void PreviewPane::RequestCurrentFrame() {
  service_->RequestFrame(static_cast<std::size_t>(frame_slider_->value()),
                         CurrentOptions());
}

PreviewOptions PreviewPane::CurrentOptions() const {
  PreviewOptions options;
  options.apply_noise = noise_checkbox_->isChecked();
  options.apply_dropouts = dropouts_checkbox_->isChecked();
  return options;
}

void PreviewPane::OnScheduleInfoChanged() {
  const std::optional<PreviewScheduleInfo>& info = service_->schedule_info();
  if (!info.has_value()) {
    return;
  }

  // No signal blocking: when the new range clamps the current value, the
  // resulting valueChanged re-requests the (now valid) frame.
  const int last_frame =
      std::max(0, static_cast<int>(info->output_frame_count) - 1);
  frame_slider_->setRange(0, last_frame);
  frame_spinbox_->setRange(0, last_frame);
  frame_total_label_->setText(tr("of %1").arg(info->output_frame_count));
  line_spinbox_->setRange(1, std::max(1, info->lines_per_frame));

  // Honour the project's signal_type as the default encoded mode while
  // preserving a manual override across unrelated edits.
  if (applied_signal_type_ != info->signal_type) {
    applied_signal_type_ = info->signal_type;
    encoded_mode_combo_->setCurrentIndex(info->signal_type == "yc" ? 1 : 0);
  }

  if (pending_section_jump_ >= 0) {
    const int section_index = pending_section_jump_;
    pending_section_jump_ = -1;
    ShowSectionFirstFrame(section_index);
  }
}

void PreviewPane::OnFrameReady(std::shared_ptr<const PreviewFrameData> frame) {
  if (frame == nullptr || frame->revision != service_->revision()) {
    return;
  }
  current_frame_ = std::move(frame);
  banner_label_->setVisible(false);
  UpdatePictures();
  UpdateScope();
  UpdateFrameInfoLabel();
}

void PreviewPane::OnPreviewFailed(quint64 revision, const QString& message) {
  if (revision != service_->revision()) {
    return;
  }
  if (current_frame_ != nullptr) {
    ShowBanner(tr("%1 — showing the last good frame.").arg(message));
  } else {
    ShowBanner(tr("Preview unavailable: %1").arg(message));
  }
}

void PreviewPane::OnFrameIndexChanged(int output_frame_index) {
  Q_UNUSED(output_frame_index);
  RequestCurrentFrame();
}

void PreviewPane::OnFieldOrModeChanged() {
  // Field and composite/Y-C switches re-render from the cached buffers; no
  // synthesis is re-run.
  UpdatePictures();
}

void PreviewPane::OnDegradationToggled() { RequestCurrentFrame(); }

void PreviewPane::OnEncodedRowClicked(int row) {
  const std::optional<PreviewScheduleInfo>& info = service_->schedule_info();
  if (!info.has_value() || info->standard == Standard::kUnknown) {
    return;
  }
  const int field = field_combo_->currentIndex() + 1;
  line_spinbox_->setValue(PictureRowToLineNumber(info->standard, field, row));
}

void PreviewPane::OnScopeLineChanged(int line_number) {
  Q_UNUSED(line_number);
  UpdateScope();
}

void PreviewPane::OnCursorMoved(int sample_index, double microseconds,
                                double millivolts) {
  const std::optional<PreviewScheduleInfo>& info = service_->schedule_info();
  QString text = tr("Sample %1 · %2 µs · %3 mV")
                     .arg(sample_index)
                     .arg(microseconds, 0, 'f', 2)
                     .arg(millivolts, 0, 'f', 1);
  // IRE applies to the System M standards (NTSC, PAL-M).
  if (info.has_value() && info->standard != Standard::kPal) {
    text += tr(" · %1 IRE").arg(MillivoltsToIre(millivolts), 0, 'f', 1);
  }
  readout_label_->setText(text);
}

void PreviewPane::UpdatePictures() {
  if (current_frame_ == nullptr) {
    return;
  }
  const std::optional<PreviewScheduleInfo>& info = service_->schedule_info();
  if (!info.has_value() || info->standard == Standard::kUnknown) {
    return;
  }

  // Source view.
  if (current_frame_->has_source_image) {
    const QImage source = RenderSourceImage(current_frame_->source_image);
    source_view_->SetImage(source, false);
    source_view_->setVisible(!source.isNull());
    source_placeholder_->setVisible(source.isNull());
  } else {
    source_view_->SetImage(QImage(), false);
    source_view_->setVisible(false);
    source_placeholder_->setVisible(true);
  }

  // Encoded views.
  const int field = field_combo_->currentIndex() + 1;
  const bool yc_mode = encoded_mode_combo_->currentIndex() == 1;
  if (yc_mode) {
    encoded_primary_view_->SetImage(
        RenderEncodedFieldImage(current_frame_->y_mv, current_frame_->c_mv,
                                info->standard, field, EncodedImageMode::kLuma),
        true);
    encoded_secondary_view_->SetImage(
        RenderEncodedFieldImage(current_frame_->y_mv, current_frame_->c_mv,
                                info->standard, field,
                                EncodedImageMode::kChroma),
        true);
  } else {
    encoded_primary_view_->SetImage(
        RenderEncodedFieldImage(current_frame_->y_mv, current_frame_->c_mv,
                                info->standard, field,
                                EncodedImageMode::kComposite),
        true);
  }
  encoded_splitter_->widget(1)->setVisible(yc_mode);
}

void PreviewPane::UpdateScope() {
  if (current_frame_ == nullptr) {
    return;
  }
  const std::optional<PreviewScheduleInfo>& info = service_->schedule_info();
  if (!info.has_value() || info->standard == Standard::kUnknown) {
    return;
  }

  const int line = line_spinbox_->value();
  scope_->SetLineData(
      ExtractLineMillivolts(current_frame_->y_mv, info->standard, line),
      ExtractLineMillivolts(current_frame_->c_mv, info->standard, line),
      info->sample_rate_hz, info->levels);
}

void PreviewPane::UpdateFrameInfoLabel() {
  if (current_frame_ == nullptr) {
    frame_info_label_->clear();
    return;
  }
  QString text = tr("Output frame %1 · disc frame %2")
                     .arg(current_frame_->output_frame_index)
                     .arg(current_frame_->disc_frame_index);
  if (!current_frame_->section_name.isEmpty()) {
    text += tr(" · section \"%1\"").arg(current_frame_->section_name);
  }
  frame_info_label_->setText(text);
}

void PreviewPane::ShowSectionFirstFrame(int section_index) {
  const std::optional<PreviewScheduleInfo>& info = service_->schedule_info();
  if (!info.has_value()) {
    pending_section_jump_ = section_index;
    if (needs_refresh_) {
      refresh_timer_.start();
    } else {
      RequestCurrentFrame();
    }
    return;
  }

  if (section_index < 0 || static_cast<std::size_t>(section_index) >=
                               info->section_first_output_frame.size()) {
    return;
  }
  const qint64 first_frame =
      info->section_first_output_frame[static_cast<std::size_t>(section_index)];
  if (first_frame < 0) {
    ShowBanner(tr("The selected section contributes no output frames."));
    return;
  }
  if (frame_slider_->value() == static_cast<int>(first_frame)) {
    RequestCurrentFrame();
  } else {
    frame_slider_->setValue(static_cast<int>(first_frame));
  }
}

void PreviewPane::ShowBanner(const QString& message) {
  banner_label_->setText(message);
  banner_label_->setVisible(true);
}

}  // namespace videosynth::gui
