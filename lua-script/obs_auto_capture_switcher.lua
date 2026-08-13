obs = obslua

local ffi_ok, ffi = pcall(require, "ffi")

local state = {
  ffi_ready = false,
  ffi_error = nil,
  scene_name = "",
  process_map_text = "",
  poll_ms = 500,
  hide_when_unmatched = true,
  ignore_obs = true,
  log_switches = true,
  mappings = {},
  unique_sources = {},
  last_active_process = "",
  last_active_source = "",
  last_scene_name = "",
}

local obs_process_names = {
  ["obs.exe"] = true,
  ["obs32.exe"] = true,
  ["obs64.exe"] = true,
}

local CP_UTF8 = 65001
local PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

if ffi_ok then
  ffi.cdef[[
    typedef void *HANDLE;
    typedef void *HWND;
    typedef int BOOL;
    typedef unsigned long DWORD;
    typedef unsigned int UINT;
    typedef wchar_t WCHAR;

    HWND GetForegroundWindow(void);
    BOOL IsWindow(HWND hWnd);
    BOOL IsWindowVisible(HWND hWnd);
    BOOL IsIconic(HWND hWnd);
    int GetWindowTextLengthW(HWND hWnd);
    int GetWindowTextW(HWND hWnd, WCHAR *lpString, int nMaxCount);
    DWORD GetWindowThreadProcessId(HWND hWnd, DWORD *lpdwProcessId);
    HANDLE OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId);
    BOOL QueryFullProcessImageNameW(HANDLE hProcess, DWORD dwFlags, WCHAR *lpExeName, DWORD *lpdwSize);
    BOOL CloseHandle(HANDLE hObject);
    int WideCharToMultiByte(
      UINT CodePage,
      DWORD dwFlags,
      const WCHAR *lpWideCharStr,
      int cchWideChar,
      char *lpMultiByteStr,
      int cbMultiByte,
      const char *lpDefaultChar,
      BOOL *lpUsedDefaultChar
    );
  ]]

  state.ffi_ready = true
else
  state.ffi_error = tostring(ffi)
end

local function trim(value)
  if value == nil then
    return ""
  end

  return (value:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function basename(path)
  if path == nil or path == "" then
    return ""
  end

  return path:match("([^\\/]+)$") or path
end

local function wide_to_utf8(buffer, length)
  local C = ffi.C
  local wide_length = length or -1
  local needed = C.WideCharToMultiByte(CP_UTF8, 0, buffer, wide_length, nil, 0, nil, nil)

  if needed <= 0 then
    return ""
  end

  local out = ffi.new("char[?]", needed)
  C.WideCharToMultiByte(CP_UTF8, 0, buffer, wide_length, out, needed, nil, nil)

  if wide_length == -1 then
    return ffi.string(out)
  end

  return ffi.string(out, needed)
end

local function get_active_window_info()
  if not state.ffi_ready then
    return nil
  end

  local C = ffi.C
  local hwnd = C.GetForegroundWindow()

  if hwnd == nil or hwnd == ffi.NULL then
    return nil
  end

  if C.IsWindow(hwnd) == 0 or C.IsWindowVisible(hwnd) == 0 or C.IsIconic(hwnd) ~= 0 then
    return nil
  end

  local pid = ffi.new("DWORD[1]", 0)
  C.GetWindowThreadProcessId(hwnd, pid)

  if pid[0] == 0 then
    return nil
  end

  local process_name = ""
  local process_handle = C.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid[0])

  if process_handle ~= nil and process_handle ~= ffi.NULL then
    local buffer_len = 32768
    local process_path = ffi.new("WCHAR[?]", buffer_len)
    local size_ptr = ffi.new("DWORD[1]", buffer_len)

    if C.QueryFullProcessImageNameW(process_handle, 0, process_path, size_ptr) ~= 0 then
      local full_path = wide_to_utf8(process_path, tonumber(size_ptr[0]))
      process_name = basename(full_path):lower()
    end

    C.CloseHandle(process_handle)
  end

  local title = ""
  local title_len = C.GetWindowTextLengthW(hwnd)

  if title_len > 0 then
    local title_buffer = ffi.new("WCHAR[?]", title_len + 1)
    local written = C.GetWindowTextW(hwnd, title_buffer, title_len + 1)
    if written > 0 then
      title = wide_to_utf8(title_buffer, written)
    end
  end

  return {
    pid = tonumber(pid[0]),
    process_name = process_name,
    title = title,
  }
end

local function normalize_process_name(value)
  local normalized = trim((value or ""):lower())

  if normalized == "" then
    return ""
  end

  if not normalized:match("%.exe$") then
    normalized = normalized .. ".exe"
  end

  return normalized
end

local function parse_mappings(raw_text)
  local mappings = {}
  local unique_sources = {}

  for line in (raw_text .. "\n"):gmatch("(.-)\r?\n") do
    local clean_line = trim(line)

    if clean_line ~= "" and not clean_line:match("^#") then
      local process_name, source_name = clean_line:match("^(.-)%s*=>%s*(.-)$")

      if process_name ~= nil and source_name ~= nil then
        process_name = normalize_process_name(process_name)
        source_name = trim(source_name)

        if process_name ~= "" and source_name ~= "" then
          mappings[process_name] = source_name
          unique_sources[source_name] = true
        end
      end
    end
  end

  return mappings, unique_sources
end

local function get_target_scene_source()
  if state.scene_name ~= "" then
    return obs.obs_get_source_by_name(state.scene_name)
  end

  return obs.obs_frontend_get_current_scene()
end

local function set_scene_visibility(scene, active_source_name)
  for source_name, _ in pairs(state.unique_sources) do
    local scene_item = obs.obs_scene_find_source(scene, source_name)

    if scene_item ~= nil then
      if active_source_name ~= "" then
        obs.obs_sceneitem_set_visible(scene_item, source_name == active_source_name)
      elseif state.hide_when_unmatched then
        obs.obs_sceneitem_set_visible(scene_item, false)
      end
    end
  end
end

local function apply_switch(active_process_name, active_source_name, window_title)
  local scene_source = get_target_scene_source()
  if scene_source == nil then
    return
  end

  local scene_name = obs.obs_source_get_name(scene_source) or ""
  local should_apply = active_source_name ~= state.last_active_source or scene_name ~= state.last_scene_name

  if should_apply then
    local scene = obs.obs_scene_from_source(scene_source)
    if scene ~= nil then
      set_scene_visibility(scene, active_source_name)
    end

    if state.log_switches then
      if active_source_name ~= "" then
        obs.script_log(
          obs.LOG_INFO,
          string.format(
            "[auto-capture-switcher] scene='%s' process='%s' source='%s' title='%s'",
            scene_name,
            active_process_name,
            active_source_name,
            window_title or ""
          )
        )
      elseif state.hide_when_unmatched and state.last_active_source ~= "" then
        obs.script_log(
          obs.LOG_INFO,
          string.format("[auto-capture-switcher] scene='%s' no matching active process, mapped sources hidden", scene_name)
        )
      end
    end

    state.last_scene_name = scene_name
    state.last_active_source = active_source_name
    state.last_active_process = active_process_name
  end

  obs.obs_source_release(scene_source)
end

local function poll_active_window()
  local info = get_active_window_info()
  local active_process_name = ""
  local active_source_name = ""
  local active_title = ""

  if info ~= nil then
    active_process_name = info.process_name or ""
    active_title = info.title or ""

    if state.ignore_obs and obs_process_names[active_process_name] then
      active_process_name = ""
    end

    if active_process_name ~= "" then
      active_source_name = state.mappings[active_process_name] or ""
    end
  end

  apply_switch(active_process_name, active_source_name, active_title)
end

local function restart_timer()
  obs.timer_remove(poll_active_window)

  if state.ffi_ready and next(state.mappings) ~= nil then
    obs.timer_add(poll_active_window, state.poll_ms)
    poll_active_window()
  end
end

local function fill_scene_list(property)
  obs.obs_property_list_add_string(property, "Current program scene", "")

  local scenes = obs.obs_frontend_get_scenes()
  if scenes == nil then
    return
  end

  for _, scene_source in ipairs(scenes) do
    local name = obs.obs_source_get_name(scene_source)
    if name ~= nil and name ~= "" then
      obs.obs_property_list_add_string(property, name, name)
    end
  end

  obs.source_list_release(scenes)
end

local function log_active_window_button()
  local info = get_active_window_info()

  if info == nil then
    obs.script_log(obs.LOG_INFO, "[auto-capture-switcher] No active window detected.")
    return false
  end

  obs.script_log(
    obs.LOG_INFO,
    string.format(
      "[auto-capture-switcher] Active window: process='%s' pid=%d title='%s'",
      info.process_name or "",
      info.pid or 0,
      info.title or ""
    )
  )

  return false
end

local function on_frontend_event(event)
  if event == obs.OBS_FRONTEND_EVENT_SCENE_CHANGED or event == obs.OBS_FRONTEND_EVENT_FINISHED_LOADING then
    poll_active_window()
  end
end

function script_description()
  local base = [[
Automatically shows one of your existing OBS capture sources based on the current foreground process.

This is the fast MVP path:
- create Window Capture / Game Capture sources yourself
- map process.exe => source name
- the script shows the source for the active app and hides the others

Mapping format:
notepad.exe => Notepad Capture
game.exe => Game Capture
]]

  if not state.ffi_ready then
    return base .. "\nFFI is unavailable in this OBS Lua runtime, so foreground window detection will not work."
  end

  return base
end

function script_defaults(settings)
  obs.obs_data_set_default_string(settings, "scene_name", "")
  obs.obs_data_set_default_string(settings, "process_map", "")
  obs.obs_data_set_default_int(settings, "poll_ms", 500)
  obs.obs_data_set_default_bool(settings, "hide_when_unmatched", true)
  obs.obs_data_set_default_bool(settings, "ignore_obs", true)
  obs.obs_data_set_default_bool(settings, "log_switches", true)
  obs.obs_data_set_default_string(
    settings,
    "mapping_hint",
    "Format:\nnotepad.exe => Notepad Capture\ngame.exe => Game Capture\n\nOnly one mapping per line. Lines starting with # are ignored."
  )
end

function script_properties()
  local props = obs.obs_properties_create()

  local scene_prop = obs.obs_properties_add_list(props, "scene_name", "Target scene", obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING)
  fill_scene_list(scene_prop)

  obs.obs_properties_add_text(props, "process_map", "Process => Source mappings", obs.OBS_TEXT_MULTILINE)
  obs.obs_properties_add_int(props, "poll_ms", "Polling interval (ms)", 100, 5000, 100)
  obs.obs_properties_add_bool(props, "hide_when_unmatched", "Hide mapped sources when no listed app is active")
  obs.obs_properties_add_bool(props, "ignore_obs", "Ignore OBS when OBS is the foreground app")
  obs.obs_properties_add_bool(props, "log_switches", "Write switches to OBS log")
  obs.obs_properties_add_button(props, "log_active_window", "Log current active window", log_active_window_button)

  local hint = obs.obs_properties_add_text(props, "mapping_hint", "Help", obs.OBS_TEXT_INFO)
  obs.obs_property_set_enabled(hint, false)

  return props
end

function script_update(settings)
  state.scene_name = trim(obs.obs_data_get_string(settings, "scene_name"))
  state.process_map_text = obs.obs_data_get_string(settings, "process_map")
  state.poll_ms = obs.obs_data_get_int(settings, "poll_ms")
  state.hide_when_unmatched = obs.obs_data_get_bool(settings, "hide_when_unmatched")
  state.ignore_obs = obs.obs_data_get_bool(settings, "ignore_obs")
  state.log_switches = obs.obs_data_get_bool(settings, "log_switches")
  state.mappings, state.unique_sources = parse_mappings(state.process_map_text)
  state.last_active_process = ""
  state.last_active_source = ""
  state.last_scene_name = ""

  if state.poll_ms < 100 then
    state.poll_ms = 100
  end

  restart_timer()
end

function script_load(settings)
  obs.obs_frontend_add_event_callback(on_frontend_event)

  if not state.ffi_ready then
    obs.script_log(obs.LOG_ERROR, "[auto-capture-switcher] LuaJIT FFI is unavailable. Foreground window detection is disabled.")
    if state.ffi_error ~= nil then
      obs.script_log(obs.LOG_ERROR, "[auto-capture-switcher] " .. tostring(state.ffi_error))
    end
  end

  script_update(settings)
end

function script_unload()
  obs.timer_remove(poll_active_window)
end
