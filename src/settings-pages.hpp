#pragma once

#include <obs.h>

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

// The remaining sections of the settings window. Unlike the rule list these are
// plain settings, so each page reads the working copy on open and writes it
// back when the window applies. Nothing here talks to the source directly.

class MirrorPage : public QWidget {
public:
  explicit MirrorPage(QWidget *parent);

  void Load(obs_data_t *settings);
  void Save(obs_data_t *settings) const;

private:
  void AddWord();
  void RemoveWord();

  QCheckBox *enabled_ = nullptr;
  QLineEdit *input_ = nullptr;
  QListWidget *words_ = nullptr;
  QPushButton *add_ = nullptr;
  QPushButton *remove_ = nullptr;
};

class BlurPage : public QWidget {
public:
  explicit BlurPage(QWidget *parent);

  void Load(obs_data_t *settings);
  void Save(obs_data_t *settings) const;

private:
  void UpdateEnabledState();
  void PickColor();

  QCheckBox *enabled_ = nullptr;
  QComboBox *detect_ = nullptr;
  QComboBox *mode_ = nullptr;
  QSpinBox *strength_ = nullptr;
  QSpinBox *padding_ = nullptr;
  QPushButton *color_ = nullptr;
  QDoubleSpinBox *zone_left_ = nullptr;
  QDoubleSpinBox *zone_top_ = nullptr;
  QDoubleSpinBox *zone_width_ = nullptr;
  QDoubleSpinBox *zone_height_ = nullptr;
  unsigned int fill_color_ = 0xFF101010;
};

class AdvancedPage : public QWidget {
public:
  explicit AdvancedPage(QWidget *parent);

  void Load(obs_data_t *settings);
  void Save(obs_data_t *settings) const;

private:
  QDoubleSpinBox *poll_interval_ = nullptr;
};
