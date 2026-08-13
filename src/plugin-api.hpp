#pragma once

#include "auto-capture-source.hpp"

#include <string>
#include <vector>

// Narrow view of the source internals for the settings window.
//
// The rule format, its two legacy migrations and the id generation live in
// auto-capture-source.cpp and are deliberately not duplicated here: a second
// copy of a storage format is the kind of thing that quietly drifts. These are
// thin wrappers over the functions the source already uses, so the window and
// the source can never disagree about what a rule is.
//
// Everything here is Win32 and libobs only. No Qt.

struct RunningApp {
  std::string process_name;
  std::string sample_title;
  size_t window_count = 0;
};

namespace capture_rules {

std::vector<AutoCaptureRule> Load(obs_data_t *settings);
void Store(obs_data_t *settings, const std::vector<AutoCaptureRule> &rules);

// Ids stay stable across edits, which is what keeps a running capture from
// being dropped when the list is reordered.
std::string MakeId(const std::vector<AutoCaptureRule> &rules, const AutoCaptureRule &rule);

// "chrome.exe" -> "Chrome"
std::string FriendlyName(const std::string &process_name);

// Only decides the default of the per-rule address bar blur.
bool IsBrowser(const std::string &process_name);

} // namespace capture_rules

// One entry per executable, not per window: a browser with twenty windows is
// still one application to add.
std::vector<RunningApp> CollectRunningApps();
