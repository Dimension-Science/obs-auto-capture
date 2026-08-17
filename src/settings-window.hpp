#pragma once

#include <obs.h>

// Deliberately free of Qt: the source runs on the graphics thread and has no
// business seeing Qt headers. Everything Qt lives in settings-window.cpp.
//
// Both functions must be called on the UI thread. Property callbacks already
// run there, which is why the window is opened from one.

// Opens the settings window for a source, or raises the one already open for
// it. One window per source, keyed by source UUID.
void auto_capture_open_settings(obs_source_t *source);

// Called while the OBS properties dialog for this source is being built: closes
// it and opens the window instead.
void auto_capture_replace_properties_with_window(obs_source_t *source);

// Closes every open window. Called on module unload, when Qt widgets must not
// outlive the module that created them.
void auto_capture_close_settings_windows();
