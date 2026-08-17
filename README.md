# OBS Auto Capture

A Windows plugin for OBS Studio that captures whichever of your applications is
in the foreground, switching between them on its own.

Add one source, list the applications you care about, and stop rebuilding your
scene every time you alt-tab between a game, a browser and a chat window.

[Русская версия](README.ru.md)

## What it does

- **Follows the foreground application.** One source, a list of executables, and
  the capture switches to whichever of them you are actually looking at.
- **Picks the capture method for you.** Game capture where it works, window
  capture otherwise, or forced either way per application.
- **Stays put during alt-tab.** Switching to something that is not on the list
  leaves the last matching capture on screen instead of dropping to black.
- **Hides the browser address bar.** The plugin asks the browser where its
  address bar is and covers it with frosted glass, pixels or a solid colour, so
  the URL of what you are reading does not go out on stream.
- **Mirrors the image** for windows whose title matches a word you listed.

Everything is configured in one window with a live preview of the source, so you
can see where the blur actually lands.

## Requirements

- Windows
- OBS Studio 30.0 or newer, including 31.x and 32.x

One binary covers every supported version, because libobs only refuses a plugin
that was built against a *newer* release than the one running it. Checked by
running the built plugin on portable OBS 30.2.3 and 31.1.2 and on an installed
32.1.2, each time confirming that the source is created and starts capturing,
not merely that the module loads.

## Installing

Download the installer from [Releases](https://github.com/Dimension-Science/obs-auto-capture/releases)
and run it. It finds OBS by itself and suggests
`%ProgramData%\obs-studio\plugins`, which is where OBS looks for plugins on
Windows and which survives OBS updates. A portable OBS only loads plugins from
its own folder, and the installer suggests that instead when it detects one.

The zip from the same page is for unpacking by hand.

The installer is not code signed, so Windows SmartScreen will warn about it.

## Using it

Add the **Auto App Capture** source to a scene and double click it. The settings
window opens with a live preview on top and the sections below.

- **Application capture** — the tracked applications, added with a button and
  removed with another. The panel underneath sets the name, capture method,
  whether to switch for any window or only a fullscreen one, an optional window
  title filter, and whether to hide the address bar for this application. How
  often the foreground window is checked is at the bottom.
- **Mirror the image** — a list of words; while the active window title contains
  one of them the image is flipped horizontally.
- **Address bar blur** — how to find the bar, what to cover it with, how strong,
  and the fallback area.

### About the address bar blur

The blur is a privacy feature, so it fails towards covering *more*, never less:

- The address bar is located through UI Automation. While it has not been found
  yet, if detection is unavailable, or if the captured frame no longer matches
  the window, the **fallback area** is covered instead. The blur never silently
  turns off.
- Frosted glass is a mosaic with a smooth blend between cells, not a plain blur.
  Text under it is destroyed rather than smeared, and cannot be sharpened back.
- It is enabled per application. Browsers get it by default; everything else
  does not, so a game or a chat window is never covered by accident.

What it does **not** cover: tab titles, the link preview in the bottom-left
corner, and the suggestion dropdown. Do not treat it as a guarantee that no URL
can appear on stream.

## Building

The project uses the official OBS plugin template. You do not need an OBS
checkout: configuring downloads the pinned OBS sources and prebuilt
dependencies into `.deps`.

```
cmake --preset windows-x64
cmake --build --preset windows-x64
```

Visual Studio 2022 and CMake 3.28 or newer are required. The first configure is
slow because it builds libobs.

`buildspec.json` pins the dependency versions. The plugin is built against the
oldest supported OBS on purpose, so that one binary runs on every release above
it. Two files under `cmake/` carry local changes to the template, both marked
`Local change to the vendored template`.

## Licence

GPL-2.0, matching libobs. See [LICENSE](LICENSE).
