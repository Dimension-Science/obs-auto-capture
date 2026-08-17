#pragma once

#include "plugin-api.hpp"

#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QPushButton;

// "Which applications to capture": running programs on the left, tracked ones
// on the right, and the editor for the selected rule underneath.
//
// The page owns a working copy of the rule list. Nothing reaches the source
// until the window applies it, so a half typed rule never goes on stream.
class ApplicationsPage : public QWidget {
public:
  explicit ApplicationsPage(QWidget *parent);

  void Load(obs_data_t *settings);
  void Save(obs_data_t *settings) const;

private:
  void BuildUi();
  void ReloadTrackedList();
  void ShowSelectedRule();
  void CommitEditorToRule();
  void AddApplication();
  void RemoveSelectedRule();
  int SelectedRuleIndex() const;

  std::vector<AutoCaptureRule> rules_;
  // Set while the editor widgets are being filled, so their change signals do
  // not write the value straight back into the rule they came from.
  bool loading_editor_ = false;

  QListWidget *tracked_ = nullptr;
  QPushButton *add_ = nullptr;
  QPushButton *remove_ = nullptr;

  QGroupBox *editor_ = nullptr;
  QCheckBox *enabled_ = nullptr;
  QLineEdit *name_ = nullptr;
  QLineEdit *process_ = nullptr;
  QComboBox *mode_ = nullptr;
  QComboBox *scope_ = nullptr;
  QLineEdit *title_ = nullptr;
  QCheckBox *blur_ = nullptr;

  // How often the foreground window is checked. It belongs with the list of
  // applications it switches between, not in a section of its own.
  QDoubleSpinBox *poll_interval_ = nullptr;
};
