#include "auto-capture-source.hpp"

#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-auto-capture", "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
  return "OBS Auto Capture";
}

MODULE_EXPORT const char *obs_module_description(void)
{
  return obs_module_text("AutoAppCapture.Description");
}

bool obs_module_load(void)
{
  obs_register_source(&auto_capture_source_info);
  blog(LOG_INFO, "[obs-auto-capture] Plugin loaded.");
  return true;
}

void obs_module_unload(void)
{
  auto_capture_release_ui_resources();
  blog(LOG_INFO, "[obs-auto-capture] Plugin unloaded.");
}
