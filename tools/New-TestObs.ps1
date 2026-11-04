<#
.SYNOPSIS
  Builds an isolated OBS for testing the plugin, and starts it.

.DESCRIPTION
  Copies an installed OBS into a scratch folder, switches it to portable mode
  and drops the plugin inside. Nothing touches the real OBS: not its scenes,
  not its profiles, not %ProgramData%. A portable OBS keeps its configuration
  next to the executable and does not scan the shared plugin folder at all,
  which is why the plugin is copied into the copy rather than installed.

  Run this before releasing. A build that compiles is not a build that works.

.EXAMPLE
  .\tools\New-TestObs.ps1 -Payload .\release\obs-auto-capture
#>
[CmdletBinding()]
param(
  # The plugin folder to test: bin\64bit and data, as CMake installs it.
  [Parameter(Mandatory = $true)][string]$Payload,
  # An installed OBS to copy. Found from the registry when not given.
  [string]$SourceObs,
  [string]$Root = (Join-Path $env:TEMP "obs-auto-capture-test"),
  # Applications the demo source tracks. A title filter keeps the capture off
  # windows that are none of the test's business.
  [string]$DemoProcess = "chrome.exe",
  [string]$DemoTitle = "",
  # OBS interface language for the copy. Left empty, OBS picks its own. Set it
  # when the pictures have to come out in one language whatever the machine.
  [string]$Language = ""
)

$ErrorActionPreference = "Stop"

if (-not $SourceObs) {
  foreach ($key in "HKLM:\SOFTWARE\OBS Studio", "HKLM:\SOFTWARE\WOW6432Node\OBS Studio") {
    if (Test-Path $key) {
      $candidate = (Get-ItemProperty $key).'(default)'
      if ($candidate -and (Test-Path (Join-Path $candidate "bin\64bit\obs64.exe"))) { $SourceObs = $candidate; break }
    }
  }
}
if (-not $SourceObs) { throw "No OBS found. Pass -SourceObs." }
if (-not (Test-Path (Join-Path $Payload "bin\64bit\obs-auto-capture.dll"))) {
  throw "No plugin under $Payload"
}

Get-Process obs64 -ErrorAction SilentlyContinue |
  Where-Object { $_.Path -like "$Root*" } | ForEach-Object { $_.Kill(); $_.WaitForExit(20000) | Out-Null }
Start-Sleep -Seconds 2
if (Test-Path $Root) { Remove-Item $Root -Recurse -Force }

Write-Host "Copying $SourceObs ..."
Copy-Item $SourceObs $Root -Recurse
# An install that has ever been run in portable mode keeps a config folder next
# to the executable, and the copy brings its scene collections and its old logs
# along. Delete it: the check below reads the newest log, and a log left there
# by someone else would answer for a start that never happened.
if (Test-Path (Join-Path $Root "config")) { Remove-Item (Join-Path $Root "config") -Recurse -Force }
Set-Content (Join-Path $Root "portable_mode.txt") -Value "" -Encoding ascii

Copy-Item (Join-Path $Payload "bin\64bit\obs-auto-capture.dll") (Join-Path $Root "obs-plugins\64bit") -Force
$dataDst = Join-Path $Root "data\obs-plugins\obs-auto-capture"
New-Item -ItemType Directory -Force $dataDst | Out-Null
Copy-Item (Join-Path $Payload "data\*") $dataDst -Recurse -Force

$cfg = Join-Path $Root "config\obs-studio"
New-Item -ItemType Directory -Force (Join-Path $cfg "basic\scenes"), (Join-Path $cfg "basic\profiles\Demo") | Out-Null

# OBS 30.2 split the per-user settings into user.ini and 31 reads only that
# one. Writing both keeps this working across the supported range; miss it and
# OBS silently starts with an empty collection, which reads as a plugin fault.
$languageLine = if ($Language) { "Language=$Language" } else { "" }
$appConfig = @"
[General]
FirstRun=false
$languageLine
[Basic]
Profile=Demo
ProfileDir=Demo
SceneCollection=Demo
SceneCollectionFile=Demo
ConfigOnNewProfile=false
"@
$appConfig | Set-Content (Join-Path $cfg "global.ini") -Encoding utf8
$appConfig | Set-Content (Join-Path $cfg "user.ini") -Encoding utf8

@"
[General]
Name=Demo
[Video]
BaseCX=1920
BaseCY=1080
OutputCX=1920
OutputCY=1080
FPSType=0
FPSCommon=60
"@ | Set-Content (Join-Path $cfg "basic\profiles\Demo\basic.ini") -Encoding utf8

$rule = [ordered]@{
  id = "demo"; name = "Demo"; process = $DemoProcess; mode = "auto"; scope = "any"
  title = $DemoTitle; enabled = $true; blur = $true
  value = "$DemoProcess | auto | any | $(if ($DemoTitle) { $DemoTitle } else { '*' })"
}
$scene = [ordered]@{
  current_scene = "Scene"; current_program_scene = "Scene"
  scene_order = @(@{ name = "Scene" }); name = "Demo"
  sources = @(
    [ordered]@{
      id = "auto_app_capture_source"; versioned_id = "auto_app_capture_source"; name = "Auto App Capture"
      enabled = $true; muted = $false; volume = 1.0; balance = 0.5; mixers = 0; sync = 0; flags = 0
      settings = [ordered]@{
        blur_enabled = $true; blur_detect = "auto"; blur_mode = "frosted"
        blur_strength = 24; blur_padding = 6
        blur_zone_left = 6.0; blur_zone_top = 4.5; blur_zone_width = 80.5; blur_zone_height = 3.0
        fit_to_canvas = $true; scale_filter = "bilinear"
        mirror_enabled = $true; mirror_title_rules = ""; poll_interval_seconds = 0.5
        target_rules = @($rule)
      }
    },
    [ordered]@{
      id = "scene"; versioned_id = "scene"; name = "Scene"
      enabled = $true; muted = $false; volume = 1.0; balance = 0.5; mixers = 0; sync = 0; flags = 0
      settings = @{ items = @(@{ name = "Auto App Capture"; visible = $true; align = 5; bounds_type = 0 }) }
    }
  )
}
$scene | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $cfg "basic\scenes\Demo.json") -Encoding utf8

Start-Process (Join-Path $Root "bin\64bit\obs64.exe") -WorkingDirectory (Join-Path $Root "bin\64bit") -ArgumentList "--portable"
Start-Sleep -Seconds 25

$log = Get-ChildItem (Join-Path $cfg "logs\*.txt") -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $log) { throw "OBS wrote no log: portable mode did not take" }
$lines = Get-Content $log.FullName
$result = [ordered]@{
  Root    = $Root
  Log     = $log.Name
  Loaded  = [bool]($lines | Select-String -Quiet "\[obs-auto-capture\] Plugin loaded")
  # Loading only proves the imports resolved. Creating the source proves it runs.
  Source  = [bool]($lines | Select-String -Quiet "\[obs-auto-capture\] Internal 'window_capture' source created")
  # OBS creates this folder on every start, so count what is inside it.
  Crashed = ((Get-ChildItem (Join-Path $cfg "crashes") -File -Force -ErrorAction SilentlyContinue | Measure-Object).Count -gt 0)
}
[PSCustomObject]$result
if (-not ($result.Loaded -and $result.Source)) { throw "Smoke test failed: see $($log.FullName)" }
