#include "settings-window.hpp"

#include "applications-page.hpp"
#include "plugin-api.hpp"
#include "settings-pages.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <map>
#include <string>

namespace {

const char *Text(const char *key)
{
  return obs_module_text(key);
}

// The window edits a copy of the source settings and only hands it over on OK
// or Apply, so Cancel really is a cancel and a half finished edit never reaches
// a source that is live on stream.
class SettingsWindow : public QDialog {
public:
  SettingsWindow(obs_source_t *source, QWidget *parent);

  const std::string &SourceUuid() const { return uuid_; }

private:
  void AddSection(const char *title_key, QWidget *page);
  void Apply();
  // The source can be removed from the scene while this window is open. Nothing
  // in libobs will tell a plain QDialog about it, so the weak reference is
  // checked on a timer and the window closes itself.
  void CheckSourceAlive();

  std::string uuid_;
  obs_weak_source_t *weak_source_ = nullptr;
  obs_data_t *working_settings_ = nullptr;
  QListWidget *nav_ = nullptr;
  QStackedWidget *pages_ = nullptr;
  ApplicationsPage *applications_ = nullptr;
  MirrorPage *mirror_ = nullptr;
  BlurPage *blur_ = nullptr;
  AdvancedPage *advanced_ = nullptr;

public:
  ~SettingsWindow() override;
};

std::map<std::string, QPointer<SettingsWindow>> g_windows;

SettingsWindow::SettingsWindow(obs_source_t *source, QWidget *parent) : QDialog(parent)
{
  uuid_ = obs_source_get_uuid(source);
  weak_source_ = obs_source_get_weak_source(source);
  working_settings_ = obs_source_get_settings(source);

  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(QString("%1 — %2").arg(Text("AutoAppCapture.Window.Title"), obs_source_get_name(source)));
  resize(900, 600);
  setMinimumSize(720, 460);

  nav_ = new QListWidget(this);
  nav_->setMaximumWidth(220);
  nav_->setMinimumWidth(160);

  pages_ = new QStackedWidget(this);

  applications_ = new ApplicationsPage(pages_);
  applications_->SetRules(capture_rules::Load(working_settings_));
  AddSection("AutoAppCapture.Section.Capture", applications_);

  mirror_ = new MirrorPage(pages_);
  mirror_->Load(working_settings_);
  AddSection("AutoAppCapture.Section.Mirror", mirror_);

  blur_ = new BlurPage(pages_);
  blur_->Load(working_settings_);
  AddSection("AutoAppCapture.Section.Blur", blur_);

  advanced_ = new AdvancedPage(pages_);
  advanced_->Load(working_settings_);
  AddSection("AutoAppCapture.Section.Advanced", advanced_);

  connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
  nav_->setCurrentRow(0);

  auto *content = new QHBoxLayout();
  content->addWidget(nav_);
  content->addWidget(pages_, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
                                       this);
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    Apply();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { Apply(); });

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(content, 1);
  layout->addWidget(buttons);

  auto *watchdog = new QTimer(this);
  connect(watchdog, &QTimer::timeout, this, [this] { CheckSourceAlive(); });
  watchdog->start(500);
}

SettingsWindow::~SettingsWindow()
{
  g_windows.erase(uuid_);
  if (working_settings_ != nullptr) {
    obs_data_release(working_settings_);
  }
  if (weak_source_ != nullptr) {
    obs_weak_source_release(weak_source_);
  }
}

void SettingsWindow::AddSection(const char *title_key, QWidget *page)
{
  nav_->addItem(Text(title_key));
  pages_->addWidget(page);
}

void SettingsWindow::Apply()
{
  obs_source_t *source = obs_weak_source_get_source(weak_source_);
  if (source == nullptr) {
    return;
  }
  capture_rules::Store(working_settings_, applications_->Rules());
  mirror_->Save(working_settings_);
  blur_->Save(working_settings_);
  advanced_->Save(working_settings_);
  obs_source_update(source, working_settings_);
  obs_source_release(source);
}

void SettingsWindow::CheckSourceAlive()
{
  if (weak_source_ == nullptr || obs_weak_source_expired(weak_source_)) {
    close();
  }
}

} // namespace

void auto_capture_open_settings(obs_source_t *source)
{
  if (source == nullptr) {
    return;
  }

  const char *uuid = obs_source_get_uuid(source);
  if (uuid == nullptr) {
    return;
  }

  const auto existing = g_windows.find(uuid);
  if (existing != g_windows.end() && !existing->second.isNull()) {
    existing->second->show();
    existing->second->raise();
    existing->second->activateWindow();
    return;
  }

  auto *main_window = static_cast<QWidget *>(obs_frontend_get_main_window());
  if (main_window == nullptr) {
    blog(LOG_WARNING, "[obs-auto-capture] No main window available, cannot open the settings window.");
    return;
  }

  auto *window = new SettingsWindow(source, main_window);
  g_windows[uuid] = window;
  window->show();
}

void auto_capture_close_settings_windows()
{
  // Closing deletes the window, which erases its own entry, so the map is
  // copied first rather than iterated while it changes.
  const auto windows = g_windows;
  for (const auto &entry : windows) {
    if (!entry.second.isNull()) {
      entry.second->close();
    }
  }
  g_windows.clear();
}
