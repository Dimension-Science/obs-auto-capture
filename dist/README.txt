OBS Auto Capture — automatic app capture for OBS Studio
=======================================================

What it does
------------
Add your apps and games once. The source then always shows whichever of
them is in the foreground: minimize the game and switch to the browser and
the picture follows, go back to the game and it follows again. No manual
scene switching.

Requirements
------------
- Windows 10 / 11, 64-bit
- OBS Studio 31 or newer (built and tested against 32.1)

Install
-------
1. Close OBS Studio.
2. Unpack the whole archive somewhere.
3. Run install.cmd.
4. Start OBS Studio.

It installs into your user profile, so an OBS update will not remove it:
%APPDATA%\obs-studio\plugins\obs-auto-capture

Manual install: copy
  obs-auto-capture\bin\64bit\obs-auto-capture.dll
  obs-auto-capture\data\locale\*.ini
into
  %APPDATA%\obs-studio\plugins\obs-auto-capture\bin\64bit\
  %APPDATA%\obs-studio\plugins\obs-auto-capture\data\locale\

Usage
-----
1. In OBS click "+" in the Sources panel.
2. Pick "Auto App Capture" and name the source.
3. In the properties window press "Choose an app or window..." (it has a
   search box), or pick the app in the "Running applications" list, then
   press "Add to the list".
4. Repeat for the rest of your apps and games.
5. The panel at the top always shows what is being captured right now and
   with which method.

Tips
----
- "Track this application" temporarily disables a rule without deleting it.
- "Capture method": Automatic fits almost everything. Use "Game capture
  only" for games that refuse to be captured, "Window capture only" for
  regular apps.
- "Switch to it -> Only when it is fullscreen" is handy for games whose
  windowed mode should be ignored.
- "Window title contains" limits a rule to certain pages, e.g. youtube.
- Opening a window that is not on the list (OBS itself, for example) does
  not blank the source: the last matching app stays on screen.

Uninstall
---------
Run uninstall.cmd, or delete
%APPDATA%\obs-studio\plugins\obs-auto-capture

Troubleshooting
---------------
Send the OBS log file (Help -> Log Files -> Show Log Files, newest .txt).
Plugin lines are prefixed with [obs-auto-capture].
