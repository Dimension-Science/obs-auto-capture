#include "applications-page.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

const char *Text(const char *key)
{
  return obs_module_text(key);
}

QString RuleLabel(const AutoCaptureRule &rule)
{
  QString label = QString::fromStdString(rule.display_name);
  if (label.isEmpty()) {
    label = QString::fromStdString(rule.process_name);
  }
  return QString("%1  (%2)").arg(label, QString::fromStdString(rule.process_name));
}

QString AppLabel(const RunningApp &app)
{
  QString label = QString::fromStdString(app.process_name);
  if (!app.sample_title.empty()) {
    label += QString("  —  %1").arg(QString::fromStdString(app.sample_title));
  }
  if (app.window_count > 1) {
    label += QString("  (+%1 %2)").arg(app.window_count - 1).arg(Text("AutoAppCapture.Add.MoreWindows"));
  }
  return label;
}

} // namespace

ApplicationsPage::ApplicationsPage(QWidget *parent) : QWidget(parent)
{
  BuildUi();
  RefreshRunningApps();
}

void ApplicationsPage::BuildUi()
{
  search_ = new QLineEdit(this);
  search_->setPlaceholderText(Text("AutoAppCapture.Window.Search"));
  connect(search_, &QLineEdit::textChanged, this, [this] { RefreshRunningApps(); });

  available_ = new QListWidget(this);
  tracked_ = new QListWidget(this);

  add_ = new QPushButton("→", this);
  remove_ = new QPushButton("←", this);
  add_->setFixedWidth(44);
  remove_->setFixedWidth(44);
  add_->setToolTip(Text("AutoAppCapture.Window.Add.Tooltip"));
  remove_->setToolTip(Text("AutoAppCapture.Window.Remove.Tooltip"));

  connect(add_, &QPushButton::clicked, this, [this] { AddSelectedApp(); });
  connect(remove_, &QPushButton::clicked, this, [this] { RemoveSelectedRule(); });
  connect(available_, &QListWidget::itemDoubleClicked, this, [this] { AddSelectedApp(); });
  connect(tracked_, &QListWidget::currentRowChanged, this, [this] { ShowSelectedRule(); });

  // The checkbox in the list is the same "track this application" flag as in
  // the editor, so toggling it there has to write through as well.
  connect(tracked_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
    if (loading_editor_) {
      return;
    }
    const int row = tracked_->row(item);
    if (row >= 0 && row < static_cast<int>(rules_.size())) {
      rules_[static_cast<size_t>(row)].enabled = item->checkState() == Qt::Checked;
      if (row == tracked_->currentRow()) {
        ShowSelectedRule();
      }
    }
  });

  auto *middle = new QVBoxLayout();
  middle->addStretch(1);
  middle->addWidget(add_);
  middle->addWidget(remove_);
  middle->addStretch(1);

  auto *available_side = new QVBoxLayout();
  available_side->addWidget(new QLabel(Text("AutoAppCapture.Window.Available"), this));
  available_side->addWidget(search_);
  available_side->addWidget(available_, 1);

  auto *tracked_side = new QVBoxLayout();
  tracked_side->addWidget(new QLabel(Text("AutoAppCapture.Window.Tracked"), this));
  tracked_side->addWidget(tracked_, 1);

  auto *lists = new QHBoxLayout();
  lists->addLayout(available_side, 1);
  lists->addLayout(middle);
  lists->addLayout(tracked_side, 1);

  enabled_ = new QCheckBox(Text("AutoAppCapture.Rule.Enabled"), this);
  name_ = new QLineEdit(this);
  process_ = new QLineEdit(this);
  process_->setReadOnly(true);
  mode_ = new QComboBox(this);
  mode_->addItem(Text("AutoAppCapture.Rule.Mode.Auto"), "auto");
  mode_->addItem(Text("AutoAppCapture.Rule.Mode.Window"), "window");
  mode_->addItem(Text("AutoAppCapture.Rule.Mode.Game"), "game");
  mode_->setToolTip(Text("AutoAppCapture.Rule.Mode.Tooltip"));
  scope_ = new QComboBox(this);
  scope_->addItem(Text("AutoAppCapture.Rule.Scope.Any"), "any");
  scope_->addItem(Text("AutoAppCapture.Rule.Scope.Fullscreen"), "fullscreen");
  title_ = new QLineEdit(this);
  title_->setToolTip(Text("AutoAppCapture.Rule.Title.Tooltip"));
  blur_ = new QCheckBox(Text("AutoAppCapture.Rule.Blur"), this);
  blur_->setToolTip(Text("AutoAppCapture.Rule.Blur.Tooltip"));

  auto *form = new QFormLayout();
  form->addRow(enabled_);
  form->addRow(Text("AutoAppCapture.Rule.Name"), name_);
  form->addRow(Text("AutoAppCapture.Rule.Process"), process_);
  form->addRow(Text("AutoAppCapture.Rule.Mode"), mode_);
  form->addRow(Text("AutoAppCapture.Rule.Scope"), scope_);
  form->addRow(Text("AutoAppCapture.Rule.Title"), title_);
  form->addRow(blur_);

  editor_ = new QGroupBox(Text("AutoAppCapture.Window.RuleEditor"), this);
  editor_->setLayout(form);

  const auto commit = [this] { CommitEditorToRule(); };
  connect(enabled_, &QCheckBox::toggled, this, commit);
  connect(blur_, &QCheckBox::toggled, this, commit);
  connect(name_, &QLineEdit::textEdited, this, commit);
  connect(title_, &QLineEdit::textEdited, this, commit);
  connect(mode_, &QComboBox::currentIndexChanged, this, commit);
  connect(scope_, &QComboBox::currentIndexChanged, this, commit);

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(lists, 1);
  layout->addWidget(editor_);
}

void ApplicationsPage::RefreshRunningApps()
{
  running_ = CollectRunningApps();

  const QString filter = search_ != nullptr ? search_->text().trimmed().toLower() : QString();
  available_->clear();
  for (const RunningApp &app : running_) {
    const QString label = AppLabel(app);
    if (!filter.isEmpty() && !label.toLower().contains(filter)) {
      continue;
    }
    auto *item = new QListWidgetItem(label, available_);
    item->setData(Qt::UserRole, QString::fromStdString(app.process_name));
  }
}

void ApplicationsPage::SetRules(std::vector<AutoCaptureRule> rules)
{
  rules_ = std::move(rules);
  ReloadTrackedList();
}

void ApplicationsPage::ReloadTrackedList()
{
  const int previous = tracked_->currentRow();

  loading_editor_ = true;
  tracked_->clear();
  for (const AutoCaptureRule &rule : rules_) {
    auto *item = new QListWidgetItem(RuleLabel(rule), tracked_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
  }
  loading_editor_ = false;

  if (!rules_.empty()) {
    const int row = std::clamp(previous, 0, static_cast<int>(rules_.size()) - 1);
    tracked_->setCurrentRow(row);
  }
  ShowSelectedRule();
}

int ApplicationsPage::SelectedRuleIndex() const
{
  const int row = tracked_->currentRow();
  return (row >= 0 && row < static_cast<int>(rules_.size())) ? row : -1;
}

void ApplicationsPage::ShowSelectedRule()
{
  const int index = SelectedRuleIndex();
  editor_->setEnabled(index >= 0);
  remove_->setEnabled(index >= 0);
  if (index < 0) {
    return;
  }

  const AutoCaptureRule &rule = rules_[static_cast<size_t>(index)];
  loading_editor_ = true;
  enabled_->setChecked(rule.enabled);
  name_->setText(QString::fromStdString(rule.display_name));
  process_->setText(QString::fromStdString(rule.process_name));
  mode_->setCurrentIndex(rule.capture_mode == AutoCaptureMode::Window
                             ? 1
                             : (rule.capture_mode == AutoCaptureMode::Game ? 2 : 0));
  scope_->setCurrentIndex(rule.fullscreen_only ? 1 : 0);
  title_->setText(QString::fromStdString(rule.title_contains));
  blur_->setChecked(rule.blur_address_bar);
  loading_editor_ = false;
}

void ApplicationsPage::CommitEditorToRule()
{
  if (loading_editor_) {
    return;
  }
  const int index = SelectedRuleIndex();
  if (index < 0) {
    return;
  }

  AutoCaptureRule &rule = rules_[static_cast<size_t>(index)];
  rule.enabled = enabled_->isChecked();
  rule.display_name = name_->text().trimmed().toStdString();
  if (rule.display_name.empty()) {
    rule.display_name = capture_rules::FriendlyName(rule.process_name);
  }
  rule.capture_mode = mode_->currentIndex() == 1
                          ? AutoCaptureMode::Window
                          : (mode_->currentIndex() == 2 ? AutoCaptureMode::Game : AutoCaptureMode::Auto);
  rule.fullscreen_only = scope_->currentIndex() == 1;
  rule.title_contains = title_->text().trimmed().toLower().toStdString();
  rule.blur_address_bar = blur_->isChecked();

  // Only the label is refreshed: rebuilding the list here would fight the
  // cursor of whoever is typing in the name field.
  if (QListWidgetItem *item = tracked_->item(index)) {
    loading_editor_ = true;
    item->setText(RuleLabel(rule));
    item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    loading_editor_ = false;
  }
}

void ApplicationsPage::AddSelectedApp()
{
  QListWidgetItem *item = available_->currentItem();
  if (item == nullptr) {
    return;
  }
  const std::string process = item->data(Qt::UserRole).toString().toStdString();
  if (process.empty()) {
    return;
  }

  const auto duplicate = std::find_if(rules_.begin(), rules_.end(), [&process](const AutoCaptureRule &rule) {
    return rule.process_name == process && rule.title_contains.empty();
  });
  if (duplicate != rules_.end()) {
    tracked_->setCurrentRow(static_cast<int>(std::distance(rules_.begin(), duplicate)));
    return;
  }

  AutoCaptureRule rule;
  rule.process_name = process;
  rule.display_name = capture_rules::FriendlyName(process);
  rule.blur_address_bar = capture_rules::IsBrowser(process);
  rule.id = capture_rules::MakeId(rules_, rule);
  rules_.push_back(std::move(rule));

  ReloadTrackedList();
  tracked_->setCurrentRow(static_cast<int>(rules_.size()) - 1);
}

void ApplicationsPage::RemoveSelectedRule()
{
  const int index = SelectedRuleIndex();
  if (index < 0) {
    return;
  }
  rules_.erase(rules_.begin() + index);
  ReloadTrackedList();
}
