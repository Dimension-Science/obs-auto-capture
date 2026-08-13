; Installer for the OBS Auto Capture plugin.
;
; The standard destination page, pre-filled with the folder the plugin should
; normally go to. Everything interesting happens around it:
;
;   * the default is the user plugin folder, which needs no administrator and
;     survives OBS updates, adjusted automatically for a portable OBS;
;   * a folder picked by hand is classified by what is inside it, so pointing at
;     an OBS installation produces the layout OBS expects there;
;   * an old copy in the other standard location is removed, because two copies
;     of one module both get loaded.
;
; Build:  ISCC.exe installer\obs-auto-capture.iss
; Input:  dist\obs-auto-capture\  (produced by the normal build)
; Output: dist\obs-auto-capture-<version>-setup.exe

#define AppName "OBS Auto Capture"
; Overridable so a release build stamps the tag instead of an edited constant:
;   ISCC.exe /DAppVersion=1.2.0 installer\obs-auto-capture.iss
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
; The Windows file version resource accepts numbers only, so a pre-release tag
; like 1.0.0-rc1 keeps its suffix in the visible name and loses it here.
#if Pos("-", AppVersion) > 0
  #define NumericVersion Copy(AppVersion, 1, Pos("-", AppVersion) - 1)
#else
  #define NumericVersion AppVersion
#endif
#define AppPublisher "Dimension Science"
#define PluginId "obs-auto-capture"
; Overridable so CI can package what it just built:
;   ISCC.exe /DPayloadDir=..\release\obs-auto-capture installer\obs-auto-capture.iss
#ifndef PayloadDir
  #define PayloadDir "..\dist\obs-auto-capture"
#endif

[Setup]
AppId={{9F2C1B7E-4A56-4E2D-9C31-8D6B0E7A5C14}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#NumericVersion}
VersionInfoDescription={#AppName} plugin installer
DefaultDirName={code:GetDefaultDir}
AppendDefaultDirName=no
DirExistsWarning=no
DisableProgramGroupPage=yes
DisableWelcomePage=no
; Picks Russian or English from the system language instead of opening with a
; question nobody needs to answer.
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage
; The default location never needs administrator. A hand-picked one might, and
; the wizard says so before installing rather than failing halfway through.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename={#PluginId}-{#AppVersion}-setup
UninstallFilesDir={code:GetDataDir}
UninstallDisplayName={#AppName} {#AppVersion}
WizardStyle=modern
Compression=lzma2
SolidCompression=yes

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
ru.SelectDirLabel3=Плагин будет установлен в указанную папку.
ru.SelectDirBrowseLabel=Нажмите «Далее», чтобы продолжить. Чтобы выбрать другую папку, нажмите «Обзор». Можно указать и папку самой OBS Studio — установщик разложит файлы так, как она ожидает.
en.SelectDirLabel3=The plugin will be installed into the folder below.
en.SelectDirBrowseLabel=Click Next to continue. To pick a different folder, click Browse. An OBS Studio folder works too: the files will be laid out the way it expects them there.

[CustomMessages]
ru.NoWrite=Нет прав на запись в указанную папку. Выберите другую или запустите установщик от имени администратора.
ru.ObsRunning=OBS Studio запущена, файл плагина занят. Закройте OBS Studio и нажмите «Повторить».
ru.RemoveOther=Плагин уже установлен в другом месте:%n%n%1%n%nЕсли оставить обе копии, OBS загрузит плагин дважды. Удалить старую копию?
en.NoWrite=No write access to that folder. Pick another one, or run the installer as administrator.
en.ObsRunning=OBS Studio is running and the plugin file is in use. Close OBS Studio and press Retry.
en.RemoveOther=The plugin is already installed in another location:%n%n%1%n%nLeaving both copies makes OBS load the plugin twice. Remove the old copy?

[Files]
Source: "{#PayloadDir}\bin\64bit\{#PluginId}.dll"; DestDir: "{code:GetBinDir}"; Flags: ignoreversion
Source: "{#PayloadDir}\data\locale\*.ini"; DestDir: "{code:GetDataDir}\locale"; Flags: ignoreversion
Source: "{#PayloadDir}\data\shaders\*.effect"; DestDir: "{code:GetDataDir}\shaders"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{code:GetDataDir}\locale"
Type: filesandordirs; Name: "{code:GetDataDir}\shaders"
Type: dirifempty; Name: "{code:GetDataDir}"
Type: dirifempty; Name: "{code:GetBinDir}"

[Code]
var
  ObsDir: String;
  ObsIsPortable: Boolean;

function IsObsFolder(Dir: String): Boolean;
begin
  Result := (Dir <> '') and FileExists(AddBackslash(Dir) + 'bin\64bit\obs64.exe');
end;

// Reads the target of a shortcut. Wrapped in try/except because Windows Script
// Host can be disabled by policy, and this is only the last fallback anyway.
function ObsDirFromShortcut(LinkPath: String): String;
var
  Shell: Variant;
  Target: String;
begin
  Result := '';
  if not FileExists(LinkPath) then
    exit;
  try
    Shell := CreateOleObject('WScript.Shell');
    Target := Shell.CreateShortcut(LinkPath).TargetPath;
    if Target <> '' then
      // <obs>\bin\64bit\obs64.exe -> <obs>
      Result := ExtractFileDir(ExtractFileDir(ExtractFileDir(Target)));
  except
  end;
end;

function ObsDirFromUninstallEntry(Root: Integer): String;
var
  Icon: String;
begin
  Result := '';
  // The documented InstallLocation value is empty on plenty of machines, but
  // DisplayIcon points straight at obs64.exe.
  if RegQueryStringValue(Root, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'DisplayIcon', Icon) then
  begin
    Icon := RemoveQuotes(Icon);
    if Icon <> '' then
      Result := ExtractFileDir(ExtractFileDir(ExtractFileDir(Icon)));
  end;
end;

function DetectObsDir(): String;
var
  Candidate: String;
begin
  Result := '';

  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Candidate) and IsObsFolder(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;
  if RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', Candidate) and IsObsFolder(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;

  Candidate := ObsDirFromUninstallEntry(HKLM64);
  if IsObsFolder(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;
  Candidate := ObsDirFromUninstallEntry(HKLM32);
  if IsObsFolder(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;
  Candidate := ObsDirFromUninstallEntry(HKCU);
  if IsObsFolder(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;

  Candidate := ExpandConstant('{commonpf64}\obs-studio');
  if IsObsFolder(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;
  Candidate := ExpandConstant('{commonpf32}\obs-studio');
  if IsObsFolder(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;

  Candidate := ObsDirFromShortcut(ExpandConstant('{commonprograms}\OBS Studio\OBS Studio (64bit).lnk'));
  if IsObsFolder(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;
  Candidate := ObsDirFromShortcut(ExpandConstant('{userprograms}\OBS Studio\OBS Studio (64bit).lnk'));
  if IsObsFolder(Candidate) then
    Result := Candidate;
end;

procedure EnsureObsDetected();
begin
  if ObsDir <> '' then
    exit;
  ObsDir := DetectObsDir();
  ObsIsPortable := (ObsDir <> '') and
                   (FileExists(AddBackslash(ObsDir) + 'portable_mode.txt') or
                    FileExists(AddBackslash(ObsDir) + 'obs_portable_mode.txt'));
end;

// In portable mode OBS keeps its configuration next to the executable and never
// looks in %APPDATA%, so the suggested folder has to move with it.
function UserPluginDir(): String;
begin
  EnsureObsDetected();
  if ObsIsPortable then
    Result := AddBackslash(ObsDir) + 'config\obs-studio\plugins\{#PluginId}'
  else
    Result := ExpandConstant('{userappdata}\obs-studio\plugins\{#PluginId}');
end;

function GetDefaultDir(Param: String): String;
begin
  Result := UserPluginDir();
end;

function TargetRoot(): String;
begin
  Result := RemoveBackslashUnlessRoot(WizardDirValue());
end;

// An OBS installation and a plugin folder need different layouts. Which one to
// use is decided by what is in the folder, not by a question the user should
// not have to answer.
function UsesObsLayout(): Boolean;
begin
  Result := IsObsFolder(TargetRoot());
end;

function GetBinDir(Param: String): String;
begin
  if UsesObsLayout() then
    Result := AddBackslash(TargetRoot()) + 'obs-plugins\64bit'
  else
    Result := AddBackslash(TargetRoot()) + 'bin\64bit';
end;

function GetDataDir(Param: String): String;
begin
  if UsesObsLayout() then
    Result := AddBackslash(TargetRoot()) + 'data\obs-plugins\{#PluginId}'
  else
    Result := AddBackslash(TargetRoot()) + 'data';
end;

function CanWriteTo(Dir: String): Boolean;
var
  Probe: String;
begin
  Probe := AddBackslash(Dir) + 'obs-auto-capture-write-test.tmp';
  ForceDirectories(Dir);
  Result := SaveStringToFile(Probe, 'x', False);
  if Result then
    DeleteFile(Probe);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Chosen: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then
    exit;

  Chosen := TargetRoot();
  // Browsing to a plugins folder should not scatter bin and data across it.
  // The correction is written back into the field so it is visible, not
  // applied behind the user's back.
  if not IsObsFolder(Chosen) and (CompareText(ExtractFileName(Chosen), '{#PluginId}') <> 0) then
  begin
    WizardForm.DirEdit.Text := AddBackslash(Chosen) + '{#PluginId}';
    Chosen := TargetRoot();
  end;

  if not CanWriteTo(GetBinDir('')) then
  begin
    MsgBox(CustomMessage('NoWrite'), mbError, MB_OK);
    Result := False;
  end;
end;

function SameFolder(A, B: String): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(A), RemoveBackslashUnlessRoot(B)) = 0;
end;

function ConfirmRemoval(Where: String): Boolean;
begin
  Result := WizardSilent or
            (MsgBox(FmtMessage(CustomMessage('RemoveOther'), [Where]), mbConfirmation, MB_YESNO) = IDYES);
end;

// Both standard locations are checked, not just "the other one": with a
// hand-picked folder in the mix either of them can be the leftover.
procedure RemoveOtherInstallation();
var
  TargetBin, OtherBin, OtherData: String;
begin
  EnsureObsDetected();
  TargetBin := GetBinDir('');

  OtherBin := AddBackslash(UserPluginDir()) + 'bin\64bit';
  if not SameFolder(OtherBin, TargetBin) and FileExists(AddBackslash(OtherBin) + '{#PluginId}.dll') then
    if ConfirmRemoval(UserPluginDir()) then
      DelTree(UserPluginDir(), True, True, True);

  if ObsDir <> '' then
  begin
    OtherBin := AddBackslash(ObsDir) + 'obs-plugins\64bit';
    OtherData := AddBackslash(ObsDir) + 'data\obs-plugins\{#PluginId}';
    if not SameFolder(OtherBin, TargetBin) and FileExists(AddBackslash(OtherBin) + '{#PluginId}.dll') then
      if ConfirmRemoval(OtherBin) then
      begin
        DeleteFile(AddBackslash(OtherBin) + '{#PluginId}.dll');
        DeleteFile(AddBackslash(OtherBin) + '{#PluginId}.pdb');
        DelTree(OtherData, True, True, True);
      end;
  end;
end;

// A locked DLL means OBS is running: replacing it would silently fail.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Dll, Temp: String;
begin
  Result := '';
  Dll := AddBackslash(GetBinDir('')) + '{#PluginId}.dll';
  if FileExists(Dll) then
  begin
    Temp := Dll + '.busycheck';
    if RenameFile(Dll, Temp) then
      RenameFile(Temp, Dll)
    else
      Result := CustomMessage('ObsRunning');
  end;
  if Result = '' then
    RemoveOtherInstallation();
end;
