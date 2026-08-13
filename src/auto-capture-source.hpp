#pragma once

#include <windows.h>

#include <obs-module.h>

#include "address-bar-locator.hpp"

#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

enum class AutoCaptureMode {
  Auto,
  Window,
  Game,
};

enum class AutoCaptureBackend {
  None,
  WindowCapture,
  GameCapture,
};

enum class BlurMode {
  Frosted,
  Mosaic,
  Fill,
};

// Rectangle in fractions of the captured frame, so it survives a change of
// capture resolution without being recomputed.
struct BlurZone {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;

  bool Valid() const { return right > left && bottom > top; }
};

struct BlurSettings {
  bool enabled = false;
  bool auto_detect = true;
  BlurMode mode = BlurMode::Frosted;
  int cell_pixels = 24;
  int padding_pixels = 6;
  uint32_t fill_color = 0xFF000000;
  // Used whenever the address bar was not located: covering too much beats
  // covering nothing.
  BlurZone fallback_zone;
};

struct AutoCaptureRule {
  std::string id;
  std::string display_name;
  std::string process_name;
  bool enabled = true;
  AutoCaptureMode capture_mode = AutoCaptureMode::Auto;
  bool fullscreen_only = false;
  std::string title_contains;
  // Hiding the address bar only makes sense for browsers, so it is a property
  // of the rule and not of the whole source.
  bool blur_address_bar = false;
};

class AutoCaptureSource {
public:
  AutoCaptureSource(obs_data_t *settings, obs_source_t *source);
  ~AutoCaptureSource();

  void Update(obs_data_t *settings);
  void Tick(float seconds);

  static const char *GetName(void *type_data);
  static void *Create(obs_data_t *settings, obs_source_t *source);
  static void Destroy(void *data);
  static void UpdateCallback(void *data, obs_data_t *settings);
  static void TickCallback(void *data, float seconds);
  static void VideoRenderCallback(void *data, gs_effect_t *effect);
  static enum gs_color_space GetColorSpaceCallback(void *data, size_t count,
                                                   const enum gs_color_space *preferred_spaces);
  static void EnumActiveSourcesCallback(void *data, obs_source_enum_proc_t cb, void *param);
  static obs_properties_t *GetProperties(void *data);
  static void GetDefaults(obs_data_t *settings);
  static uint32_t GetWidth(void *data);
  static uint32_t GetHeight(void *data);

  // Property callbacks.
  static bool RuleFieldModified(void *data,
                                obs_properties_t *props,
                                obs_property_t *property,
                                obs_data_t *settings);
  static bool RuleEnabledModified(void *data,
                                  obs_properties_t *props,
                                  obs_property_t *property,
                                  obs_data_t *settings);
  static bool BlurModeModified(void *data,
                               obs_properties_t *props,
                               obs_property_t *property,
                               obs_data_t *settings);
  static bool RefreshClicked(obs_properties_t *props, obs_property_t *property, void *data);
  static bool OpenWindowPickerClicked(obs_properties_t *props, obs_property_t *property, void *data);
  static bool AddRuleClicked(obs_properties_t *props, obs_property_t *property, void *data);
  static bool DeleteRuleClicked(obs_properties_t *props, obs_property_t *property, void *data);

  // Shared helpers.
  static std::vector<std::string> SplitLines(const std::string &value);
  static std::string ToLower(std::string value);
  static std::string Trim(std::string value);
  static std::string SerializeRule(const AutoCaptureRule &rule);
  static std::string RuleSettingSuffix(const AutoCaptureRule &rule);
  static bool ParseRuleString(const std::string &value,
                              AutoCaptureRule *rule,
                              bool legacy_fullscreen_only_default);

private:
  void PollActiveWindow();
  void ClearActiveMatch();
  bool EnsureCaptureSources();
  bool EnsureChildCaptureSource(obs_source_t **slot,
                                const char *source_id,
                                const char *source_suffix,
                                obs_data_t *initial_settings);
  void ResetCaptureSources();
  void SyncCaptureSources();
  void UpdateCaptureTargets(const std::string &window_selector, bool force);
  obs_source_t *SelectRenderSource() const;
  void UpdateRenderBackend(obs_source_t *selected_source);
  bool EnsureBlurEffect();
  void ReleaseBlurResources();
  bool BlurAppliesToActiveRule() const;
  void SyncAddressBarLocator();
  bool MapScreenRectToCapture(const RECT &screen, uint32_t cx, uint32_t cy, BlurZone *zone) const;
  size_t BuildBlurZones(BlurZone *zones, size_t capacity, uint32_t cx, uint32_t cy) const;
  bool RenderObscured(obs_source_t *child);
  std::string BuildStatusText() const;
  const AutoCaptureRule *GetActiveRule() const;
  bool ShouldMirror() const;
  std::string GetStatusText() const;

  static std::string BuildWindowSelector(HWND hwnd);

private:
  static constexpr size_t kInvalidRuleIndex = std::numeric_limits<size_t>::max();
  // Matches the number of zone uniforms declared by the effect.
  static constexpr size_t kMaxBlurZones = 4;

  obs_source_t *source_ = nullptr;
  obs_source_t *window_capture_source_ = nullptr;
  obs_source_t *game_capture_source_ = nullptr;

  std::unordered_set<std::string> target_processes_;
  std::vector<AutoCaptureRule> rules_;
  size_t enabled_rule_count_ = 0;

  float poll_interval_seconds_ = 0.5f;
  float elapsed_seconds_ = 0.0f;

  HWND active_window_ = nullptr;
  DWORD active_pid_ = 0;
  size_t active_rule_index_ = kInvalidRuleIndex;
  std::string active_rule_id_;
  std::string active_process_name_;
  std::string active_window_title_;
  std::string active_window_selector_;
  AutoCaptureBackend active_backend_ = AutoCaptureBackend::None;
  std::string status_text_;
  std::vector<std::string> mirror_title_rules_;
  bool mirror_enabled_ = true;
  uint32_t last_width_ = 1280;
  uint32_t last_height_ = 720;

  BlurSettings blur_;
  AddressBarLocator address_bar_locator_;
  mutable AddressBarStatus address_bar_status_ = AddressBarStatus::Idle;
  gs_effect_t *blur_effect_ = nullptr;
  gs_texrender_t *blur_texrender_ = nullptr;
  // Set after a failed load so a broken or missing effect file is reported
  // once instead of on every frame.
  bool blur_effect_failed_ = false;
};

extern obs_source_info auto_capture_source_info;

// Releases process-wide UI resources (window class, fonts) on module unload.
void auto_capture_release_ui_resources();
