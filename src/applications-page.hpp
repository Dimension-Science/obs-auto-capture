#pragma once

#include "plugin-api.hpp"

#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
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

  void SetRules(std::vector<AutoCaptureRule> rules);
  const std::vector<AutoCaptureRule> &Rules() const { return rules_; }

  void RefreshRunningApps();

private:
  void BuildUi();
  void ReloadTrackedList();
  void ShowSelectedRule();
  void CommitEditorToRule();
  void AddSelectedApp();
  void RemoveSelectedRule();
  int SelectedRuleIndex() const;

  std::vector<AutoCaptureRule> rules_;
  std::vector<RunningApp> running_;
  // Set while the editor widgets are being filled, so their change signals do
  // not write the value straight back into the rule they came from.
  bool loading_editor_ = false;

  QLineEdit *search_ = nullptr;
  QListWidget *available_ = nullptr;
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
};
