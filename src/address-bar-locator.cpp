#include "address-bar-locator.hpp"

// The plugin builds with WIN32_LEAN_AND_MEAN, so windows.h leaves out the COM
// headers and the "interface" keyword the UI Automation headers are written in
// stays undefined. objbase.h has to come first.
#include <objbase.h>

#include <uiautomation.h>

#include <obs-module.h>

#include <algorithm>
#include <chrono>

namespace {

uint64_t NowMs()
{
  using namespace std::chrono;
  return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// A poll that takes longer than this means the browser is answering slowly.
// Rather than keep hammering it, the interval is stretched.
constexpr uint64_t kSlowPollMs = 40;
constexpr unsigned kMinPollIntervalMs = 250;
constexpr unsigned kMaxPollIntervalMs = 2000;
// A window whose address bar was not found is not searched again immediately:
// a failed search is the expensive case, and it usually stays failed.
constexpr uint64_t kSearchRetryMs = 5000;
constexpr uint64_t kFirstRetryMs = 700;

template <typename T> void SafeRelease(T *&pointer)
{
  if (pointer != nullptr) {
    pointer->Release();
    pointer = nullptr;
  }
}

} // namespace

AddressBarLocator::~AddressBarLocator()
{
  Stop();
}

void AddressBarLocator::SetTarget(HWND window, bool wanted)
{
  bool wake_now = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (target_ != window || wanted_ != wanted) {
      target_ = window;
      wanted_ = wanted;
      wake_now = true;
      if (!wanted || window == nullptr) {
        result_ = AddressBarRect{};
      } else {
        result_.valid = false;
        result_.status = AddressBarStatus::Searching;
      }
    }
    if (wanted && !thread_running_ && !stop_requested_) {
      thread_running_ = true;
      thread_ = std::thread(&AddressBarLocator::ThreadMain, this);
    }
  }
  if (wake_now) {
    wake_.notify_all();
  }
}

AddressBarRect AddressBarLocator::GetResult() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return result_;
}

void AddressBarLocator::Stop()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!thread_running_) {
      stop_requested_ = true;
      return;
    }
    stop_requested_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  thread_running_ = false;
}

void AddressBarLocator::ThreadMain()
{
  // Multithreaded apartment: this thread never pumps messages, and UI
  // Automation is only polled from here, never subscribed to.
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool com_owned = SUCCEEDED(com);

  while (true) {
    HWND target = nullptr;
    bool wanted = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait_for(lock, std::chrono::milliseconds(poll_interval_ms_),
                     [this] { return stop_requested_; });
      if (stop_requested_) {
        break;
      }
      target = target_;
      wanted = wanted_;
    }

    if (!wanted || target == nullptr || !IsWindow(target)) {
      ForgetElement();
      continue;
    }

    const uint64_t started = NowMs();
    RECT rect = {0, 0, 0, 0};
    const bool located = LocateForWindow(target, &rect);
    const uint64_t elapsed = NowMs() - started;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      // The target may have changed while the call was in flight; that result
      // belongs to a window nobody is capturing any more.
      if (target_ == target && wanted_) {
        result_.valid = located;
        result_.screen = rect;
        result_.timestamp_ms = NowMs();
        if (automation_failed_) {
          result_.status = AddressBarStatus::Unavailable;
        } else {
          result_.status = located ? AddressBarStatus::Found : AddressBarStatus::NotFound;
        }
      }
      if (elapsed > kSlowPollMs) {
        const unsigned next = poll_interval_ms_ * 2;
        if (poll_interval_ms_ < kMaxPollIntervalMs) {
          poll_interval_ms_ = std::min(next, kMaxPollIntervalMs);
          blog(LOG_WARNING,
               "[obs-auto-capture] Address bar lookup took %llu ms, slowing down to %u ms between checks.",
               static_cast<unsigned long long>(elapsed), poll_interval_ms_);
        }
      } else if (poll_interval_ms_ > kMinPollIntervalMs && elapsed * 4 < kSlowPollMs) {
        poll_interval_ms_ = kMinPollIntervalMs;
      }
    }
  }

  ForgetElement();
  ReleaseAutomation();
  if (com_owned) {
    CoUninitialize();
  }
}

bool AddressBarLocator::EnsureAutomation()
{
  if (automation_ != nullptr) {
    return true;
  }
  if (automation_failed_) {
    return false;
  }

  // CUIAutomation8 exposes the timeouts. Without them a hung browser would
  // block this thread until it answers.
  IUIAutomation2 *automation2 = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation2));
  if (SUCCEEDED(hr) && automation2 != nullptr) {
    automation2->put_ConnectionTimeout(1000);
    automation2->put_TransactionTimeout(2000);
    hr = automation2->QueryInterface(IID_PPV_ARGS(&automation_));
    automation2->Release();
  }
  if (automation_ == nullptr) {
    hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation_));
  }
  if (FAILED(hr) || automation_ == nullptr) {
    automation_failed_ = true;
    blog(LOG_WARNING, "[obs-auto-capture] UI Automation is unavailable (0x%08lx); the manual zone will be used.",
         static_cast<unsigned long>(hr));
    return false;
  }

  automation_->CreateTrueCondition(&any_condition_);
  // One request per tree level instead of one per property per element: the
  // walk below only needs the control type and the rectangle, and asking for
  // them in bulk is what keeps the whole search in the low milliseconds.
  automation_->CreateCacheRequest(&cache_request_);
  if (any_condition_ == nullptr || cache_request_ == nullptr) {
    automation_failed_ = true;
    ReleaseAutomation();
    return false;
  }
  cache_request_->AddProperty(UIA_ControlTypePropertyId);
  cache_request_->AddProperty(UIA_BoundingRectanglePropertyId);
  cache_request_->put_TreeScope(TreeScope_Element);
  cache_request_->put_AutomationElementMode(AutomationElementMode_Full);
  return true;
}

void AddressBarLocator::ReleaseAutomation()
{
  SafeRelease(cache_request_);
  SafeRelease(any_condition_);
  SafeRelease(automation_);
}

void AddressBarLocator::ForgetElement()
{
  SafeRelease(address_bar_);
  cached_window_ = nullptr;
  failed_searches_ = 0;
  next_search_attempt_ms_ = 0;
}

// Both checks are geometric on purpose. Matching the element name would mean
// matching localised browser strings, and a Russian Chrome, an English one and
// Firefox all call the address bar something different.
bool AddressBarLocator::LooksLikeAddressBar(const RECT &window_bounds, const RECT &rect)
{
  const long window_width = window_bounds.right - window_bounds.left;
  const long window_height = window_bounds.bottom - window_bounds.top;
  const long width = rect.right - rect.left;
  const long height = rect.bottom - rect.top;
  if (window_width <= 0 || window_height <= 0 || width <= 0 || height <= 0) {
    return false;
  }
  // Wide, thin, and inside the browser chrome above the page.
  if (width * 4 < window_width) {
    return false;
  }
  if (height * 12 > window_height || height < 8) {
    return false;
  }
  return (rect.top - window_bounds.top) * 5 < window_height;
}

// Nothing below the top fifth of the window can contain the address bar, and
// not descending there is what keeps the page out of the walk entirely.
bool AddressBarLocator::WorthDescending(const RECT &window_bounds, const RECT &rect)
{
  const long window_height = window_bounds.bottom - window_bounds.top;
  if (window_height <= 0) {
    return false;
  }
  if (rect.right <= rect.left || rect.bottom <= rect.top) {
    return true;  // No geometry yet; cannot rule the subtree out.
  }
  return (rect.top - window_bounds.top) * 5 < window_height;
}

// Level by level, widest first, pruning everything that cannot hold an address
// bar. The alternative, FindFirst over all descendants, walks into the page and
// makes the browser build the accessibility tree of the whole document: that
// measured 324 ms on a Twitch tab and picked an element inside the page.
bool AddressBarLocator::FindAddressBar(HWND window)
{
  if (!EnsureAutomation()) {
    return false;
  }

  RECT window_bounds = {0, 0, 0, 0};
  if (!GetWindowRect(window, &window_bounds)) {
    return false;
  }

  IUIAutomationElement *root = nullptr;
  if (FAILED(automation_->ElementFromHandle(window, &root)) || root == nullptr) {
    return false;
  }

  constexpr int kMaxDepth = 8;
  constexpr int kMaxNodes = 256;

  IUIAutomationElement *level[kMaxNodes];
  int level_count = 0;
  level[level_count++] = root;
  int visited = 0;

  IUIAutomationElement *best = nullptr;
  long best_width = 0;

  for (int depth = 0; depth < kMaxDepth && level_count > 0 && best == nullptr; ++depth) {
    IUIAutomationElement *next_level[kMaxNodes];
    int next_count = 0;

    for (int i = 0; i < level_count; ++i) {
      IUIAutomationElementArray *children = nullptr;
      if (SUCCEEDED(level[i]->FindAllBuildCache(TreeScope_Children, any_condition_, cache_request_, &children)) &&
          children != nullptr) {
        int child_count = 0;
        children->get_Length(&child_count);
        for (int c = 0; c < child_count && visited < kMaxNodes; ++c) {
          IUIAutomationElement *child = nullptr;
          if (FAILED(children->GetElement(c, &child)) || child == nullptr) {
            continue;
          }
          ++visited;

          CONTROLTYPEID control_type = 0;
          RECT rect = {0, 0, 0, 0};
          child->get_CachedControlType(&control_type);
          child->get_CachedBoundingRectangle(&rect);

          // The page itself. Everything under it belongs to the site.
          if (control_type == UIA_DocumentControlTypeId) {
            child->Release();
            continue;
          }

          // Chrome and Edge expose the omnibox as an Edit, Firefox exposes its
          // address bar as a ComboBox with the URL in a Text child. Both are
          // accepted, and the widest match wins: a search field next to the
          // address bar is always the narrower of the two.
          const bool candidate_type =
              control_type == UIA_EditControlTypeId || control_type == UIA_ComboBoxControlTypeId;
          if (candidate_type && LooksLikeAddressBar(window_bounds, rect)) {
            const long width = rect.right - rect.left;
            if (width > best_width) {
              SafeRelease(best);
              best = child;
              best_width = width;
              continue;
            }
          }

          if (next_count < kMaxNodes && WorthDescending(window_bounds, rect)) {
            next_level[next_count++] = child;
          } else {
            child->Release();
          }
        }
        children->Release();
      }
      level[i]->Release();
    }

    level_count = next_count;
    for (int i = 0; i < next_count; ++i) {
      level[i] = next_level[i];
    }
  }

  for (int i = 0; i < level_count; ++i) {
    level[i]->Release();
  }

  if (best == nullptr) {
    return false;
  }

  SafeRelease(address_bar_);
  address_bar_ = best;
  cached_window_ = window;
  return true;
}

bool AddressBarLocator::LocateForWindow(HWND window, RECT *rect)
{
  if (cached_window_ != window) {
    ForgetElement();
  }

  RECT window_bounds = {0, 0, 0, 0};
  if (!GetWindowRect(window, &window_bounds)) {
    return false;
  }

  if (address_bar_ != nullptr) {
    RECT current = {0, 0, 0, 0};
    // The cheap path: one cross-process property read on an element that was
    // already found. Everything else only runs when this stops working.
    if (SUCCEEDED(address_bar_->get_CurrentBoundingRectangle(&current)) &&
        LooksLikeAddressBar(window_bounds, current)) {
      *rect = current;
      return true;
    }
    ForgetElement();
  }

  const uint64_t now = NowMs();
  if (now < next_search_attempt_ms_) {
    return false;
  }
  if (!FindAddressBar(window)) {
    // A browser builds its accessibility tree only once someone asks, so the
    // first attempt on a fresh window often finds nothing. Retry soon, then
    // back off for windows that simply have no address bar.
    ++failed_searches_;
    next_search_attempt_ms_ = now + (failed_searches_ < 3 ? kFirstRetryMs : kSearchRetryMs);
    return false;
  }
  failed_searches_ = 0;
  next_search_attempt_ms_ = 0;

  RECT found = {0, 0, 0, 0};
  if (FAILED(address_bar_->get_CurrentBoundingRectangle(&found))) {
    ForgetElement();
    return false;
  }
  *rect = found;
  return true;
}
