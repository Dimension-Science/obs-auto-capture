#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

struct IUIAutomation;
struct IUIAutomationElement;
struct IUIAutomationCondition;
struct IUIAutomationCacheRequest;

// Why the address bar is looked up: only for the status line, never for a
// decision about what to cover. Anything other than Found means the caller
// falls back to the manual zone.
enum class AddressBarStatus {
  Idle,       // Nothing to look for right now.
  Searching,  // Target known, no rectangle yet.
  Found,
  NotFound,   // The window has no element that looks like an address bar.
  Unavailable // UI Automation itself could not be started.
};

struct AddressBarRect {
  bool valid = false;
  RECT screen = {0, 0, 0, 0};  // Physical screen pixels, like the capture.
  uint64_t timestamp_ms = 0;
  AddressBarStatus status = AddressBarStatus::Idle;
};

// Polls the browser for the position of its address bar on a thread of its own.
//
// Everything expensive happens there: UI Automation calls cross a process
// boundary and can block for as long as the browser is busy. The render thread
// only ever reads the last known rectangle under a short lock.
class AddressBarLocator {
public:
  AddressBarLocator() = default;
  ~AddressBarLocator();

  AddressBarLocator(const AddressBarLocator &) = delete;
  AddressBarLocator &operator=(const AddressBarLocator &) = delete;

  // Both are safe to call from any thread and cheap enough for every tick.
  void SetTarget(HWND window, bool wanted);
  AddressBarRect GetResult() const;

  void Stop();

private:
  void ThreadMain();
  bool EnsureAutomation();
  void ReleaseAutomation();
  void ForgetElement();
  // Runs on the worker thread only.
  bool LocateForWindow(HWND window, RECT *rect);
  bool FindAddressBar(HWND window);
  static bool LooksLikeAddressBar(const RECT &window_bounds, const RECT &rect);
  static bool WorthDescending(const RECT &window_bounds, const RECT &rect);

  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::thread thread_;
  bool thread_running_ = false;
  bool stop_requested_ = false;
  HWND target_ = nullptr;
  bool wanted_ = false;
  AddressBarRect result_;
  // Grows when a poll turns out to be slow, so a busy browser cannot drag the
  // whole plugin down.
  unsigned poll_interval_ms_ = 250;

  // Worker thread state, never touched from outside.
  IUIAutomation *automation_ = nullptr;
  IUIAutomationCondition *any_condition_ = nullptr;
  IUIAutomationCacheRequest *cache_request_ = nullptr;
  IUIAutomationElement *address_bar_ = nullptr;
  HWND cached_window_ = nullptr;
  uint64_t next_search_attempt_ms_ = 0;
  unsigned failed_searches_ = 0;
  bool automation_failed_ = false;
};
