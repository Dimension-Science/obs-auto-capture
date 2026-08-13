#include "auto-capture-source.hpp"

#include "plugin-api.hpp"

#ifdef AUTO_CAPTURE_HAS_UI
#include "settings-window.hpp"
#endif

#include <dwmapi.h>

#include <util/dstr.h>
#include <util/windows/window-helpers.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
constexpr const char *kSourceId = "auto_app_capture_source";

// Stored rule list and legacy settings kept for backwards compatibility.
constexpr const char *kRulesSetting = "target_rules";
constexpr const char *kLegacyTargetsSetting = "target_processes";
constexpr const char *kLegacyFullscreenOnlySetting = "fullscreen_only";

// Rule editor (applies to the rule selected in the list).
constexpr const char *kSelectedRuleSetting = "selected_rule";
constexpr const char *kEditTargetSetting = "edit_target";
constexpr const char *kEditEnabledSetting = "edit_enabled";
constexpr const char *kEditNameSetting = "edit_name";
constexpr const char *kEditProcessSetting = "edit_process";
constexpr const char *kEditModeSetting = "edit_mode";
constexpr const char *kEditScopeSetting = "edit_scope";
constexpr const char *kEditTitleSetting = "edit_title";
constexpr const char *kEditBlurSetting = "edit_blur";

// "Add application" section.
constexpr const char *kNewProcessSetting = "available_process";
constexpr const char *kNewNameSetting = "new_rule_name";
constexpr const char *kNewModeSetting = "new_rule_mode";
constexpr const char *kNewScopeSetting = "new_rule_scope";
constexpr const char *kNewTitleSetting = "new_rule_title";

// Everything else.
constexpr const char *kPollIntervalSetting = "poll_interval_seconds";
constexpr const char *kStatusTextSetting = "status_text";
constexpr const char *kMirrorEnabledSetting = "mirror_enabled";
constexpr const char *kMirrorTitleRulesSetting = "mirror_title_rules";

// Panel sections. The dialog has no tab widget, so one selector at the top
// decides which group of settings is shown; the rest keep working while hidden.
constexpr const char *kUiSectionSetting = "ui_section";
constexpr const char *kSectionCapture = "capture";
constexpr const char *kSectionMirror = "mirror";
constexpr const char *kSectionBlur = "blur";
constexpr const char *kSectionAdvanced = "advanced";

constexpr const char *kRulesGroup = "rules_group";
constexpr const char *kAddGroup = "add_group";
constexpr const char *kMirrorGroup = "mirror_group";
constexpr const char *kBlurGroup = "blur_group";
constexpr const char *kAdvancedGroup = "advanced_group";

// Address bar blur.
constexpr const char *kBlurEnabledSetting = "blur_enabled";
constexpr const char *kBlurModeSetting = "blur_mode";
constexpr const char *kBlurStrengthSetting = "blur_strength";
constexpr const char *kBlurDetectSetting = "blur_detect";
constexpr const char *kBlurPaddingSetting = "blur_padding";
constexpr const char *kBlurFillColorSetting = "blur_fill_color";
constexpr const char *kBlurZoneLeftSetting = "blur_zone_left";
constexpr const char *kBlurZoneTopSetting = "blur_zone_top";
constexpr const char *kBlurZoneWidthSetting = "blur_zone_width";
constexpr const char *kBlurZoneHeightSetting = "blur_zone_height";
constexpr const char *kBlurHint = "blur_hint";

constexpr const char *kBlurDetectAuto = "auto";
constexpr const char *kBlurDetectManual = "manual";

constexpr const char *kBlurModeFrosted = "frosted";
constexpr const char *kBlurModeMosaic = "mosaic";
constexpr const char *kBlurModeFill = "fill";

constexpr const char *kBlurEffectFile = "shaders/browser-privacy-blur.effect";

constexpr const char *kRulesEmptyHint = "rules_empty_hint";
constexpr const char *kPickWindowAction = "pick_window";
constexpr const char *kAddRuleAction = "add_rule";
constexpr const char *kDeleteRuleAction = "delete_rule";
constexpr const char *kRefreshAction = "refresh_state";
constexpr const char *kOpenSettingsAction = "open_settings";

constexpr const char *kWindowCaptureSourceId = "window_capture";
constexpr const char *kWindowCaptureWindowSetting = "window";
constexpr const char *kWindowCaptureMethodSetting = "method";
constexpr const char *kWindowCapturePrioritySetting = "priority";
constexpr const char *kWindowCaptureCursorSetting = "cursor";
constexpr const char *kWindowCaptureCompatibilitySetting = "compatibility";
constexpr const char *kWindowCaptureClientAreaSetting = "client_area";
constexpr const char *kWindowCaptureForceSdrSetting = "force_sdr";
constexpr int kWindowCaptureMethodWgc = 2;

constexpr const char *kGameCaptureSourceId = "game_capture";
constexpr const char *kGameCaptureModeSetting = "capture_mode";
constexpr const char *kGameCaptureWindowSetting = "window";
constexpr const char *kGameCapturePrioritySetting = "priority";
constexpr const char *kGameCaptureCompatibilitySetting = "sli_compatibility";
constexpr const char *kGameCaptureCursorSetting = "capture_cursor";
constexpr const char *kGameCaptureTransparencySetting = "allow_transparency";
constexpr const char *kGameCapturePremultipliedAlphaSetting = "premultiplied_alpha";
constexpr const char *kGameCaptureLimitFramerateSetting = "limit_framerate";
constexpr const char *kGameCaptureCaptureOverlaysSetting = "capture_overlays";
constexpr const char *kGameCaptureAntiCheatHookSetting = "anti_cheat_hook";
constexpr const char *kGameCaptureHookRateSetting = "hook_rate";
constexpr const char *kGameCaptureRgb10a2SpaceSetting = "rgb10a2_space";
constexpr const char *kGameCaptureModeWindow = "window";
constexpr const char *kGameCaptureRgb10a2SpaceSrgb = "srgb";
constexpr int kGameCaptureHookRateNormal = 1;

constexpr const char *kModeAuto = "auto";
constexpr const char *kModeWindow = "window";
constexpr const char *kModeGame = "game";
constexpr const char *kRuleScopeAny = "any";
constexpr const char *kRuleScopeFullscreen = "fullscreen";
constexpr const char *kRuleWildcard = "*";

const char *Text(const char *key)
{
  return obs_module_text(key);
}

const char *CaptureModeToken(AutoCaptureMode mode)
{
  switch (mode) {
    case AutoCaptureMode::Window:
      return kModeWindow;
    case AutoCaptureMode::Game:
      return kModeGame;
    case AutoCaptureMode::Auto:
    default:
      return kModeAuto;
  }
}

AutoCaptureMode ParseCaptureModeToken(const std::string &value)
{
  const std::string token = AutoCaptureSource::ToLower(AutoCaptureSource::Trim(value));
  if (token == kModeWindow || token == "window_capture") {
    return AutoCaptureMode::Window;
  }
  if (token == kModeGame || token == "game_capture") {
    return AutoCaptureMode::Game;
  }
  return AutoCaptureMode::Auto;
}

const char *DisplayCaptureMode(AutoCaptureMode mode)
{
  switch (mode) {
    case AutoCaptureMode::Window:
      return Text("AutoAppCapture.Mode.Window.Short");
    case AutoCaptureMode::Game:
      return Text("AutoAppCapture.Mode.Game.Short");
    case AutoCaptureMode::Auto:
    default:
      return Text("AutoAppCapture.Mode.Auto.Short");
  }
}

const char *DisplayBackendName(AutoCaptureBackend backend)
{
  switch (backend) {
    case AutoCaptureBackend::WindowCapture:
      return Text("AutoAppCapture.Backend.Window");
    case AutoCaptureBackend::GameCapture:
      return Text("AutoAppCapture.Backend.Game");
    case AutoCaptureBackend::None:
    default:
      return Text("AutoAppCapture.Backend.None");
  }
}

const char *BackendLogName(AutoCaptureBackend backend)
{
  switch (backend) {
    case AutoCaptureBackend::WindowCapture:
      return "window_capture";
    case AutoCaptureBackend::GameCapture:
      return "game_capture";
    case AutoCaptureBackend::None:
    default:
      return "none";
  }
}

std::string WideToUtf8(const wchar_t *value, int length)
{
  if (!value || length <= 0) {
    return {};
  }
  const int required = WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr, 0, nullptr, nullptr);
  if (required <= 0) {
    return {};
  }
  std::string result(static_cast<size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, length, result.data(), required, nullptr, nullptr);
  return result;
}

std::wstring Utf8ToWide(const std::string &value)
{
  if (value.empty()) {
    return {};
  }
  const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
  if (required <= 0) {
    return {};
  }
  std::wstring result(static_cast<size_t>(required), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), required);
  return result;
}

std::wstring LocalizedWide(const char *key)
{
  return Utf8ToWide(Text(key));
}

std::string EscapeHtml(const std::string &value)
{
  std::string result;
  result.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '&':
        result += "&amp;";
        break;
      case '<':
        result += "&lt;";
        break;
      case '>':
        result += "&gt;";
        break;
      default:
        result += ch;
        break;
    }
  }
  return result;
}

// "chrome.exe" -> "Chrome": a readable default rule name.
std::string FriendlyProcessName(const std::string &process_name)
{
  std::string name = process_name;
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) {
    name.erase(dot);
  }
  if (name.empty()) {
    return process_name;
  }
  name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  return name;
}

// Decides the default of the per-rule address bar blur. Only a default: the
// checkbox in the rule editor always wins, so an unlisted browser is one click
// away and a false positive is one click from off.
bool IsBrowserProcess(const std::string &process_name)
{
  static const char *const kBrowsers[] = {
      "chrome.exe",   "msedge.exe", "firefox.exe", "opera.exe",  "opera_gx.exe", "browser.exe",
      "vivaldi.exe",  "brave.exe",  "arc.exe",     "chromium.exe", "iexplore.exe", "waterfox.exe",
      "librewolf.exe", "thorium.exe", "yandex.exe",
  };
  const std::string name = AutoCaptureSource::ToLower(process_name);
  for (const char *browser : kBrowsers) {
    if (name == browser) {
      return true;
    }
  }
  return false;
}

std::string GetWindowTitle(HWND hwnd)
{
  const int length = GetWindowTextLengthW(hwnd);
  if (length <= 0) {
    return {};
  }
  std::wstring wide_title(static_cast<size_t>(length) + 1, L'\0');
  const int written = GetWindowTextW(hwnd, wide_title.data(), length + 1);
  if (written <= 0) {
    return {};
  }
  return WideToUtf8(wide_title.c_str(), written);
}

std::string GetProcessNameFromPid(DWORD pid)
{
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) {
    return {};
  }
  wchar_t buffer[MAX_PATH] = {};
  DWORD size = static_cast<DWORD>(std::size(buffer));
  std::string result;
  if (QueryFullProcessImageNameW(process, 0, buffer, &size) != 0 && size > 0) {
    std::wstring full_path(buffer, size);
    const size_t slash = full_path.find_last_of(L"\\/");
    const std::wstring file_name = slash == std::wstring::npos ? full_path : full_path.substr(slash + 1);
    result = WideToUtf8(file_name.c_str(), static_cast<int>(file_name.size()));
  }
  CloseHandle(process);
  return result;
}

bool IsWindowEffectivelyFullscreen(HWND hwnd)
{
  RECT window_rect = {};
  if (!GetWindowRect(hwnd, &window_rect)) {
    return false;
  }
  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  if (!monitor) {
    return false;
  }
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    return false;
  }
  const RECT monitor_rect = info.rcMonitor;
  return window_rect.left <= monitor_rect.left && window_rect.top <= monitor_rect.top &&
         window_rect.right >= monitor_rect.right && window_rect.bottom >= monitor_rect.bottom;
}

bool IsCandidateWindowForPicker(HWND hwnd)
{
  if (!IsWindow(hwnd)) {
    return false;
  }
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if ((style & WS_CHILD) != 0 || (ex_style & WS_EX_TOOLWINDOW) != 0) {
    return false;
  }
  if (GetWindow(hwnd, GW_OWNER) != nullptr) {
    return false;
  }
  return true;
}

HWND NormalizeWindowForCapture(HWND hwnd)
{
  if (!hwnd || !IsWindow(hwnd)) {
    return nullptr;
  }
  HWND root = GetAncestor(hwnd, GA_ROOT);
  if (root && IsWindow(root)) {
    hwnd = root;
  }
  return hwnd;
}

void ReplaceAll(std::string &value, const std::string &from, const std::string &to)
{
  if (from.empty()) {
    return;
  }
  size_t position = 0;
  while ((position = value.find(from, position)) != std::string::npos) {
    value.replace(position, from.size(), to);
    position += to.size();
  }
}

void EncodeWindowComponent(std::string &value)
{
  ReplaceAll(value, "#", "#22");
  ReplaceAll(value, ":", "#3A");
}

obs_data_t *CreateWindowCaptureSettings(const std::string &window_selector)
{
  obs_data_t *settings = obs_data_create();
  obs_data_set_string(settings, kWindowCaptureWindowSetting, window_selector.c_str());
  obs_data_set_int(settings, kWindowCaptureMethodSetting, kWindowCaptureMethodWgc);
  obs_data_set_int(settings, kWindowCapturePrioritySetting, WINDOW_PRIORITY_TITLE);
  obs_data_set_bool(settings, kWindowCaptureCursorSetting, true);
  obs_data_set_bool(settings, kWindowCaptureCompatibilitySetting, false);
  obs_data_set_bool(settings, kWindowCaptureClientAreaSetting, false);
  obs_data_set_bool(settings, kWindowCaptureForceSdrSetting, false);
  return settings;
}

obs_data_t *CreateGameCaptureSettings(const std::string &window_selector)
{
  obs_data_t *settings = obs_data_create();
  obs_data_set_string(settings, kGameCaptureModeSetting, kGameCaptureModeWindow);
  obs_data_set_string(settings, kGameCaptureWindowSetting, window_selector.c_str());
  obs_data_set_int(settings, kGameCapturePrioritySetting, WINDOW_PRIORITY_EXE);
  obs_data_set_bool(settings, kGameCaptureCompatibilitySetting, false);
  obs_data_set_bool(settings, kGameCaptureCursorSetting, true);
  obs_data_set_bool(settings, kGameCaptureTransparencySetting, false);
  obs_data_set_bool(settings, kGameCapturePremultipliedAlphaSetting, false);
  obs_data_set_bool(settings, kGameCaptureLimitFramerateSetting, false);
  obs_data_set_bool(settings, kGameCaptureCaptureOverlaysSetting, false);
  obs_data_set_bool(settings, kGameCaptureAntiCheatHookSetting, true);
  obs_data_set_int(settings, kGameCaptureHookRateSetting, kGameCaptureHookRateNormal);
  obs_data_set_string(settings, kGameCaptureRgb10a2SpaceSetting, kGameCaptureRgb10a2SpaceSrgb);
  return settings;
}

bool HasUsableVideo(obs_source_t *source)
{
  return source != nullptr && obs_source_get_width(source) > 0 && obs_source_get_height(source) > 0;
}

// ---------------------------------------------------------------------------
// Running window / process enumeration
// ---------------------------------------------------------------------------

struct WindowOption {
  std::string process_name;
  std::string window_title;
  std::string label;
};

struct ProcessOption {
  std::string process_name;
  std::string sample_title;
  size_t window_count = 0;
  std::string label;
};

BOOL CALLBACK CollectWindowsProc(HWND hwnd, LPARAM lParam)
{
  if (!IsCandidateWindowForPicker(hwnd) || !IsWindowVisible(hwnd)) {
    return TRUE;
  }
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0) {
    return TRUE;
  }
  const std::string process_name = AutoCaptureSource::ToLower(GetProcessNameFromPid(pid));
  if (process_name.empty()) {
    return TRUE;
  }
  const std::string title = AutoCaptureSource::Trim(GetWindowTitle(hwnd));
  if (title.empty()) {
    return TRUE;
  }
  auto *windows = reinterpret_cast<std::vector<WindowOption> *>(lParam);
  if (!windows) {
    return TRUE;
  }
  windows->push_back({process_name, title, "[" + process_name + "]  " + title});
  return TRUE;
}

std::vector<WindowOption> CollectWindows()
{
  std::vector<WindowOption> windows;
  EnumWindows(CollectWindowsProc, reinterpret_cast<LPARAM>(&windows));
  std::sort(windows.begin(), windows.end(), [](const WindowOption &left, const WindowOption &right) {
    return AutoCaptureSource::ToLower(left.label) < AutoCaptureSource::ToLower(right.label);
  });
  return windows;
}

// One entry per executable instead of one entry per window, so the combo box
// stays readable when a browser has twenty tabs open in separate windows.
std::vector<ProcessOption> CollectProcesses(const std::vector<WindowOption> &windows)
{
  std::vector<ProcessOption> processes;
  for (const WindowOption &window : windows) {
    auto existing = std::find_if(processes.begin(), processes.end(), [&window](const ProcessOption &option) {
      return option.process_name == window.process_name;
    });
    if (existing == processes.end()) {
      ProcessOption option;
      option.process_name = window.process_name;
      option.sample_title = window.window_title;
      option.window_count = 1;
      processes.push_back(std::move(option));
    } else {
      ++existing->window_count;
    }
  }
  for (ProcessOption &option : processes) {
    std::ostringstream label;
    label << option.process_name << "  \xE2\x80\x94  " << option.sample_title;
    if (option.window_count > 1) {
      label << "  (+" << (option.window_count - 1) << " " << Text("AutoAppCapture.Add.MoreWindows") << ")";
    }
    option.label = label.str();
  }
  std::sort(processes.begin(), processes.end(), [](const ProcessOption &left, const ProcessOption &right) {
    return left.process_name < right.process_name;
  });
  return processes;
}

// ---------------------------------------------------------------------------
// Native "choose a window" dialog
// ---------------------------------------------------------------------------

constexpr int kPickerListId = 1001;
constexpr int kPickerSelectId = 1002;
constexpr int kPickerCancelId = 1003;
constexpr int kPickerRefreshId = 1004;
constexpr int kPickerFilterId = 1005;
constexpr int kPickerFilterLabelId = 1006;

const wchar_t *const kPickerClassName = L"ObsAutoCaptureWindowPicker";
bool g_picker_class_registered = false;

struct WindowPickerState {
  std::vector<WindowOption> windows;
  std::vector<size_t> visible;
  int selected_index = -1;
  bool finished = false;
  HFONT font = nullptr;
  int dpi = 96;
};

int ScaleForDpi(int value, int dpi)
{
  return MulDiv(value, dpi, 96);
}

void CreatePickerFont(WindowPickerState *state)
{
  if (state->font) {
    DeleteObject(state->font);
    state->font = nullptr;
  }
  state->font = CreateFontW(-MulDiv(9, state->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  if (!state->font) {
    state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  }
}

void ApplyPickerFont(HWND window, WindowPickerState *state)
{
  const int ids[] = {kPickerListId, kPickerSelectId, kPickerCancelId, kPickerRefreshId, kPickerFilterId,
                     kPickerFilterLabelId};
  for (const int id : ids) {
    if (HWND control = GetDlgItem(window, id)) {
      SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
    }
  }
}

void LayoutPicker(HWND window, WindowPickerState *state)
{
  RECT client = {};
  GetClientRect(window, &client);
  const int dpi = state->dpi;
  const int margin = ScaleForDpi(12, dpi);
  const int row = ScaleForDpi(26, dpi);
  const int button_width = ScaleForDpi(104, dpi);
  const int label_width = ScaleForDpi(64, dpi);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;

  const int filter_left = margin + label_width + ScaleForDpi(4, dpi);
  const int filter_width = std::max(ScaleForDpi(80, dpi), width - filter_left - margin - button_width - margin);

  SetWindowPos(GetDlgItem(window, kPickerFilterLabelId), nullptr, margin, margin + ScaleForDpi(4, dpi), label_width,
               row, SWP_NOZORDER);
  SetWindowPos(GetDlgItem(window, kPickerFilterId), nullptr, filter_left, margin, filter_width, row, SWP_NOZORDER);
  SetWindowPos(GetDlgItem(window, kPickerRefreshId), nullptr, width - margin - button_width, margin, button_width, row,
               SWP_NOZORDER);

  const int list_top = margin + row + ScaleForDpi(10, dpi);
  const int list_height = std::max(ScaleForDpi(80, dpi), height - list_top - margin - row - ScaleForDpi(10, dpi));
  SetWindowPos(GetDlgItem(window, kPickerListId), nullptr, margin, list_top, width - 2 * margin, list_height,
               SWP_NOZORDER);

  const int buttons_top = list_top + list_height + ScaleForDpi(10, dpi);
  SetWindowPos(GetDlgItem(window, kPickerCancelId), nullptr, width - margin - button_width, buttons_top, button_width,
               row, SWP_NOZORDER);
  SetWindowPos(GetDlgItem(window, kPickerSelectId), nullptr,
               width - margin - 2 * button_width - ScaleForDpi(8, dpi), buttons_top, button_width, row, SWP_NOZORDER);
}

void FillPickerList(HWND window, WindowPickerState *state)
{
  HWND list = GetDlgItem(window, kPickerListId);
  if (!list) {
    return;
  }
  wchar_t filter_buffer[128] = {};
  GetDlgItemTextW(window, kPickerFilterId, filter_buffer, static_cast<int>(std::size(filter_buffer)));
  const std::string filter = AutoCaptureSource::ToLower(WideToUtf8(filter_buffer, lstrlenW(filter_buffer)));

  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  state->visible.clear();
  for (size_t i = 0; i < state->windows.size(); ++i) {
    const WindowOption &option = state->windows[i];
    if (!filter.empty() && AutoCaptureSource::ToLower(option.label).find(filter) == std::string::npos) {
      continue;
    }
    const std::wstring label = Utf8ToWide(option.label);
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    state->visible.push_back(i);
  }
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
  if (!state->visible.empty()) {
    SendMessageW(list, LB_SETCURSEL, 0, 0);
  }
}

void FinishPicker(HWND window, WindowPickerState *state, bool accept)
{
  if (accept) {
    const int index = static_cast<int>(SendDlgItemMessageW(window, kPickerListId, LB_GETCURSEL, 0, 0));
    if (index >= 0 && static_cast<size_t>(index) < state->visible.size()) {
      state->selected_index = static_cast<int>(state->visible[static_cast<size_t>(index)]);
    }
  }
  DestroyWindow(window);
}

LRESULT CALLBACK WindowPickerProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
  auto *state = reinterpret_cast<WindowPickerState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto *create = reinterpret_cast<CREATESTRUCTW *>(l_param);
    state = static_cast<WindowPickerState *>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  if (!state) {
    return DefWindowProcW(window, message, w_param, l_param);
  }

  switch (message) {
    case WM_CREATE: {
      HINSTANCE instance = reinterpret_cast<CREATESTRUCTW *>(l_param)->hInstance;
      state->dpi = static_cast<int>(GetDpiForWindow(window));
      if (state->dpi <= 0) {
        state->dpi = 96;
      }
      CreatePickerFont(state);
      const std::wstring filter_label = LocalizedWide("AutoAppCapture.Picker.Filter");
      const std::wstring refresh_label = LocalizedWide("AutoAppCapture.Picker.Refresh");
      const std::wstring select_label = LocalizedWide("AutoAppCapture.Picker.Select");
      const std::wstring cancel_label = LocalizedWide("AutoAppCapture.Picker.Cancel");
      CreateWindowExW(0, L"STATIC", filter_label.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPickerFilterLabelId)), instance, nullptr);
      CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                      window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPickerFilterId)), instance, nullptr);
      CreateWindowExW(0, L"BUTTON", refresh_label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPickerRefreshId)), instance, nullptr);
      CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY, 0, 0, 0, 0, window,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPickerListId)), instance, nullptr);
      CreateWindowExW(0, L"BUTTON", select_label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0,
                      0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPickerSelectId)), instance, nullptr);
      CreateWindowExW(0, L"BUTTON", cancel_label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPickerCancelId)), instance, nullptr);
      ApplyPickerFont(window, state);
      LayoutPicker(window, state);
      FillPickerList(window, state);
      return 0;
    }
    case WM_SIZE:
      LayoutPicker(window, state);
      return 0;
    case WM_DPICHANGED: {
      state->dpi = HIWORD(w_param);
      CreatePickerFont(state);
      ApplyPickerFont(window, state);
      const RECT *suggested = reinterpret_cast<const RECT *>(l_param);
      SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      LayoutPicker(window, state);
      return 0;
    }
    case WM_GETMINMAXINFO: {
      auto *info = reinterpret_cast<MINMAXINFO *>(l_param);
      info->ptMinTrackSize.x = ScaleForDpi(420, state->dpi);
      info->ptMinTrackSize.y = ScaleForDpi(260, state->dpi);
      return 0;
    }
    case WM_COMMAND: {
      const int control = LOWORD(w_param);
      const int notification = HIWORD(w_param);
      if (control == kPickerFilterId && notification == EN_CHANGE) {
        FillPickerList(window, state);
        return 0;
      }
      if (control == kPickerRefreshId) {
        state->windows = CollectWindows();
        FillPickerList(window, state);
        return 0;
      }
      if (control == kPickerSelectId || control == IDOK ||
          (control == kPickerListId && notification == LBN_DBLCLK)) {
        FinishPicker(window, state, true);
        return 0;
      }
      if (control == kPickerCancelId || control == IDCANCEL) {
        FinishPicker(window, state, false);
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      FinishPicker(window, state, false);
      return 0;
    case WM_NCDESTROY:
      if (state->font && state->font != GetStockObject(DEFAULT_GUI_FONT)) {
        DeleteObject(state->font);
      }
      state->font = nullptr;
      state->finished = true;
      break;
  }
  return DefWindowProcW(window, message, w_param, l_param);
}

bool EnsurePickerClass(HINSTANCE instance)
{
  if (g_picker_class_registered) {
    return true;
  }
  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = WindowPickerProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  window_class.lpszClassName = kPickerClassName;
  g_picker_class_registered = RegisterClassW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  return g_picker_class_registered;
}

// Runs a modal dialog owned by the OBS window that is currently active, so the
// properties dialog cannot be used while the picker is open.
int ShowWindowPicker(std::vector<WindowOption> &windows)
{
  HINSTANCE instance = static_cast<HINSTANCE>(obs_get_module_lib(obs_current_module()));
  if (!instance) {
    instance = GetModuleHandleW(nullptr);
  }
  if (!EnsurePickerClass(instance)) {
    blog(LOG_WARNING, "[obs-auto-capture] Unable to register the window picker class.");
    return -1;
  }

  WindowPickerState state;
  state.windows = windows;

  HWND owner = GetActiveWindow();
  if (!owner) {
    HWND foreground = GetForegroundWindow();
    DWORD foreground_pid = 0;
    if (foreground) {
      GetWindowThreadProcessId(foreground, &foreground_pid);
    }
    if (foreground_pid == GetCurrentProcessId()) {
      owner = foreground;
    }
  }
  const int dpi = owner ? static_cast<int>(GetDpiForWindow(owner)) : 96;
  const int width = ScaleForDpi(760, dpi > 0 ? dpi : 96);
  const int height = ScaleForDpi(460, dpi > 0 ? dpi : 96);
  int left = CW_USEDEFAULT;
  int top = CW_USEDEFAULT;
  RECT owner_rect = {};
  if (owner && GetWindowRect(owner, &owner_rect)) {
    left = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    top = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
  }

  const std::wstring title = LocalizedWide("AutoAppCapture.Picker.Title");
  HWND picker = CreateWindowExW(WS_EX_DLGMODALFRAME, kPickerClassName, title.c_str(),
                                WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE, left, top, width, height, owner,
                                nullptr, instance, &state);
  if (!picker) {
    blog(LOG_WARNING, "[obs-auto-capture] Unable to create the window picker dialog.");
    return -1;
  }

  // Disable the whole owner chain (properties dialog plus the OBS main window)
  // for the duration of the nested message loop, otherwise OBS could be closed
  // while this call is still on the stack.
  std::vector<HWND> disabled;
  for (HWND ancestor = owner; ancestor != nullptr && disabled.size() < 8;
       ancestor = GetWindow(ancestor, GW_OWNER)) {
    if (IsWindowEnabled(ancestor)) {
      EnableWindow(ancestor, FALSE);
      disabled.push_back(ancestor);
    }
  }
  SetFocus(GetDlgItem(picker, kPickerFilterId));

  MSG message = {};
  bool quit_received = false;
  while (!state.finished) {
    const BOOL result = GetMessageW(&message, nullptr, 0, 0);
    if (result <= 0) {
      quit_received = result == 0;
      break;
    }
    if (!IsDialogMessageW(picker, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }

  for (auto it = disabled.rbegin(); it != disabled.rend(); ++it) {
    EnableWindow(*it, TRUE);
  }
  if (owner && !quit_received) {
    SetActiveWindow(owner);
  }
  if (quit_received) {
    // The application is shutting down: let the real message loop see it too.
    PostQuitMessage(static_cast<int>(message.wParam));
  }

  windows = std::move(state.windows);
  return state.selected_index;
}

// ---------------------------------------------------------------------------
// Rule persistence
// ---------------------------------------------------------------------------

std::vector<std::string> SplitRuleParts(const std::string &value)
{
  std::vector<std::string> parts;
  std::stringstream stream(value);
  std::string part;
  while (std::getline(stream, part, '|')) {
    parts.push_back(AutoCaptureSource::Trim(std::move(part)));
  }
  return parts;
}

const AutoCaptureRule *FindRule(const std::vector<AutoCaptureRule> &rules, const std::string &id)
{
  if (id.empty()) {
    return nullptr;
  }
  const auto found = std::find_if(rules.begin(), rules.end(),
                                  [&id](const AutoCaptureRule &rule) { return rule.id == id; });
  return found == rules.end() ? nullptr : &(*found);
}

std::string MakeUniqueRuleId(const std::vector<AutoCaptureRule> &rules, const AutoCaptureRule &rule)
{
  const std::string base = AutoCaptureSource::RuleSettingSuffix(rule);
  std::string candidate = base;
  for (int attempt = 1; FindRule(rules, candidate) != nullptr; ++attempt) {
    candidate = base + "-" + std::to_string(attempt);
  }
  return candidate;
}

// Reads the rule list, transparently upgrading both legacy formats:
// the flat "process.exe | mode | scope | title" text box and the string-only
// array combined with per-rule "rule_enabled_<hash>" keys.
void LoadRules(obs_data_t *settings, std::vector<AutoCaptureRule> &rules, bool *needs_rewrite)
{
  rules.clear();
  bool rewrite = false;

  obs_data_array_t *items = obs_data_get_array(settings, kRulesSetting);
  if (items != nullptr) {
    const size_t count = obs_data_array_count(items);
    for (size_t i = 0; i < count; ++i) {
      obs_data_t *item = obs_data_array_item(items, i);
      AutoCaptureRule rule;
      const std::string process =
          AutoCaptureSource::ToLower(AutoCaptureSource::Trim(obs_data_get_string(item, "process")));
      if (!process.empty()) {
        rule.process_name = process;
        rule.id = obs_data_get_string(item, "id");
        rule.display_name = AutoCaptureSource::Trim(obs_data_get_string(item, "name"));
        rule.capture_mode = ParseCaptureModeToken(obs_data_get_string(item, "mode"));
        rule.fullscreen_only = AutoCaptureSource::ToLower(obs_data_get_string(item, "scope")) == kRuleScopeFullscreen;
        rule.title_contains = AutoCaptureSource::ToLower(AutoCaptureSource::Trim(obs_data_get_string(item, "title")));
        rule.enabled = !obs_data_has_user_value(item, "enabled") || obs_data_get_bool(item, "enabled");
        // Rules written before the blur existed get the sensible answer for
        // their process instead of a blanket "off".
        rule.blur_address_bar = obs_data_has_user_value(item, "blur") ? obs_data_get_bool(item, "blur")
                                                                      : IsBrowserProcess(process);
      } else if (AutoCaptureSource::ParseRuleString(obs_data_get_string(item, "value"), &rule, false)) {
        rule.id = AutoCaptureSource::RuleSettingSuffix(rule);
        const std::string enabled_key = "rule_enabled_" + rule.id;
        const std::string name_key = "rule_name_" + rule.id;
        rule.enabled = !obs_data_has_user_value(settings, enabled_key.c_str()) ||
                       obs_data_get_bool(settings, enabled_key.c_str());
        rule.display_name = AutoCaptureSource::Trim(obs_data_get_string(settings, name_key.c_str()));
        rule.blur_address_bar = IsBrowserProcess(rule.process_name);
        rewrite = true;
      } else {
        obs_data_release(item);
        continue;
      }
      if (rule.title_contains == kRuleWildcard) {
        rule.title_contains.clear();
      }
      if (rule.display_name.empty()) {
        rule.display_name = FriendlyProcessName(rule.process_name);
      }
      if (rule.id.empty() || FindRule(rules, rule.id) != nullptr) {
        rule.id = MakeUniqueRuleId(rules, rule);
        rewrite = true;
      }
      rules.push_back(std::move(rule));
      obs_data_release(item);
    }
    obs_data_array_release(items);
  }

  if (rules.empty()) {
    const std::string legacy = obs_data_get_string(settings, kLegacyTargetsSetting);
    if (!legacy.empty()) {
      const bool legacy_fullscreen_only = obs_data_get_bool(settings, kLegacyFullscreenOnlySetting);
      for (const std::string &line : AutoCaptureSource::SplitLines(legacy)) {
        AutoCaptureRule rule;
        if (!AutoCaptureSource::ParseRuleString(line, &rule, legacy_fullscreen_only)) {
          continue;
        }
        rule.display_name = FriendlyProcessName(rule.process_name);
        rule.blur_address_bar = IsBrowserProcess(rule.process_name);
        rule.id = MakeUniqueRuleId(rules, rule);
        rules.push_back(std::move(rule));
        rewrite = true;
      }
    }
  }

  if (needs_rewrite != nullptr) {
    *needs_rewrite = rewrite;
  }
}

void StoreRules(obs_data_t *settings, const std::vector<AutoCaptureRule> &rules)
{
  obs_data_array_t *array = obs_data_array_create();
  for (const AutoCaptureRule &rule : rules) {
    obs_data_t *item = obs_data_create();
    obs_data_set_string(item, "id", rule.id.c_str());
    obs_data_set_string(item, "name", rule.display_name.c_str());
    obs_data_set_string(item, "process", rule.process_name.c_str());
    obs_data_set_string(item, "mode", CaptureModeToken(rule.capture_mode));
    obs_data_set_string(item, "scope", rule.fullscreen_only ? kRuleScopeFullscreen : kRuleScopeAny);
    obs_data_set_string(item, "title", rule.title_contains.c_str());
    obs_data_set_bool(item, "enabled", rule.enabled);
    obs_data_set_bool(item, "blur", rule.blur_address_bar);
    // Kept so that an older build of the plugin still understands the list.
    obs_data_set_string(item, "value", AutoCaptureSource::SerializeRule(rule).c_str());
    obs_data_array_push_back(array, item);
    obs_data_release(item);
  }
  obs_data_set_array(settings, kRulesSetting, array);
  obs_data_array_release(array);
  obs_data_unset_user_value(settings, kLegacyTargetsSetting);
}

void LoadEditFields(obs_data_t *settings, const AutoCaptureRule &rule)
{
  obs_data_set_string(settings, kEditTargetSetting, rule.id.c_str());
  obs_data_set_bool(settings, kEditEnabledSetting, rule.enabled);
  obs_data_set_string(settings, kEditNameSetting, rule.display_name.c_str());
  obs_data_set_string(settings, kEditProcessSetting, rule.process_name.c_str());
  obs_data_set_string(settings, kEditModeSetting, CaptureModeToken(rule.capture_mode));
  obs_data_set_string(settings, kEditScopeSetting, rule.fullscreen_only ? kRuleScopeFullscreen : kRuleScopeAny);
  obs_data_set_string(settings, kEditTitleSetting, rule.title_contains.c_str());
  obs_data_set_bool(settings, kEditBlurSetting, rule.blur_address_bar);
}

void ClearEditFields(obs_data_t *settings)
{
  obs_data_set_string(settings, kEditTargetSetting, "");
  obs_data_set_bool(settings, kEditEnabledSetting, true);
  obs_data_set_string(settings, kEditNameSetting, "");
  obs_data_set_string(settings, kEditProcessSetting, "");
  obs_data_set_string(settings, kEditModeSetting, kModeAuto);
  obs_data_set_string(settings, kEditScopeSetting, kRuleScopeAny);
  obs_data_set_string(settings, kEditTitleSetting, "");
  obs_data_set_bool(settings, kEditBlurSetting, false);
}

// Copies the editor widgets back into the rule they belong to. Returns true
// when something actually changed.
bool ApplyEditFields(obs_data_t *settings, std::vector<AutoCaptureRule> &rules)
{
  const std::string target = obs_data_get_string(settings, kEditTargetSetting);
  if (target.empty()) {
    return false;
  }
  auto found = std::find_if(rules.begin(), rules.end(),
                            [&target](const AutoCaptureRule &rule) { return rule.id == target; });
  if (found == rules.end()) {
    return false;
  }
  AutoCaptureRule updated = *found;
  updated.enabled = obs_data_get_bool(settings, kEditEnabledSetting);
  updated.display_name = AutoCaptureSource::Trim(obs_data_get_string(settings, kEditNameSetting));
  updated.capture_mode = ParseCaptureModeToken(obs_data_get_string(settings, kEditModeSetting));
  updated.fullscreen_only =
      AutoCaptureSource::ToLower(obs_data_get_string(settings, kEditScopeSetting)) == kRuleScopeFullscreen;
  updated.title_contains =
      AutoCaptureSource::ToLower(AutoCaptureSource::Trim(obs_data_get_string(settings, kEditTitleSetting)));
  updated.blur_address_bar = obs_data_get_bool(settings, kEditBlurSetting);
  if (updated.title_contains == kRuleWildcard) {
    updated.title_contains.clear();
  }
  if (updated.display_name.empty()) {
    updated.display_name = FriendlyProcessName(updated.process_name);
  }
  if (updated.enabled == found->enabled && updated.display_name == found->display_name &&
      updated.capture_mode == found->capture_mode && updated.fullscreen_only == found->fullscreen_only &&
      updated.title_contains == found->title_contains) {
    return false;
  }
  *found = std::move(updated);
  return true;
}

std::string RuleListLabel(const AutoCaptureRule &rule)
{
  // Eye / crossed-out marker so the enabled state is visible in the list.
  std::string label = rule.enabled ? "\xE2\x97\x8F  " : "\xE2\x97\x8B  ";
  label += rule.display_name;
  label += "  (";
  label += rule.process_name;
  label += ")";
  if (!rule.enabled) {
    label += "  \xE2\x80\x94  ";
    label += Text("AutoAppCapture.Rule.Disabled");
  }
  return label;
}

obs_property_t *AddModeList(obs_properties_t *group, const char *setting, const char *description)
{
  obs_property_t *list =
      obs_properties_add_list(group, setting, description, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(list, Text("AutoAppCapture.Rule.Mode.Auto"), kModeAuto);
  obs_property_list_add_string(list, Text("AutoAppCapture.Rule.Mode.Window"), kModeWindow);
  obs_property_list_add_string(list, Text("AutoAppCapture.Rule.Mode.Game"), kModeGame);
  obs_property_set_long_description(list, Text("AutoAppCapture.Rule.Mode.Tooltip"));
  return list;
}

obs_property_t *AddScopeList(obs_properties_t *group, const char *setting, const char *description)
{
  obs_property_t *list =
      obs_properties_add_list(group, setting, description, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(list, Text("AutoAppCapture.Rule.Scope.Any"), kRuleScopeAny);
  obs_property_list_add_string(list, Text("AutoAppCapture.Rule.Scope.Fullscreen"), kRuleScopeFullscreen);
  return list;
}

// The property list must never be rebuilt from inside a property callback:
// obs_source_update_properties() makes the dialog destroy the very widget whose
// handler is running. Everything below therefore edits the existing properties
// object in place and lets OBS refresh the widgets afterwards.
void SetEditorVisible(obs_properties_t *props, bool has_rules)
{
  static const char *const editor_keys[] = {kEditEnabledSetting, kEditNameSetting,  kEditProcessSetting,
                                            kEditModeSetting,    kEditScopeSetting, kEditTitleSetting,
                                            kEditBlurSetting,    kDeleteRuleAction};
  for (const char *key : editor_keys) {
    if (obs_property_t *property = obs_properties_get(props, key)) {
      obs_property_set_visible(property, has_rules);
    }
  }
  if (obs_property_t *hint = obs_properties_get(props, kRulesEmptyHint)) {
    obs_property_set_visible(hint, !has_rules);
  }
}

// Unknown or missing values fall back to the capture section, so a settings
// file written by another version can never leave the panel empty.
std::string NormalizeSection(const std::string &value)
{
  if (value == kSectionMirror || value == kSectionBlur || value == kSectionAdvanced) {
    return value;
  }
  return kSectionCapture;
}

BlurMode ParseBlurMode(const std::string &value)
{
  if (value == kBlurModeMosaic) {
    return BlurMode::Mosaic;
  }
  if (value == kBlurModeFill) {
    return BlurMode::Fill;
  }
  return BlurMode::Frosted;
}

// Mirrors the technique choice libobs makes for a source that draws its own
// render target, so the blur cannot change how the frame is converted.
const char *BlurTechniqueName(enum gs_color_space current_space,
                              enum gs_color_space source_space,
                              float *multiplier)
{
  const char *technique = "Draw";
  *multiplier = 1.0f;

  switch (source_space) {
    case GS_CS_SRGB:
    case GS_CS_SRGB_16F:
      if (current_space == GS_CS_709_SCRGB) {
        technique = "DrawMultiply";
        *multiplier = obs_get_video_sdr_white_level() / 80.0f;
      }
      break;
    case GS_CS_709_EXTENDED:
      switch (current_space) {
        case GS_CS_SRGB:
        case GS_CS_SRGB_16F:
          technique = "DrawTonemap";
          break;
        case GS_CS_709_SCRGB:
          technique = "DrawMultiply";
          *multiplier = obs_get_video_sdr_white_level() / 80.0f;
          break;
        default:
          break;
      }
      break;
    case GS_CS_709_SCRGB:
      switch (current_space) {
        case GS_CS_SRGB:
        case GS_CS_SRGB_16F:
          technique = "DrawMultiplyTonemap";
          *multiplier = 80.0f / obs_get_video_sdr_white_level();
          break;
        case GS_CS_709_EXTENDED:
          technique = "DrawMultiply";
          *multiplier = 80.0f / obs_get_video_sdr_white_level();
          break;
        default:
          break;
      }
      break;
  }

  return technique;
}

void ApplySectionVisibility(obs_properties_t *props, const std::string &section)
{
  static const struct {
    const char *group;
    const char *section;
  } kGroupSections[] = {
      {kRulesGroup, kSectionCapture},
      {kAddGroup, kSectionCapture},
      {kMirrorGroup, kSectionMirror},
      {kBlurGroup, kSectionBlur},
      {kAdvancedGroup, kSectionAdvanced},
  };
  for (const auto &entry : kGroupSections) {
    if (obs_property_t *property = obs_properties_get(props, entry.group)) {
      obs_property_set_visible(property, section == entry.section);
    }
  }
}

// Hiding a group is enough: the dialog skips invisible properties entirely when
// it rebuilds its widgets, and returning true is what asks for that rebuild.
bool SectionModified(void *, obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
  ApplySectionVisibility(props, NormalizeSection(obs_data_get_string(settings, kUiSectionSetting)));
  return true;
}

// Cell size means nothing for a solid fill, and the colour means nothing for
// the other two modes.
void ApplyBlurModeVisibility(obs_properties_t *props, BlurMode mode)
{
  if (obs_property_t *strength = obs_properties_get(props, kBlurStrengthSetting)) {
    obs_property_set_visible(strength, mode != BlurMode::Fill);
  }
  if (obs_property_t *color = obs_properties_get(props, kBlurFillColorSetting)) {
    obs_property_set_visible(color, mode == BlurMode::Fill);
  }
}

void RefillRuleList(obs_properties_t *props, const std::vector<AutoCaptureRule> &rules)
{
  obs_property_t *list = obs_properties_get(props, kSelectedRuleSetting);
  if (list == nullptr) {
    return;
  }
  obs_property_list_clear(list);
  for (const AutoCaptureRule &rule : rules) {
    obs_property_list_add_string(list, RuleListLabel(rule).c_str(), rule.id.c_str());
  }
  if (rules.empty()) {
    obs_property_list_add_string(list, Text("AutoAppCapture.Rules.Empty"), "");
  }
  obs_property_set_enabled(list, !rules.empty());
  SetEditorVisible(props, !rules.empty());
}

void RefillRunningList(obs_properties_t *props)
{
  obs_property_t *list = obs_properties_get(props, kNewProcessSetting);
  if (list == nullptr) {
    return;
  }
  const std::vector<ProcessOption> processes = CollectProcesses(CollectWindows());
  obs_property_list_clear(list);
  for (const ProcessOption &option : processes) {
    obs_property_list_add_string(list, option.label.c_str(), option.process_name.c_str());
  }
  if (processes.empty()) {
    obs_property_list_add_string(list, Text("AutoAppCapture.Add.NoRunning"), "");
  }
}

bool SelectedRuleModified(void *, obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
  std::vector<AutoCaptureRule> rules;
  bool rewrite = false;
  LoadRules(settings, rules, &rewrite);
  if (ApplyEditFields(settings, rules) || rewrite) {
    StoreRules(settings, rules);
  }
  const std::string selected = obs_data_get_string(settings, kSelectedRuleSetting);
  if (const AutoCaptureRule *rule = FindRule(rules, selected)) {
    LoadEditFields(settings, *rule);
  } else {
    ClearEditFields(settings);
  }
  RefillRuleList(props, rules);
  return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Source implementation
// ---------------------------------------------------------------------------

AutoCaptureSource::AutoCaptureSource(obs_data_t *settings, obs_source_t *source) : source_(source)
{
  EnsureCaptureSources();
  Update(settings);
}

AutoCaptureSource::~AutoCaptureSource()
{
  // Joined before anything else: the worker must not observe a half destroyed
  // source, and it never touches libobs itself.
  address_bar_locator_.Stop();
  ResetCaptureSources();
  ReleaseBlurResources();
}

void AutoCaptureSource::Update(obs_data_t *settings)
{
  // libobs defers updates of video sources to the graphics thread, so this
  // callback must only READ the settings object. Inserting or removing items
  // here would race with the UI thread walking the very same obs_data while a
  // scene collection is loaded or saved. Every write lives in the property
  // callbacks below, which run on the UI thread.
  std::vector<AutoCaptureRule> rules;
  LoadRules(settings, rules, nullptr);

  const bool mirror_enabled = obs_data_get_bool(settings, kMirrorEnabledSetting);
  std::vector<std::string> mirror_rules;
  for (std::string line : SplitLines(obs_data_get_string(settings, kMirrorTitleRulesSetting))) {
    line = ToLower(Trim(std::move(line)));
    if (!line.empty()) {
      mirror_rules.push_back(std::move(line));
    }
  }
  float poll_interval = static_cast<float>(obs_data_get_double(settings, kPollIntervalSetting));
  if (poll_interval < 0.1f) {
    poll_interval = 0.1f;
  }

  BlurSettings blur;
  blur.enabled = obs_data_get_bool(settings, kBlurEnabledSetting);
  blur.auto_detect = std::string(obs_data_get_string(settings, kBlurDetectSetting)) != kBlurDetectManual;
  blur.padding_pixels = static_cast<int>(obs_data_get_int(settings, kBlurPaddingSetting));
  if (blur.padding_pixels < 0) {
    blur.padding_pixels = 0;
  }
  blur.mode = ParseBlurMode(obs_data_get_string(settings, kBlurModeSetting));
  blur.cell_pixels = static_cast<int>(obs_data_get_int(settings, kBlurStrengthSetting));
  if (blur.cell_pixels < 2) {
    blur.cell_pixels = 2;
  }
  blur.fill_color = static_cast<uint32_t>(obs_data_get_int(settings, kBlurFillColorSetting));
  const auto zone_fraction = [settings](const char *setting) {
    double percent = obs_data_get_double(settings, setting);
    percent = percent < 0.0 ? 0.0 : (percent > 100.0 ? 100.0 : percent);
    return static_cast<float>(percent / 100.0);
  };
  blur.fallback_zone.left = zone_fraction(kBlurZoneLeftSetting);
  blur.fallback_zone.top = zone_fraction(kBlurZoneTopSetting);
  blur.fallback_zone.right = std::min(1.0f, blur.fallback_zone.left + zone_fraction(kBlurZoneWidthSetting));
  blur.fallback_zone.bottom = std::min(1.0f, blur.fallback_zone.top + zone_fraction(kBlurZoneHeightSetting));

  rules_ = std::move(rules);
  target_processes_.clear();
  enabled_rule_count_ = 0;
  for (const AutoCaptureRule &rule : rules_) {
    if (!rule.enabled) {
      continue;
    }
    ++enabled_rule_count_;
    target_processes_.insert(rule.process_name);
  }
  mirror_enabled_ = mirror_enabled;
  mirror_title_rules_ = std::move(mirror_rules);
  poll_interval_seconds_ = poll_interval;
  blur_ = blur;

  // Editing the list shifts rule indexes, so the active rule is looked up by
  // its id again instead of dropping the capture that is already running.
  active_rule_index_ = kInvalidRuleIndex;
  for (size_t i = 0; i < rules_.size(); ++i) {
    if (rules_[i].id == active_rule_id_ && rules_[i].enabled) {
      active_rule_index_ = i;
      break;
    }
  }
  if (enabled_rule_count_ == 0 || active_rule_index_ == kInvalidRuleIndex) {
    ClearActiveMatch();
  }

  PollActiveWindow();
  SyncCaptureSources();
  UpdateRenderBackend(SelectRenderSource());
  SyncAddressBarLocator();
  status_text_ = BuildStatusText();
}

void AutoCaptureSource::Tick(float seconds)
{
  elapsed_seconds_ += seconds;
  if (elapsed_seconds_ < poll_interval_seconds_) {
    return;
  }
  elapsed_seconds_ = 0.0f;
  PollActiveWindow();
  SyncCaptureSources();
  UpdateRenderBackend(SelectRenderSource());
  SyncAddressBarLocator();
  status_text_ = BuildStatusText();
}

const char *AutoCaptureSource::GetName(void *)
{
  return Text("AutoAppCapture.Source");
}

void *AutoCaptureSource::Create(obs_data_t *settings, obs_source_t *source)
{
  return new AutoCaptureSource(settings, source);
}

void AutoCaptureSource::Destroy(void *data)
{
  delete static_cast<AutoCaptureSource *>(data);
}

void AutoCaptureSource::UpdateCallback(void *data, obs_data_t *settings)
{
  static_cast<AutoCaptureSource *>(data)->Update(settings);
}

void AutoCaptureSource::TickCallback(void *data, float seconds)
{
  static_cast<AutoCaptureSource *>(data)->Tick(seconds);
}

bool AutoCaptureSource::EnsureBlurEffect()
{
  if (blur_effect_ != nullptr) {
    return true;
  }
  if (blur_effect_failed_) {
    return false;
  }

  char *path = obs_module_file(kBlurEffectFile);
  if (path == nullptr) {
    blur_effect_failed_ = true;
    blog(LOG_ERROR, "[obs-auto-capture] Blur effect '%s' is missing from the plugin data folder.", kBlurEffectFile);
    return false;
  }

  char *error = nullptr;
  blur_effect_ = gs_effect_create_from_file(path, &error);
  bfree(path);
  if (blur_effect_ == nullptr) {
    blur_effect_failed_ = true;
    blog(LOG_ERROR, "[obs-auto-capture] Failed to compile the blur effect: %s", error != nullptr ? error : "unknown");
  }
  bfree(error);
  return blur_effect_ != nullptr;
}

void AutoCaptureSource::ReleaseBlurResources()
{
  if (blur_effect_ == nullptr && blur_texrender_ == nullptr) {
    return;
  }
  // The graphics context is recursive, so this is also safe when the source is
  // destroyed from the graphics thread itself.
  obs_enter_graphics();
  if (blur_texrender_ != nullptr) {
    gs_texrender_destroy(blur_texrender_);
    blur_texrender_ = nullptr;
  }
  if (blur_effect_ != nullptr) {
    gs_effect_destroy(blur_effect_);
    blur_effect_ = nullptr;
  }
  obs_leave_graphics();
}

// Only the app that is being captured right now decides this: a browser rule
// must not blur the frame of the game the source switched to afterwards.
bool AutoCaptureSource::BlurAppliesToActiveRule() const
{
  if (!blur_.enabled) {
    return false;
  }
  const AutoCaptureRule *rule = GetActiveRule();
  return rule != nullptr && rule->blur_address_bar;
}

void AutoCaptureSource::SyncAddressBarLocator()
{
  // Game capture hands over a frame whose geometry has nothing to do with the
  // window rectangle, so the address bar cannot be mapped into it.
  const bool wanted = blur_.enabled && blur_.auto_detect && BlurAppliesToActiveRule() &&
                      active_backend_ == AutoCaptureBackend::WindowCapture && active_window_ != nullptr;
  address_bar_locator_.SetTarget(wanted ? active_window_ : nullptr, wanted);
}

// The capture starts at the window origin libobs-winrt uses, so the same
// rectangle is what turns a screen coordinate into a frame coordinate. When
// neither candidate matches the frame that actually arrived, the mapping is
// refused rather than guessed: a zone in the wrong place is worse than a zone
// that is too big.
bool AutoCaptureSource::MapScreenRectToCapture(const RECT &screen,
                                               uint32_t cx,
                                               uint32_t cy,
                                               BlurZone *zone) const
{
  if (active_window_ == nullptr || !IsWindow(active_window_) || cx == 0 || cy == 0) {
    return false;
  }

  POINT origin = {0, 0};
  RECT bounds = {0, 0, 0, 0};
  bool matched = false;
  if (SUCCEEDED(DwmGetWindowAttribute(active_window_, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) &&
      static_cast<uint32_t>(bounds.right - bounds.left) == cx &&
      static_cast<uint32_t>(bounds.bottom - bounds.top) == cy) {
    origin.x = bounds.left;
    origin.y = bounds.top;
    matched = true;
  }
  if (!matched) {
    RECT window_rect = {0, 0, 0, 0};
    if (GetWindowRect(active_window_, &window_rect) &&
        static_cast<uint32_t>(window_rect.right - window_rect.left) == cx &&
        static_cast<uint32_t>(window_rect.bottom - window_rect.top) == cy) {
      origin.x = window_rect.left;
      origin.y = window_rect.top;
      matched = true;
    }
  }
  if (!matched) {
    return false;
  }

  const float padding = static_cast<float>(blur_.padding_pixels);
  const float left = static_cast<float>(screen.left - origin.x) - padding;
  const float top = static_cast<float>(screen.top - origin.y) - padding;
  const float right = static_cast<float>(screen.right - origin.x) + padding;
  const float bottom = static_cast<float>(screen.bottom - origin.y) + padding;

  const auto clamp01 = [](float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); };
  zone->left = clamp01(left / static_cast<float>(cx));
  zone->top = clamp01(top / static_cast<float>(cy));
  zone->right = clamp01(right / static_cast<float>(cx));
  zone->bottom = clamp01(bottom / static_cast<float>(cy));
  return zone->Valid();
}

size_t AutoCaptureSource::BuildBlurZones(BlurZone *zones, size_t capacity, uint32_t cx, uint32_t cy) const
{
  if (capacity == 0 || !BlurAppliesToActiveRule()) {
    address_bar_status_ = AddressBarStatus::Idle;
    return 0;
  }

  if (blur_.auto_detect) {
    const AddressBarRect detected = address_bar_locator_.GetResult();
    address_bar_status_ = detected.status;
    BlurZone zone;
    if (detected.valid && MapScreenRectToCapture(detected.screen, cx, cy, &zone)) {
      zones[0] = zone;
      return 1;
    }
  } else {
    address_bar_status_ = AddressBarStatus::Idle;
  }

  // Nothing was located, or the frame does not match the window any more.
  // Never return zero here: switching the blur off is the user's decision, not
  // a consequence of a failed lookup.
  if (blur_.fallback_zone.Valid()) {
    zones[0] = blur_.fallback_zone;
    return 1;
  }
  return 0;
}

// Returns false whenever the frame could not be drawn through the effect, so
// the caller renders the child directly instead of showing nothing.
bool AutoCaptureSource::RenderObscured(obs_source_t *child)
{
  const uint32_t cx = obs_source_get_width(child);
  const uint32_t cy = obs_source_get_height(child);
  if (cx == 0 || cy == 0) {
    return false;
  }

  BlurZone zones[kMaxBlurZones];
  const size_t zone_count = BuildBlurZones(zones, kMaxBlurZones, cx, cy);
  if (zone_count == 0) {
    return false;
  }
  if (!EnsureBlurEffect()) {
    return false;
  }

  const enum gs_color_space preferred_spaces[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED};
  const enum gs_color_space space =
      obs_source_get_color_space(child, OBS_COUNTOF(preferred_spaces), preferred_spaces);
  const enum gs_color_format format = gs_get_format_from_space(space);

  if (blur_texrender_ != nullptr && gs_texrender_get_format(blur_texrender_) != format) {
    gs_texrender_destroy(blur_texrender_);
    blur_texrender_ = nullptr;
  }
  if (blur_texrender_ == nullptr) {
    blur_texrender_ = gs_texrender_create(format, GS_ZS_NONE);
    if (blur_texrender_ == nullptr) {
      return false;
    }
  }
  gs_texrender_reset(blur_texrender_);

  // gs_texrender_begin resets the matrix, so the mirror transform of the caller
  // does not leak into the child and is applied to the finished frame instead.
  gs_blend_state_push();
  gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
  const bool captured = gs_texrender_begin_with_color_space(blur_texrender_, cx, cy, space);
  if (captured) {
    struct vec4 clear_color;
    vec4_zero(&clear_color);
    gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
    gs_ortho(0.0f, static_cast<float>(cx), 0.0f, static_cast<float>(cy), -100.0f, 100.0f);
    obs_source_video_render(child);
    gs_texrender_end(blur_texrender_);
  }
  gs_blend_state_pop();
  if (!captured) {
    return false;
  }

  gs_texture_t *texture = gs_texrender_get_texture(blur_texrender_);
  if (texture == nullptr) {
    return false;
  }

  float multiplier = 1.0f;
  const char *technique = BlurTechniqueName(gs_get_color_space(), space, &multiplier);

  struct vec4 zone_values[kMaxBlurZones];
  for (size_t i = 0; i < kMaxBlurZones; ++i) {
    if (i < zone_count) {
      vec4_set(&zone_values[i], zones[i].left, zones[i].top, zones[i].right, zones[i].bottom);
    } else {
      vec4_zero(&zone_values[i]);
    }
  }

  struct vec2 cell;
  const float cell_pixels = static_cast<float>(blur_.cell_pixels);
  vec2_set(&cell, cell_pixels / static_cast<float>(cx), cell_pixels / static_cast<float>(cy));

  struct vec4 fill;
  vec4_from_rgba_srgb(&fill, blur_.fill_color);
  fill.w = 1.0f;

  static const char *const kZoneParams[kMaxBlurZones] = {"zone0", "zone1", "zone2", "zone3"};

  gs_effect_set_texture_srgb(gs_effect_get_param_by_name(blur_effect_, "image"), texture);
  gs_effect_set_float(gs_effect_get_param_by_name(blur_effect_, "multiplier"), multiplier);
  for (size_t i = 0; i < kMaxBlurZones; ++i) {
    gs_effect_set_vec4(gs_effect_get_param_by_name(blur_effect_, kZoneParams[i]), &zone_values[i]);
  }
  gs_effect_set_int(gs_effect_get_param_by_name(blur_effect_, "zone_count"), static_cast<int>(zone_count));
  gs_effect_set_vec2(gs_effect_get_param_by_name(blur_effect_, "cell_size"), &cell);
  gs_effect_set_int(gs_effect_get_param_by_name(blur_effect_, "obscure_mode"), static_cast<int>(blur_.mode));
  gs_effect_set_vec4(gs_effect_get_param_by_name(blur_effect_, "fill_color"), &fill);

  const bool previous_srgb = gs_framebuffer_srgb_enabled();
  gs_enable_framebuffer_srgb(true);
  while (gs_effect_loop(blur_effect_, technique)) {
    gs_draw_sprite(texture, 0, cx, cy);
  }
  gs_enable_framebuffer_srgb(previous_srgb);
  return true;
}

void AutoCaptureSource::VideoRenderCallback(void *data, gs_effect_t *effect)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (instance != nullptr) {
    obs_source_t *selected = instance->SelectRenderSource();
    if (selected != nullptr) {
      const bool mirror = instance->ShouldMirror();
      if (mirror) {
        gs_matrix_push();
        gs_matrix_scale3f(-1.0f, 1.0f, 1.0f);
        gs_matrix_translate3f(-static_cast<float>(obs_source_get_width(selected)), 0.0f, 0.0f);
      }
      if (!instance->RenderObscured(selected)) {
        obs_source_video_render(selected);
      }
      if (mirror) {
        gs_matrix_pop();
      }
    }
  }
  UNUSED_PARAMETER(effect);
}

enum gs_color_space AutoCaptureSource::GetColorSpaceCallback(void *data,
                                                             size_t count,
                                                             const enum gs_color_space *preferred_spaces)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (instance != nullptr) {
    if (obs_source_t *selected = instance->SelectRenderSource()) {
      return obs_source_get_color_space(selected, count, preferred_spaces);
    }
  }
  return count > 0 ? preferred_spaces[0] : GS_CS_SRGB;
}

void AutoCaptureSource::EnumActiveSourcesCallback(void *data, obs_source_enum_proc_t cb, void *param)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (!instance) {
    return;
  }
  if (instance->window_capture_source_ != nullptr) {
    cb(instance->source_, instance->window_capture_source_, param);
  }
  if (instance->game_capture_source_ != nullptr) {
    cb(instance->source_, instance->game_capture_source_, param);
  }
}

// Editing a field of the selected rule writes it straight back into the stored
// list. This runs on the UI thread, which is the only place the settings object
// may be modified.
bool AutoCaptureSource::RuleFieldModified(void *data,
                                          obs_properties_t *,
                                          obs_property_t *,
                                          obs_data_t *settings)
{
  std::vector<AutoCaptureRule> rules;
  bool rewrite = false;
  LoadRules(settings, rules, &rewrite);
  if (!ApplyEditFields(settings, rules) && !rewrite) {
    return false;
  }
  StoreRules(settings, rules);
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (instance != nullptr && instance->source_ != nullptr) {
    obs_source_update(instance->source_, settings);
  }
  // No widget refresh: it would recreate the field being typed into.
  return false;
}

#ifdef AUTO_CAPTURE_HAS_UI
// Property callbacks run on the UI thread, which is the only thread allowed to
// touch Qt. Opening the window from anywhere else would be a crash waiting to
// happen.
bool AutoCaptureSource::OpenSettingsClicked(obs_properties_t *, obs_property_t *, void *data)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (instance != nullptr && instance->source_ != nullptr) {
    auto_capture_open_settings(instance->source_);
  }
  return false;
}
#endif

bool AutoCaptureSource::BlurModeModified(void *,
                                         obs_properties_t *props,
                                         obs_property_t *,
                                         obs_data_t *settings)
{
  ApplyBlurModeVisibility(props, ParseBlurMode(obs_data_get_string(settings, kBlurModeSetting)));
  return true;
}

// Toggling the checkbox also changes the marker in front of the rule name, so
// the list entries are refreshed in place.
bool AutoCaptureSource::RuleEnabledModified(void *data,
                                            obs_properties_t *props,
                                            obs_property_t *property,
                                            obs_data_t *settings)
{
  RuleFieldModified(data, props, property, settings);
  std::vector<AutoCaptureRule> rules;
  LoadRules(settings, rules, nullptr);
  RefillRuleList(props, rules);
  return true;
}

bool AutoCaptureSource::RefreshClicked(obs_properties_t *props, obs_property_t *, void *data)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (!instance || !instance->source_) {
    return false;
  }
  obs_data_t *settings = obs_source_get_settings(instance->source_);
  if (settings != nullptr) {
    obs_data_set_string(settings, kStatusTextSetting, instance->GetStatusText().c_str());
    std::vector<AutoCaptureRule> rules;
    LoadRules(settings, rules, nullptr);
    RefillRuleList(props, rules);
    obs_data_release(settings);
  }
  RefillRunningList(props);
  return true;
}

bool AutoCaptureSource::OpenWindowPickerClicked(obs_properties_t *props, obs_property_t *, void *data)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (!instance || !instance->source_) {
    return false;
  }
  std::vector<WindowOption> windows = CollectWindows();
  const int selected = ShowWindowPicker(windows);
  if (selected < 0 || static_cast<size_t>(selected) >= windows.size()) {
    return false;
  }
  const WindowOption &choice = windows[static_cast<size_t>(selected)];

  obs_data_t *settings = obs_source_get_settings(instance->source_);
  if (!settings) {
    return false;
  }
  obs_data_set_string(settings, kNewProcessSetting, choice.process_name.c_str());
  if (Trim(obs_data_get_string(settings, kNewNameSetting)).empty()) {
    obs_data_set_string(settings, kNewNameSetting, FriendlyProcessName(choice.process_name).c_str());
  }
  obs_source_update(instance->source_, settings);
  obs_data_release(settings);

  blog(LOG_INFO, "[obs-auto-capture] Picked window: %s", choice.label.c_str());
  RefillRunningList(props);
  return true;
}

bool AutoCaptureSource::AddRuleClicked(obs_properties_t *props, obs_property_t *, void *data)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (!instance || !instance->source_) {
    return false;
  }
  obs_data_t *settings = obs_source_get_settings(instance->source_);
  if (!settings) {
    return false;
  }

  std::vector<AutoCaptureRule> rules;
  bool rewrite = false;
  LoadRules(settings, rules, &rewrite);
  ApplyEditFields(settings, rules);

  std::string process = ToLower(Trim(obs_data_get_string(settings, kNewProcessSetting)));
  if (process.empty()) {
    const std::vector<ProcessOption> processes = CollectProcesses(CollectWindows());
    if (!processes.empty()) {
      process = processes.front().process_name;
    }
  }
  if (process.empty()) {
    blog(LOG_WARNING, "[obs-auto-capture] No application selected, nothing to add.");
    obs_data_release(settings);
    return false;
  }

  AutoCaptureRule rule;
  rule.process_name = process;
  rule.display_name = Trim(obs_data_get_string(settings, kNewNameSetting));
  if (rule.display_name.empty()) {
    rule.display_name = FriendlyProcessName(process);
  }
  rule.capture_mode = ParseCaptureModeToken(obs_data_get_string(settings, kNewModeSetting));
  rule.fullscreen_only = ToLower(obs_data_get_string(settings, kNewScopeSetting)) == kRuleScopeFullscreen;
  rule.title_contains = ToLower(Trim(obs_data_get_string(settings, kNewTitleSetting)));
  if (rule.title_contains == kRuleWildcard) {
    rule.title_contains.clear();
  }
  rule.blur_address_bar = IsBrowserProcess(process);

  const auto duplicate = std::find_if(rules.begin(), rules.end(), [&rule](const AutoCaptureRule &existing) {
    return existing.process_name == rule.process_name && existing.capture_mode == rule.capture_mode &&
           existing.fullscreen_only == rule.fullscreen_only && existing.title_contains == rule.title_contains;
  });
  std::string selected_id;
  if (duplicate != rules.end()) {
    selected_id = duplicate->id;
    blog(LOG_INFO, "[obs-auto-capture] Rule for '%s' already exists.", rule.process_name.c_str());
  } else {
    rule.id = MakeUniqueRuleId(rules, rule);
    selected_id = rule.id;
    rules.push_back(rule);
    blog(LOG_INFO, "[obs-auto-capture] Added rule '%s'.", SerializeRule(rule).c_str());
  }

  StoreRules(settings, rules);
  obs_data_set_string(settings, kSelectedRuleSetting, selected_id.c_str());
  if (const AutoCaptureRule *stored = FindRule(rules, selected_id)) {
    LoadEditFields(settings, *stored);
  }
  // Leave the "add" section ready for the next application.
  obs_data_set_string(settings, kNewNameSetting, "");
  obs_data_set_string(settings, kNewTitleSetting, "");

  obs_source_update(instance->source_, settings);
  obs_data_release(settings);
  RefillRuleList(props, rules);
  return true;
}

bool AutoCaptureSource::DeleteRuleClicked(obs_properties_t *props, obs_property_t *, void *data)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (!instance || !instance->source_) {
    return false;
  }
  obs_data_t *settings = obs_source_get_settings(instance->source_);
  if (!settings) {
    return false;
  }

  std::vector<AutoCaptureRule> rules;
  bool rewrite = false;
  LoadRules(settings, rules, &rewrite);
  const std::string selected = obs_data_get_string(settings, kSelectedRuleSetting);
  const size_t before = rules.size();
  rules.erase(std::remove_if(rules.begin(), rules.end(),
                             [&selected](const AutoCaptureRule &rule) { return rule.id == selected; }),
              rules.end());
  if (rules.size() == before && !rewrite) {
    obs_data_release(settings);
    return false;
  }

  StoreRules(settings, rules);
  const std::string next = rules.empty() ? std::string() : rules.front().id;
  obs_data_set_string(settings, kSelectedRuleSetting, next.c_str());
  if (const AutoCaptureRule *rule = FindRule(rules, next)) {
    LoadEditFields(settings, *rule);
  } else {
    ClearEditFields(settings);
  }

  obs_source_update(instance->source_, settings);
  obs_data_release(settings);
  blog(LOG_INFO, "[obs-auto-capture] Removed rule '%s'.", selected.c_str());
  RefillRuleList(props, rules);
  return true;
}

obs_properties_t *AutoCaptureSource::GetProperties(void *data)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  const std::vector<WindowOption> windows = CollectWindows();
  const std::vector<ProcessOption> processes = CollectProcesses(windows);

  std::vector<AutoCaptureRule> rules;
  std::string selected_id;
  std::string section = kSectionCapture;
  obs_data_t *settings = instance != nullptr && instance->source_ != nullptr
                             ? obs_source_get_settings(instance->source_)
                             : nullptr;
  if (settings != nullptr) {
    bool rewrite = false;
    LoadRules(settings, rules, &rewrite);
    if (rewrite) {
      StoreRules(settings, rules);
    }
    selected_id = obs_data_get_string(settings, kSelectedRuleSetting);
    if (FindRule(rules, selected_id) == nullptr) {
      selected_id = rules.empty() ? std::string() : rules.front().id;
      obs_data_set_string(settings, kSelectedRuleSetting, selected_id.c_str());
    }
    if (const AutoCaptureRule *rule = FindRule(rules, selected_id)) {
      LoadEditFields(settings, *rule);
    } else {
      ClearEditFields(settings);
    }
    if (Trim(obs_data_get_string(settings, kNewProcessSetting)).empty() && !processes.empty()) {
      obs_data_set_string(settings, kNewProcessSetting, processes.front().process_name.c_str());
    }
    obs_data_set_string(settings, kStatusTextSetting, instance->GetStatusText().c_str());
    section = NormalizeSection(obs_data_get_string(settings, kUiSectionSetting));
    obs_data_set_string(settings, kUiSectionSetting, section.c_str());
  }

  obs_properties_t *properties = obs_properties_create();

  // --- Status -------------------------------------------------------------
  obs_property_t *status = obs_properties_add_text(properties, kStatusTextSetting, Text("AutoAppCapture.Status"),
                                                   OBS_TEXT_INFO);
  obs_property_text_set_info_type(status, rules.empty() ? OBS_TEXT_INFO_WARNING : OBS_TEXT_INFO_NORMAL);
  obs_property_text_set_info_word_wrap(status, true);
  obs_properties_add_button2(properties, kRefreshAction, Text("AutoAppCapture.Actions.Refresh"),
                             AutoCaptureSource::RefreshClicked, data);

#ifdef AUTO_CAPTURE_HAS_UI
  // The sections below stay where they are until the window can replace them.
  // Removing them first would leave the plugin unconfigurable for anyone whose
  // OBS cannot open the window.
  obs_properties_add_button2(properties, kOpenSettingsAction, Text("AutoAppCapture.Actions.Configure"),
                             AutoCaptureSource::OpenSettingsClicked, data);
#endif

  // --- Section selector ---------------------------------------------------
  obs_property_t *section_list = obs_properties_add_list(properties, kUiSectionSetting, Text("AutoAppCapture.Section"),
                                                         OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(section_list, Text("AutoAppCapture.Section.Capture"), kSectionCapture);
  obs_property_list_add_string(section_list, Text("AutoAppCapture.Section.Mirror"), kSectionMirror);
  obs_property_list_add_string(section_list, Text("AutoAppCapture.Section.Blur"), kSectionBlur);
  obs_property_list_add_string(section_list, Text("AutoAppCapture.Section.Advanced"), kSectionAdvanced);
  obs_property_set_long_description(section_list, Text("AutoAppCapture.Section.Tooltip"));
  obs_property_set_modified_callback2(section_list, SectionModified, data);

  // --- Added applications -------------------------------------------------
  obs_properties_t *rules_group = obs_properties_create();
  obs_property_t *rule_list = obs_properties_add_list(rules_group, kSelectedRuleSetting,
                                                      Text("AutoAppCapture.Rules.Selected"), OBS_COMBO_TYPE_LIST,
                                                      OBS_COMBO_FORMAT_STRING);
  obs_property_set_modified_callback2(rule_list, SelectedRuleModified, data);
  // Every property is always created; only visibility changes, because the
  // property set may not be rebuilt while the dialog is open.
  for (const AutoCaptureRule &rule : rules) {
    obs_property_list_add_string(rule_list, RuleListLabel(rule).c_str(), rule.id.c_str());
  }
  if (rules.empty()) {
    obs_property_list_add_string(rule_list, Text("AutoAppCapture.Rules.Empty"), "");
  }
  obs_property_set_enabled(rule_list, !rules.empty());

  obs_property_t *hint = obs_properties_add_text(rules_group, kRulesEmptyHint, "", OBS_TEXT_INFO);
  obs_property_set_long_description(hint, Text("AutoAppCapture.Rules.EmptyHint"));
  obs_property_text_set_info_word_wrap(hint, true);

  obs_property_t *enabled = obs_properties_add_bool(rules_group, kEditEnabledSetting,
                                                    Text("AutoAppCapture.Rule.Enabled"));
  obs_property_set_modified_callback2(enabled, AutoCaptureSource::RuleEnabledModified, data);
  obs_property_t *name = obs_properties_add_text(rules_group, kEditNameSetting, Text("AutoAppCapture.Rule.Name"),
                                                 OBS_TEXT_DEFAULT);
  obs_property_set_modified_callback2(name, AutoCaptureSource::RuleFieldModified, data);
  obs_property_t *process = obs_properties_add_text(rules_group, kEditProcessSetting,
                                                    Text("AutoAppCapture.Rule.Process"), OBS_TEXT_DEFAULT);
  obs_property_set_enabled(process, false);
  obs_property_t *mode = AddModeList(rules_group, kEditModeSetting, Text("AutoAppCapture.Rule.Mode"));
  obs_property_set_modified_callback2(mode, AutoCaptureSource::RuleFieldModified, data);
  obs_property_t *scope = AddScopeList(rules_group, kEditScopeSetting, Text("AutoAppCapture.Rule.Scope"));
  obs_property_set_modified_callback2(scope, AutoCaptureSource::RuleFieldModified, data);
  obs_property_t *title = obs_properties_add_text(rules_group, kEditTitleSetting, Text("AutoAppCapture.Rule.Title"),
                                                  OBS_TEXT_DEFAULT);
  obs_property_set_long_description(title, Text("AutoAppCapture.Rule.Title.Tooltip"));
  obs_property_set_modified_callback2(title, AutoCaptureSource::RuleFieldModified, data);
  obs_property_t *rule_blur = obs_properties_add_bool(rules_group, kEditBlurSetting, Text("AutoAppCapture.Rule.Blur"));
  obs_property_set_long_description(rule_blur, Text("AutoAppCapture.Rule.Blur.Tooltip"));
  obs_property_set_modified_callback2(rule_blur, AutoCaptureSource::RuleFieldModified, data);
  obs_properties_add_button2(rules_group, kDeleteRuleAction, Text("AutoAppCapture.Actions.Delete"),
                             AutoCaptureSource::DeleteRuleClicked, data);
  obs_properties_add_group(properties, kRulesGroup, Text("AutoAppCapture.Group.Rules"), OBS_GROUP_NORMAL,
                           rules_group);
  SetEditorVisible(properties, !rules.empty());

  // --- Add an application -------------------------------------------------
  obs_properties_t *add_group = obs_properties_create();
  obs_properties_add_button2(add_group, kPickWindowAction, Text("AutoAppCapture.Actions.Pick"),
                             AutoCaptureSource::OpenWindowPickerClicked, data);
  obs_property_t *running = obs_properties_add_list(add_group, kNewProcessSetting, Text("AutoAppCapture.Add.Running"),
                                                    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  for (const ProcessOption &option : processes) {
    obs_property_list_add_string(running, option.label.c_str(), option.process_name.c_str());
  }
  if (processes.empty()) {
    obs_property_list_add_string(running, Text("AutoAppCapture.Add.NoRunning"), "");
  }
  obs_properties_add_text(add_group, kNewNameSetting, Text("AutoAppCapture.Add.Name"), OBS_TEXT_DEFAULT);
  AddModeList(add_group, kNewModeSetting, Text("AutoAppCapture.Rule.Mode"));
  AddScopeList(add_group, kNewScopeSetting, Text("AutoAppCapture.Rule.Scope"));
  obs_property_t *new_title = obs_properties_add_text(add_group, kNewTitleSetting, Text("AutoAppCapture.Rule.Title"),
                                                      OBS_TEXT_DEFAULT);
  obs_property_set_long_description(new_title, Text("AutoAppCapture.Rule.Title.Tooltip"));
  obs_properties_add_button2(add_group, kAddRuleAction, Text("AutoAppCapture.Actions.Add"),
                             AutoCaptureSource::AddRuleClicked, data);
  obs_properties_add_group(properties, kAddGroup, Text("AutoAppCapture.Group.Add"), OBS_GROUP_NORMAL, add_group);

  // --- Mirroring ----------------------------------------------------------
  obs_properties_t *mirror_group = obs_properties_create();
  obs_properties_add_bool(mirror_group, kMirrorEnabledSetting, Text("AutoAppCapture.Mirror.Enabled"));
  obs_property_t *mirror_rules = obs_properties_add_text(mirror_group, kMirrorTitleRulesSetting,
                                                         Text("AutoAppCapture.Mirror.Titles"), OBS_TEXT_MULTILINE);
  obs_property_set_long_description(mirror_rules, Text("AutoAppCapture.Mirror.Titles.Tooltip"));
  obs_properties_add_group(properties, kMirrorGroup, Text("AutoAppCapture.Group.Mirror"), OBS_GROUP_NORMAL,
                           mirror_group);

  // --- Address bar blur ---------------------------------------------------
  obs_properties_t *blur_group = obs_properties_create();
  obs_properties_add_bool(blur_group, kBlurEnabledSetting, Text("AutoAppCapture.Blur.Enabled"));
  obs_property_t *blur_hint = obs_properties_add_text(blur_group, kBlurHint, "", OBS_TEXT_INFO);
  obs_property_set_long_description(blur_hint, Text("AutoAppCapture.Blur.Hint"));
  obs_property_text_set_info_word_wrap(blur_hint, true);

  obs_property_t *blur_detect = obs_properties_add_list(blur_group, kBlurDetectSetting,
                                                        Text("AutoAppCapture.Blur.Detect"), OBS_COMBO_TYPE_LIST,
                                                        OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(blur_detect, Text("AutoAppCapture.Blur.Detect.Auto"), kBlurDetectAuto);
  obs_property_list_add_string(blur_detect, Text("AutoAppCapture.Blur.Detect.Manual"), kBlurDetectManual);
  obs_property_set_long_description(blur_detect, Text("AutoAppCapture.Blur.Detect.Tooltip"));

  obs_property_t *blur_padding = obs_properties_add_int_slider(blur_group, kBlurPaddingSetting,
                                                               Text("AutoAppCapture.Blur.Padding"), 0, 40, 1);
  obs_property_int_set_suffix(blur_padding, Text("AutoAppCapture.Blur.Strength.Suffix"));
  obs_property_set_long_description(blur_padding, Text("AutoAppCapture.Blur.Padding.Tooltip"));

  obs_property_t *blur_mode = obs_properties_add_list(blur_group, kBlurModeSetting, Text("AutoAppCapture.Blur.Mode"),
                                                      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(blur_mode, Text("AutoAppCapture.Blur.Mode.Frosted"), kBlurModeFrosted);
  obs_property_list_add_string(blur_mode, Text("AutoAppCapture.Blur.Mode.Mosaic"), kBlurModeMosaic);
  obs_property_list_add_string(blur_mode, Text("AutoAppCapture.Blur.Mode.Fill"), kBlurModeFill);
  obs_property_set_modified_callback2(blur_mode, AutoCaptureSource::BlurModeModified, data);

  obs_property_t *blur_strength = obs_properties_add_int_slider(blur_group, kBlurStrengthSetting,
                                                                Text("AutoAppCapture.Blur.Strength"), 4, 80, 1);
  obs_property_int_set_suffix(blur_strength, Text("AutoAppCapture.Blur.Strength.Suffix"));
  obs_property_set_long_description(blur_strength, Text("AutoAppCapture.Blur.Strength.Tooltip"));
  obs_properties_add_color(blur_group, kBlurFillColorSetting, Text("AutoAppCapture.Blur.FillColor"));

  const struct {
    const char *setting;
    const char *description;
  } kZoneFields[] = {
      {kBlurZoneLeftSetting, "AutoAppCapture.Blur.Zone.Left"},
      {kBlurZoneTopSetting, "AutoAppCapture.Blur.Zone.Top"},
      {kBlurZoneWidthSetting, "AutoAppCapture.Blur.Zone.Width"},
      {kBlurZoneHeightSetting, "AutoAppCapture.Blur.Zone.Height"},
  };
  for (const auto &field : kZoneFields) {
    obs_property_t *zone_field =
        obs_properties_add_float_slider(blur_group, field.setting, Text(field.description), 0.0, 100.0, 0.5);
    obs_property_float_set_suffix(zone_field, Text("AutoAppCapture.Blur.Zone.Suffix"));
  }
  obs_properties_add_group(properties, kBlurGroup, Text("AutoAppCapture.Group.Blur"), OBS_GROUP_NORMAL, blur_group);

  BlurMode blur_mode_value = BlurMode::Frosted;
  if (settings != nullptr) {
    blur_mode_value = ParseBlurMode(obs_data_get_string(settings, kBlurModeSetting));
  }
  ApplyBlurModeVisibility(properties, blur_mode_value);

  // --- Advanced -----------------------------------------------------------
  obs_properties_t *advanced_group = obs_properties_create();
  obs_property_t *interval = obs_properties_add_float_slider(advanced_group, kPollIntervalSetting,
                                                             Text("AutoAppCapture.PollInterval"), 0.1, 5.0, 0.1);
  obs_property_float_set_suffix(interval, Text("AutoAppCapture.PollInterval.Suffix"));
  obs_property_set_long_description(interval, Text("AutoAppCapture.PollInterval.Tooltip"));
  obs_properties_add_group(properties, kAdvancedGroup, Text("AutoAppCapture.Group.Advanced"), OBS_GROUP_NORMAL,
                           advanced_group);

  ApplySectionVisibility(properties, section);

  if (settings != nullptr) {
    obs_data_release(settings);
  }
  return properties;
}

void AutoCaptureSource::GetDefaults(obs_data_t *settings)
{
  obs_data_set_default_string(settings, kSelectedRuleSetting, "");
  obs_data_set_default_string(settings, kEditTargetSetting, "");
  obs_data_set_default_bool(settings, kEditEnabledSetting, true);
  obs_data_set_default_string(settings, kEditNameSetting, "");
  obs_data_set_default_string(settings, kEditProcessSetting, "");
  obs_data_set_default_string(settings, kEditModeSetting, kModeAuto);
  obs_data_set_default_string(settings, kEditScopeSetting, kRuleScopeAny);
  obs_data_set_default_string(settings, kEditTitleSetting, "");
  obs_data_set_default_bool(settings, kEditBlurSetting, false);

  obs_data_set_default_string(settings, kNewProcessSetting, "");
  obs_data_set_default_string(settings, kNewNameSetting, "");
  obs_data_set_default_string(settings, kNewModeSetting, kModeAuto);
  obs_data_set_default_string(settings, kNewScopeSetting, kRuleScopeAny);
  obs_data_set_default_string(settings, kNewTitleSetting, "");

  obs_data_set_default_string(settings, kUiSectionSetting, kSectionCapture);

  obs_data_set_default_bool(settings, kBlurEnabledSetting, false);
  obs_data_set_default_string(settings, kBlurDetectSetting, kBlurDetectAuto);
  obs_data_set_default_int(settings, kBlurPaddingSetting, 6);
  obs_data_set_default_string(settings, kBlurModeSetting, kBlurModeFrosted);
  obs_data_set_default_int(settings, kBlurStrengthSetting, 24);
  obs_data_set_default_int(settings, kBlurFillColorSetting, 0xFF101010);
  // Roughly the top strip of a maximised browser window: tab bar plus address
  // bar. Meant to be adjusted once by eye until the auto detection lands.
  obs_data_set_default_double(settings, kBlurZoneLeftSetting, 0.0);
  obs_data_set_default_double(settings, kBlurZoneTopSetting, 0.0);
  obs_data_set_default_double(settings, kBlurZoneWidthSetting, 100.0);
  obs_data_set_default_double(settings, kBlurZoneHeightSetting, 12.0);

  obs_data_set_default_string(settings, kStatusTextSetting, Text("AutoAppCapture.Status.Empty"));
  obs_data_set_default_double(settings, kPollIntervalSetting, 0.5);
  // Was enabled by default in earlier versions: sources that never touched the
  // checkbox rely on it, and nothing is mirrored until a title rule matches.
  obs_data_set_default_bool(settings, kMirrorEnabledSetting, true);
  obs_data_set_default_string(settings, kMirrorTitleRulesSetting, "");
  obs_data_set_default_bool(settings, kLegacyFullscreenOnlySetting, false);
}

uint32_t AutoCaptureSource::GetWidth(void *data)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (!instance) {
    return 0;
  }
  if (obs_source_t *selected = instance->SelectRenderSource()) {
    const uint32_t width = obs_source_get_width(selected);
    if (width > 0) {
      instance->last_width_ = width;
    }
  }
  return instance->last_width_;
}

uint32_t AutoCaptureSource::GetHeight(void *data)
{
  auto *instance = static_cast<AutoCaptureSource *>(data);
  if (!instance) {
    return 0;
  }
  if (obs_source_t *selected = instance->SelectRenderSource()) {
    const uint32_t height = obs_source_get_height(selected);
    if (height > 0) {
      instance->last_height_ = height;
    }
  }
  return instance->last_height_;
}

void AutoCaptureSource::PollActiveWindow()
{
  if (enabled_rule_count_ == 0) {
    ClearActiveMatch();
    return;
  }
  // The previous capture stays alive during Alt+Tab and other transient
  // foreground changes; it is replaced only once another enabled rule matches.
  HWND hwnd = NormalizeWindowForCapture(GetForegroundWindow());
  if (!hwnd || !IsWindowVisible(hwnd)) {
    return;
  }
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0) {
    return;
  }
  std::string process_name = ToLower(GetProcessNameFromPid(pid));
  if (process_name.empty() || target_processes_.find(process_name) == target_processes_.end()) {
    return;
  }
  const std::string title = GetWindowTitle(hwnd);
  const std::string normalized_title = ToLower(title);
  const bool fullscreen = IsWindowEffectivelyFullscreen(hwnd);

  size_t matched_rule_index = kInvalidRuleIndex;
  for (size_t i = 0; i < rules_.size(); ++i) {
    const AutoCaptureRule &rule = rules_[i];
    if (!rule.enabled || rule.process_name != process_name) {
      continue;
    }
    if (rule.fullscreen_only && !fullscreen) {
      continue;
    }
    if (!rule.title_contains.empty() && normalized_title.find(rule.title_contains) == std::string::npos) {
      continue;
    }
    matched_rule_index = i;
    break;
  }
  if (matched_rule_index == kInvalidRuleIndex) {
    return;
  }

  const bool changed = active_window_ != hwnd || active_pid_ != pid || active_rule_index_ != matched_rule_index ||
                       active_process_name_ != process_name || active_window_title_ != title;
  active_window_ = hwnd;
  active_pid_ = pid;
  active_rule_index_ = matched_rule_index;
  active_rule_id_ = rules_[matched_rule_index].id;
  active_process_name_ = std::move(process_name);
  active_window_title_ = title;
  if (changed) {
    blog(LOG_INFO, "[obs-auto-capture] Active target: pid=%lu process=%s title=%s rule=%s",
         static_cast<unsigned long>(active_pid_), active_process_name_.c_str(), active_window_title_.c_str(),
         SerializeRule(rules_[matched_rule_index]).c_str());
  }
}

void AutoCaptureSource::ClearActiveMatch()
{
  active_window_ = nullptr;
  active_pid_ = 0;
  active_rule_index_ = kInvalidRuleIndex;
  active_rule_id_.clear();
  active_process_name_.clear();
  active_window_title_.clear();
}

bool AutoCaptureSource::EnsureCaptureSources()
{
  bool ready = true;
  obs_data_t *window_settings = CreateWindowCaptureSettings(std::string());
  ready = EnsureChildCaptureSource(&window_capture_source_, kWindowCaptureSourceId, " (Internal Window Capture)",
                                   window_settings) &&
          ready;
  obs_data_release(window_settings);
  obs_data_t *game_settings = CreateGameCaptureSettings(std::string());
  ready = EnsureChildCaptureSource(&game_capture_source_, kGameCaptureSourceId, " (Internal Game Capture)",
                                   game_settings) &&
          ready;
  obs_data_release(game_settings);
  return ready;
}

bool AutoCaptureSource::EnsureChildCaptureSource(obs_source_t **slot,
                                                 const char *source_id,
                                                 const char *source_suffix,
                                                 obs_data_t *initial_settings)
{
  if (*slot != nullptr) {
    return true;
  }
  std::string name = source_ != nullptr ? obs_source_get_name(source_) : Text("AutoAppCapture.Source");
  name += source_suffix;
  *slot = obs_source_create_private(source_id, name.c_str(), initial_settings);
  if (*slot == nullptr) {
    blog(LOG_WARNING, "[obs-auto-capture] Failed to create the internal '%s' source; this method is unavailable.",
         source_id);
    return false;
  }
  if (!obs_source_add_active_child(source_, *slot)) {
    blog(LOG_ERROR, "[obs-auto-capture] Failed to attach the internal '%s' child source.", source_id);
    obs_source_release(*slot);
    *slot = nullptr;
    return false;
  }
  blog(LOG_INFO, "[obs-auto-capture] Internal '%s' source created.", source_id);
  return true;
}

void AutoCaptureSource::ResetCaptureSources()
{
  active_window_selector_.clear();
  active_backend_ = AutoCaptureBackend::None;
  if (window_capture_source_ != nullptr) {
    obs_source_remove_active_child(source_, window_capture_source_);
    obs_source_release(window_capture_source_);
    window_capture_source_ = nullptr;
  }
  if (game_capture_source_ != nullptr) {
    obs_source_remove_active_child(source_, game_capture_source_);
    obs_source_release(game_capture_source_);
    game_capture_source_ = nullptr;
  }
}

void AutoCaptureSource::SyncCaptureSources()
{
  if (!EnsureCaptureSources()) {
    return;
  }
  if (active_window_ == nullptr || active_process_name_.empty()) {
    return;
  }
  const std::string window_selector = BuildWindowSelector(active_window_);
  if (window_selector.empty()) {
    return;
  }
  UpdateCaptureTargets(window_selector, false);
}

void AutoCaptureSource::UpdateCaptureTargets(const std::string &window_selector, bool force)
{
  if (!force && (window_selector.empty() || window_selector == active_window_selector_)) {
    return;
  }
  if (window_capture_source_ != nullptr) {
    obs_data_t *window_settings = CreateWindowCaptureSettings(window_selector);
    obs_source_update(window_capture_source_, window_settings);
    obs_data_release(window_settings);
  }
  if (game_capture_source_ != nullptr) {
    obs_data_t *game_settings = CreateGameCaptureSettings(window_selector);
    obs_source_update(game_capture_source_, game_settings);
    obs_data_release(game_settings);
  }
  active_window_selector_ = window_selector;
  blog(LOG_INFO, "[obs-auto-capture] Retargeted internal captures to process=%s title=%s",
       active_process_name_.c_str(), active_window_title_.c_str());
}

obs_source_t *AutoCaptureSource::SelectRenderSource() const
{
  const AutoCaptureRule *rule = GetActiveRule();
  const AutoCaptureMode mode = rule != nullptr ? rule->capture_mode : AutoCaptureMode::Auto;
  switch (mode) {
    case AutoCaptureMode::Window:
      return HasUsableVideo(window_capture_source_) ? window_capture_source_ : nullptr;
    case AutoCaptureMode::Game:
      return HasUsableVideo(game_capture_source_) ? game_capture_source_ : nullptr;
    case AutoCaptureMode::Auto:
    default:
      if (HasUsableVideo(game_capture_source_)) {
        return game_capture_source_;
      }
      if (HasUsableVideo(window_capture_source_)) {
        return window_capture_source_;
      }
      return nullptr;
  }
}

void AutoCaptureSource::UpdateRenderBackend(obs_source_t *selected_source)
{
  AutoCaptureBackend new_backend = AutoCaptureBackend::None;
  if (selected_source != nullptr && selected_source == game_capture_source_) {
    new_backend = AutoCaptureBackend::GameCapture;
  } else if (selected_source != nullptr && selected_source == window_capture_source_) {
    new_backend = AutoCaptureBackend::WindowCapture;
  }
  if (new_backend != active_backend_) {
    active_backend_ = new_backend;
    blog(LOG_INFO, "[obs-auto-capture] Active capture method: %s", BackendLogName(active_backend_));
  }
}

std::string AutoCaptureSource::GetStatusText() const
{
  return status_text_.empty() ? BuildStatusText() : status_text_;
}

std::string AutoCaptureSource::BuildStatusText() const
{
  std::ostringstream status;
  if (rules_.empty()) {
    status << Text("AutoAppCapture.Status.Empty");
    return status.str();
  }

  const AutoCaptureRule *rule = GetActiveRule();
  if (rule == nullptr || active_process_name_.empty()) {
    status << Text("AutoAppCapture.Status.Waiting") << "<br>" << Text("AutoAppCapture.Status.Rules") << ": "
           << enabled_rule_count_ << " / " << rules_.size();
    return status.str();
  }

  status << "<b>" << Text("AutoAppCapture.Status.Capturing") << ":</b> " << EscapeHtml(rule->display_name) << " ("
         << EscapeHtml(rule->process_name) << ")<br>";
  status << Text("AutoAppCapture.Status.Window") << ": "
         << EscapeHtml(active_window_title_.empty() ? Text("AutoAppCapture.Status.Untitled") : active_window_title_)
         << "<br>";
  status << Text("AutoAppCapture.Status.Method") << ": " << DisplayBackendName(active_backend_) << " ("
         << Text("AutoAppCapture.Status.Preferred") << ": " << DisplayCaptureMode(rule->capture_mode) << ")<br>";
  if (ShouldMirror()) {
    status << Text("AutoAppCapture.Status.Mirrored") << "<br>";
  }
  if (BlurAppliesToActiveRule()) {
    status << Text("AutoAppCapture.Status.Blur") << ": ";
    if (!blur_.auto_detect) {
      status << Text("AutoAppCapture.Status.Blur.Manual");
    } else {
      switch (address_bar_status_) {
        case AddressBarStatus::Found:
          status << Text("AutoAppCapture.Status.Blur.Found");
          break;
        case AddressBarStatus::Searching:
          status << Text("AutoAppCapture.Status.Blur.Searching");
          break;
        case AddressBarStatus::Unavailable:
          status << Text("AutoAppCapture.Status.Blur.Unavailable");
          break;
        default:
          status << Text("AutoAppCapture.Status.Blur.Fallback");
          break;
      }
    }
    status << "<br>";
  }
  status << Text("AutoAppCapture.Status.Rules") << ": " << enabled_rule_count_ << " / " << rules_.size();
  return status.str();
}

const AutoCaptureRule *AutoCaptureSource::GetActiveRule() const
{
  if (active_rule_index_ == kInvalidRuleIndex || active_rule_index_ >= rules_.size()) {
    return nullptr;
  }
  return &rules_[active_rule_index_];
}

bool AutoCaptureSource::ShouldMirror() const
{
  if (!mirror_enabled_ || active_window_title_.empty() || mirror_title_rules_.empty()) {
    return false;
  }
  const std::string title = ToLower(active_window_title_);
  return std::any_of(mirror_title_rules_.begin(), mirror_title_rules_.end(),
                     [&title](const std::string &rule) { return title.find(rule) != std::string::npos; });
}

std::string AutoCaptureSource::BuildWindowSelector(HWND hwnd)
{
  hwnd = NormalizeWindowForCapture(hwnd);
  if (!hwnd || !IsWindow(hwnd)) {
    return {};
  }
  struct dstr title = {0};
  struct dstr window_class = {0};
  struct dstr executable = {0};
  ms_get_window_title(&title, hwnd);
  ms_get_window_class(&window_class, hwnd);
  if (!ms_get_window_exe(&executable, hwnd)) {
    dstr_free(&title);
    dstr_free(&window_class);
    dstr_free(&executable);
    return {};
  }
  std::string title_value = title.array ? title.array : "";
  std::string class_value = window_class.array ? window_class.array : "";
  std::string executable_value = executable.array ? executable.array : "";
  dstr_free(&title);
  dstr_free(&window_class);
  dstr_free(&executable);
  if (class_value.empty() || executable_value.empty()) {
    return {};
  }
  EncodeWindowComponent(title_value);
  EncodeWindowComponent(class_value);
  EncodeWindowComponent(executable_value);
  return title_value + ":" + class_value + ":" + executable_value;
}

std::string AutoCaptureSource::SerializeRule(const AutoCaptureRule &rule)
{
  std::string serialized = ToLower(Trim(rule.process_name));
  if (serialized.empty()) {
    return {};
  }
  serialized += " | ";
  serialized += CaptureModeToken(rule.capture_mode);
  serialized += " | ";
  serialized += rule.fullscreen_only ? kRuleScopeFullscreen : kRuleScopeAny;
  serialized += " | ";
  serialized += rule.title_contains.empty() ? kRuleWildcard : rule.title_contains;
  return serialized;
}

std::string AutoCaptureSource::RuleSettingSuffix(const AutoCaptureRule &rule)
{
  const std::string identity = ToLower(Trim(rule.process_name)) + "|" + CaptureModeToken(rule.capture_mode) + "|" +
                               (rule.fullscreen_only ? kRuleScopeFullscreen : kRuleScopeAny) + "|" +
                               ToLower(Trim(rule.title_contains));
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char ch : identity) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  std::ostringstream value;
  value << std::hex << std::setw(16) << std::setfill('0') << hash;
  return value.str();
}

bool AutoCaptureSource::ParseRuleString(const std::string &value,
                                        AutoCaptureRule *rule,
                                        bool legacy_fullscreen_only_default)
{
  if (rule == nullptr) {
    return false;
  }
  const std::string trimmed = Trim(value);
  if (trimmed.empty()) {
    return false;
  }
  const std::vector<std::string> parts = SplitRuleParts(trimmed);
  if (parts.empty()) {
    return false;
  }
  rule->process_name = ToLower(parts[0]);
  if (rule->process_name.empty()) {
    return false;
  }
  rule->capture_mode = parts.size() > 1 ? ParseCaptureModeToken(parts[1]) : AutoCaptureMode::Auto;
  if (parts.size() > 2) {
    const std::string scope = ToLower(parts[2]);
    rule->fullscreen_only = (scope == kRuleScopeFullscreen || scope == "full" || scope == "fs");
  } else {
    rule->fullscreen_only = legacy_fullscreen_only_default;
  }
  if (parts.size() > 3) {
    std::string title_filter = parts[3];
    for (size_t i = 4; i < parts.size(); ++i) {
      title_filter += " | ";
      title_filter += parts[i];
    }
    title_filter = ToLower(Trim(std::move(title_filter)));
    if (title_filter == kRuleWildcard) {
      title_filter.clear();
    }
    rule->title_contains = std::move(title_filter);
  } else {
    rule->title_contains.clear();
  }
  return true;
}

std::vector<std::string> AutoCaptureSource::SplitLines(const std::string &value)
{
  std::vector<std::string> lines;
  std::stringstream stream(value);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

std::string AutoCaptureSource::ToLower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string AutoCaptureSource::Trim(std::string value)
{
  const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

void auto_capture_release_ui_resources()
{
  if (g_picker_class_registered) {
    HINSTANCE instance = static_cast<HINSTANCE>(obs_get_module_lib(obs_current_module()));
    if (!instance) {
      instance = GetModuleHandleW(nullptr);
    }
    UnregisterClassW(kPickerClassName, instance);
    g_picker_class_registered = false;
  }
}

obs_source_info auto_capture_source_info = [] {
  obs_source_info info = {};
  info.id = kSourceId;
  info.type = OBS_SOURCE_TYPE_INPUT;
  info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
  info.icon_type = OBS_ICON_TYPE_WINDOW_CAPTURE;
  info.get_name = AutoCaptureSource::GetName;
  info.create = AutoCaptureSource::Create;
  info.destroy = AutoCaptureSource::Destroy;
  info.get_width = AutoCaptureSource::GetWidth;
  info.get_height = AutoCaptureSource::GetHeight;
  info.get_defaults = AutoCaptureSource::GetDefaults;
  info.get_properties = AutoCaptureSource::GetProperties;
  info.update = AutoCaptureSource::UpdateCallback;
  info.video_tick = AutoCaptureSource::TickCallback;
  info.video_render = AutoCaptureSource::VideoRenderCallback;
  info.enum_active_sources = AutoCaptureSource::EnumActiveSourcesCallback;
  info.video_get_color_space = AutoCaptureSource::GetColorSpaceCallback;
  return info;
}();

// ---------------------------------------------------------------------------
// Access for the settings window
//
// Wrappers, not a second implementation: the window must agree with the source
// about the rule format, including both legacy migrations, or a list edited in
// one place would come back wrong in the other.
// ---------------------------------------------------------------------------

namespace capture_rules {

std::vector<AutoCaptureRule> Load(obs_data_t *settings)
{
  std::vector<AutoCaptureRule> rules;
  LoadRules(settings, rules, nullptr);
  return rules;
}

void Store(obs_data_t *settings, const std::vector<AutoCaptureRule> &rules)
{
  StoreRules(settings, rules);
}

std::string MakeId(const std::vector<AutoCaptureRule> &rules, const AutoCaptureRule &rule)
{
  return MakeUniqueRuleId(rules, rule);
}

std::string FriendlyName(const std::string &process_name)
{
  return FriendlyProcessName(process_name);
}

bool IsBrowser(const std::string &process_name)
{
  return IsBrowserProcess(process_name);
}

} // namespace capture_rules

std::vector<RunningApp> CollectRunningApps()
{
  std::vector<RunningApp> apps;
  for (const ProcessOption &option : CollectProcesses(CollectWindows())) {
    RunningApp app;
    app.process_name = option.process_name;
    app.sample_title = option.sample_title;
    app.window_count = option.window_count;
    apps.push_back(std::move(app));
  }
  return apps;
}
