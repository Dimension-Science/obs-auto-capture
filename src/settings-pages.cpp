#include "settings-pages.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <sstream>
#include <string>

namespace {

const char *Text(const char *key)
{
  return obs_module_text(key);
}

// libobs packs colours as 0xAABBGGRR: red in the low byte, the opposite of the
// order QColor uses.
QColor FromObsColor(unsigned int value)
{
  return QColor(static_cast<int>(value & 0xFF), static_cast<int>((value >> 8) & 0xFF),
                static_cast<int>((value >> 16) & 0xFF));
}

unsigned int ToObsColor(const QColor &color)
{
  return 0xFF000000u | (static_cast<unsigned int>(color.blue()) << 16) |
         (static_cast<unsigned int>(color.green()) << 8) | static_cast<unsigned int>(color.red());
}

QLabel *MakeHint(QWidget *parent, const char *key)
{
  auto *hint = new QLabel(Text(key), parent);
  hint->setWordWrap(true);
  hint->setEnabled(false);
  return hint;
}

} // namespace

// ---------------------------------------------------------------------------
// Mirroring
// ---------------------------------------------------------------------------

MirrorPage::MirrorPage(QWidget *parent) : QWidget(parent)
{
  enabled_ = new QCheckBox(Text("AutoAppCapture.Mirror.Enabled"), this);

  input_ = new QLineEdit(this);
  input_->setPlaceholderText(Text("AutoAppCapture.Window.Mirror.Placeholder"));
  add_ = new QPushButton(Text("AutoAppCapture.Window.Mirror.Add"), this);
  remove_ = new QPushButton(Text("AutoAppCapture.Window.Mirror.Remove"), this);
  words_ = new QListWidget(this);

  connect(add_, &QPushButton::clicked, this, [this] { AddWord(); });
  connect(input_, &QLineEdit::returnPressed, this, [this] { AddWord(); });
  connect(remove_, &QPushButton::clicked, this, [this] { RemoveWord(); });

  auto *input_row = new QHBoxLayout();
  input_row->addWidget(input_, 1);
  input_row->addWidget(add_);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(enabled_);
  layout->addWidget(MakeHint(this, "AutoAppCapture.Mirror.Titles.Tooltip"));
  layout->addWidget(new QLabel(Text("AutoAppCapture.Mirror.Titles"), this));
  layout->addLayout(input_row);
  layout->addWidget(words_, 1);
  layout->addWidget(remove_, 0, Qt::AlignLeft);
}

void MirrorPage::AddWord()
{
  const QString word = input_->text().trimmed();
  if (word.isEmpty()) {
    return;
  }
  for (int i = 0; i < words_->count(); ++i) {
    if (words_->item(i)->text().compare(word, Qt::CaseInsensitive) == 0) {
      input_->clear();
      return;
    }
  }
  words_->addItem(word);
  input_->clear();
}

void MirrorPage::RemoveWord()
{
  delete words_->takeItem(words_->currentRow());
}

void MirrorPage::Load(obs_data_t *settings)
{
  enabled_->setChecked(obs_data_get_bool(settings, "mirror_enabled"));

  words_->clear();
  std::istringstream stream(obs_data_get_string(settings, "mirror_title_rules"));
  std::string line;
  while (std::getline(stream, line)) {
    const QString word = QString::fromStdString(line).trimmed();
    if (!word.isEmpty()) {
      words_->addItem(word);
    }
  }
}

void MirrorPage::Save(obs_data_t *settings) const
{
  obs_data_set_bool(settings, "mirror_enabled", enabled_->isChecked());

  // Stored as one word per line, the format the source already parses.
  QStringList words;
  for (int i = 0; i < words_->count(); ++i) {
    words << words_->item(i)->text();
  }
  obs_data_set_string(settings, "mirror_title_rules", words.join('\n').toUtf8().constData());
}

// ---------------------------------------------------------------------------
// Address bar blur
// ---------------------------------------------------------------------------

BlurPage::BlurPage(QWidget *parent) : QWidget(parent)
{
  enabled_ = new QCheckBox(Text("AutoAppCapture.Blur.Enabled"), this);
  connect(enabled_, &QCheckBox::toggled, this, [this] { UpdateEnabledState(); });

  detect_ = new QComboBox(this);
  detect_->addItem(Text("AutoAppCapture.Blur.Detect.Auto"), "auto");
  detect_->addItem(Text("AutoAppCapture.Blur.Detect.Manual"), "manual");
  detect_->setToolTip(Text("AutoAppCapture.Blur.Detect.Tooltip"));

  mode_ = new QComboBox(this);
  mode_->addItem(Text("AutoAppCapture.Blur.Mode.Frosted"), "frosted");
  mode_->addItem(Text("AutoAppCapture.Blur.Mode.Mosaic"), "mosaic");
  mode_->addItem(Text("AutoAppCapture.Blur.Mode.Fill"), "fill");
  connect(mode_, &QComboBox::currentIndexChanged, this, [this] { UpdateEnabledState(); });

  strength_ = new QSpinBox(this);
  strength_->setRange(4, 80);
  strength_->setSuffix(" px");
  strength_->setToolTip(Text("AutoAppCapture.Blur.Strength.Tooltip"));

  padding_ = new QSpinBox(this);
  padding_->setRange(0, 40);
  padding_->setSuffix(" px");
  padding_->setToolTip(Text("AutoAppCapture.Blur.Padding.Tooltip"));

  color_ = new QPushButton(this);
  connect(color_, &QPushButton::clicked, this, [this] { PickColor(); });

  const auto make_percent = [this] {
    auto *box = new QDoubleSpinBox(this);
    box->setRange(0.0, 100.0);
    box->setSingleStep(0.5);
    box->setDecimals(1);
    box->setSuffix(" %");
    return box;
  };
  zone_left_ = make_percent();
  zone_top_ = make_percent();
  zone_width_ = make_percent();
  zone_height_ = make_percent();

  auto *form = new QFormLayout();
  form->addRow(Text("AutoAppCapture.Blur.Detect"), detect_);
  form->addRow(Text("AutoAppCapture.Blur.Padding"), padding_);
  form->addRow(Text("AutoAppCapture.Blur.Mode"), mode_);
  form->addRow(Text("AutoAppCapture.Blur.Strength"), strength_);
  form->addRow(Text("AutoAppCapture.Blur.FillColor"), color_);
  form->addRow(Text("AutoAppCapture.Blur.Zone.Left"), zone_left_);
  form->addRow(Text("AutoAppCapture.Blur.Zone.Top"), zone_top_);
  form->addRow(Text("AutoAppCapture.Blur.Zone.Width"), zone_width_);
  form->addRow(Text("AutoAppCapture.Blur.Zone.Height"), zone_height_);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(enabled_);
  layout->addWidget(MakeHint(this, "AutoAppCapture.Blur.Hint"));
  layout->addLayout(form);
  layout->addStretch(1);
}

void BlurPage::UpdateEnabledState()
{
  const bool on = enabled_->isChecked();
  const bool fill = mode_->currentIndex() == 2;
  for (QWidget *widget : {static_cast<QWidget *>(detect_), static_cast<QWidget *>(mode_),
                          static_cast<QWidget *>(padding_), static_cast<QWidget *>(zone_left_),
                          static_cast<QWidget *>(zone_top_), static_cast<QWidget *>(zone_width_),
                          static_cast<QWidget *>(zone_height_)}) {
    widget->setEnabled(on);
  }
  // Cell size means nothing for a solid fill, and the colour means nothing for
  // the other two modes.
  strength_->setEnabled(on && !fill);
  color_->setEnabled(on && fill);
}

void BlurPage::PickColor()
{
  const QColor picked = QColorDialog::getColor(FromObsColor(fill_color_), this);
  if (!picked.isValid()) {
    return;
  }
  fill_color_ = ToObsColor(picked);
  color_->setText(picked.name().toUpper());
}

void BlurPage::Load(obs_data_t *settings)
{
  enabled_->setChecked(obs_data_get_bool(settings, "blur_enabled"));
  detect_->setCurrentIndex(std::string(obs_data_get_string(settings, "blur_detect")) == "manual" ? 1 : 0);

  const std::string mode = obs_data_get_string(settings, "blur_mode");
  mode_->setCurrentIndex(mode == "mosaic" ? 1 : (mode == "fill" ? 2 : 0));

  strength_->setValue(static_cast<int>(obs_data_get_int(settings, "blur_strength")));
  padding_->setValue(static_cast<int>(obs_data_get_int(settings, "blur_padding")));

  fill_color_ = static_cast<unsigned int>(obs_data_get_int(settings, "blur_fill_color"));
  color_->setText(FromObsColor(fill_color_).name().toUpper());

  zone_left_->setValue(obs_data_get_double(settings, "blur_zone_left"));
  zone_top_->setValue(obs_data_get_double(settings, "blur_zone_top"));
  zone_width_->setValue(obs_data_get_double(settings, "blur_zone_width"));
  zone_height_->setValue(obs_data_get_double(settings, "blur_zone_height"));

  UpdateEnabledState();
}

void BlurPage::Save(obs_data_t *settings) const
{
  obs_data_set_bool(settings, "blur_enabled", enabled_->isChecked());
  obs_data_set_string(settings, "blur_detect", detect_->currentData().toString().toUtf8().constData());
  obs_data_set_string(settings, "blur_mode", mode_->currentData().toString().toUtf8().constData());
  obs_data_set_int(settings, "blur_strength", strength_->value());
  obs_data_set_int(settings, "blur_padding", padding_->value());
  obs_data_set_int(settings, "blur_fill_color", fill_color_);
  obs_data_set_double(settings, "blur_zone_left", zone_left_->value());
  obs_data_set_double(settings, "blur_zone_top", zone_top_->value());
  obs_data_set_double(settings, "blur_zone_width", zone_width_->value());
  obs_data_set_double(settings, "blur_zone_height", zone_height_->value());
}

