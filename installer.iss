; ============================================================================
;  Dish Inno Setup Installer Script
;  Requires: Inno Setup 6.5+ (https://jrsoftware.org/isinfo.php)
;
;  Build:  cmake --build build-release --target Dish dish_setup_image
;          iscc /DMyAppVersion=1.0.0 installer.iss
;     or:  pwsh scripts/build-installer.ps1   (reads the version from
;          CMakeLists.txt and finds ISCC for you)
;  Output: dist\dish-setup.exe
;
;  The payload is the staged install image, not the build tree: [Files] ships
;  {#ImageDir}\* verbatim, and cmake/DishSetupImage.cmake is what decides the
;  image's contents (dish.exe, windeployqt output, libsodium/SDL2, the five
;  app-local CRT DLLs, licenses/). Keeping that in one reviewed script is what
;  stops the installer and the portable zip drifting apart file by file.
;
;  Notes:
;    * Per-USER by default (PrivilegesRequired=lowest): no UAC on the default
;      path, which is also what lets the auto-updater apply silently with no
;      prompt. "Install for all users" stays available through the scope
;      dialog (PrivilegesRequiredOverridesAllowed), elevating exactly once.
;    * Restart Manager: if dish.exe is running, a manual install closes it
;      cleanly (WM_QUERYENDSESSION, so QSettings writes land) and restarts it
;      afterwards, instead of failing on locked files.
;    * OTA (/OTA): passed only by dish.exe's boot handoff, which spawns this
;      installer and exits. [Code] waits for the app's presence mutex to
;      clear, installs, and then owns the relaunch duty the app gave up by
;      exiting: new exe on success, OLD exe with --no-update-handoff on any
;      failure. Under no outcome does the user end up with no app.
;    * SetupMutex serialises two racing installers (the old engine's exit 13).
;    * SetupLogging=yes preserves logs under %TEMP% for every interactive
;      run; the OTA path passes an explicit /LOG into the staging directory,
;      which is where PRIVACY.md says the last apply log lives.
;    * Signing is not wired up yet (no certificate; see SECURITY.md). When it
;      lands: define a SignTool named "signtool" via `iscc /Ssigntool=...`
;      and add `SignTool=signtool` plus `sign` flags here, as satellite does.
;    * Bosnian: the app ships bs, but Inno has no official Bosnian.isl. The
;      installer runs in the other five app languages; vendoring the
;      community Bosnian.isl is an open follow-up.
;
;  Switches beyond Inno's standard set (/VERYSILENT, /DIR=, /LOG=, ...):
;    /OTA    boot-handoff apply; wait for the app to exit, then own relaunch
; ============================================================================

#ifndef MyAppVersion
  #error Pass the version: iscc /DMyAppVersion=M.m.p installer.iss (or use scripts/build-installer.ps1)
#endif
#ifndef ImageDir
  #define ImageDir "build-release\setup-image"
#endif

#define MyAppName "Dish"
#define MyAppPublisher "TinkerNorth"
#define MyAppURL "https://github.com/TinkerNorth/dish-windows"
#define MyAppExeName "dish.exe"
#define MyAppCopyright "Copyright (C) 2026 Dish contributors"

; The mutex dish.exe holds for its whole lifetime (UpdateHandoff.h). Unprefixed
; resolves in the caller's session namespace, which is where the app creates
; it as Local\TinkerNorth.Dish.Running.
#define AppRunningMutex "TinkerNorth.Dish.Running"

[Setup]
; Stable AppId: never change this without a migration plan; it is how Windows
; and Inno recognise in-place upgrades of an existing install.
AppId={{0B4F3D3C-9526-4953-9CCA-BAAD2AF4A5A1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
AppCopyright={#MyAppCopyright}

; Per-user by default; the dialog offers "all users" and elevates once.
; A silent re-run inherits the recorded scope and directory of the previous
; install through the AppId, so an OTA apply of a machine-scope install
; self-elevates (one UAC consent) exactly as the old engine did.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; {autopf} follows the scope: %LOCALAPPDATA%\Programs for a user install,
; C:\Program Files for a machine one. Same defaults as the old wizard.
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes

; Windows 10 1809 x64 or newer; ARM64 runs the x64 build under emulation.
; Same gate the old stub enforced as its exit 3.
MinVersion=10.0.17763
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; If dish.exe is running: close it via Restart Manager (graceful, so config
; writes survive), and bring it back when done. This replaces the old
; installer's blocker face and --closeapps.
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll
RestartApplications=yes

; Two installers at once serialise instead of racing (old exit 13).
SetupMutex=TinkerNorth.DishSetup

; Keep a log under %TEMP% for every run so a failed interactive install can
; be diagnosed after the fact. The OTA path overrides with an explicit /LOG.
SetupLogging=yes

; The image ships LGPL-3.0 text in licenses\; surface the project licence in
; the wizard as well.
LicenseFile={#ImageDir}\licenses\LICENSE.LGPL-3.0.txt

OutputDir=dist
OutputBaseFilename=dish-setup
SetupIconFile=packaging\dish.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}

Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; Stamp the setup exe itself so its Properties dialog and SmartScreen
; telemetry name the right version.
VersionInfoVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} Setup
VersionInfoProductName={#MyAppName}

[Languages]
; The five app languages Inno has official catalogues for. Bosnian (bs) is
; app-only until the community .isl is vendored; see the header note.
Name: "english";    MessagesFile: "compiler:Default.isl"
Name: "german";     MessagesFile: "compiler:Languages\German.isl"
Name: "spanish";    MessagesFile: "compiler:Languages\Spanish.isl"
Name: "french";     MessagesFile: "compiler:Languages\French.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
; Start Menu is always created (the old wizard's default-on); Desktop stays
; the old default-off, opt-in.
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The staged install image, verbatim. cmake/DishSetupImage.cmake owns the
; list; nothing is enumerated here so the two can never disagree.
Source: "{#ImageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; Interactive installs offer a launch checkbox (the old wizard's default-on
; "Launch Dish"). Silent installs skip it; the OTA relaunch is [Code]'s duty.
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: shellexec nowait postinstall skipifsilent

[UninstallDelete]
; The updater's download cache is not user data (PRIVACY.md). Satellites,
; pairings and preferences under HKCU and %LOCALAPPDATA%\Dish are kept, so a
; reinstall restores them; PRIVACY.md documents how to remove them by hand.
Type: filesandordirs; Name: "{localappdata}\Dish\updates"

[Code]
{ --- /OTA: the boot-handoff apply -------------------------------------------
  dish.exe's startup gate (src/update/UpdateHandoff.cpp) spawns this installer
  from the staging directory with

    /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /OTA /LOG="<stage>\apply.log"

  and then exits so its own files are replaceable. That hands two duties to
  this script: wait until the app's presence mutex actually clears before
  touching files, and make sure SOME dish.exe runs afterwards - the new one on
  success, the old one (which Inno's rollback restored) with the loop-breaker
  flag on failure. The flag makes the relaunched old exe skip its boot gate
  once; the attempt counter it already synced bounds retries at two. }

var
  OtaInstallSucceeded: Boolean;

function WantsOTA: Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/OTA') = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function InitializeSetup: Boolean;
var
  Waited: Integer;
begin
  Result := True;
  if not WantsOTA then
    Exit;
  { The spawning dish.exe is mid-exit. Its Running mutex dies with the
    process; poll it rather than a pid so the wait needs no handle plumbing.
    60 s mirrors the old engine's --waitpid budget. On timeout, carry on:
    Restart Manager and Inno's own in-use handling are the second line. }
  Waited := 0;
  while CheckForMutexes('{#AppRunningMutex}') and (Waited < 60000) do
  begin
    Sleep(250);
    Waited := Waited + 250;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;
  if not WantsOTA then
    Exit;
  OtaInstallSucceeded := True;
  { Success leg of the relaunch duty. The app was not running (it exited to
    let this apply), so RestartApplications has nothing to restart; start the
    freshly installed exe ourselves. No arguments, per the handoff contract:
    the new exe's janitor discards the now-stale stage on its own. }
  ExecAsOriginalUser(ExpandConstant('{app}\{#MyAppExeName}'), '', ExpandConstant('{app}'),
    SW_SHOWNORMAL, ewNoWait, ResultCode);
end;

procedure DeinitializeSetup;
var
  ExePath: String;
  ResultCode: Integer;
begin
  if not WantsOTA then
    Exit;
  if OtaInstallSucceeded then
    Exit;
  { Failure leg: the install aborted or rolled back, the invoking app is
    already dead, and nobody else will start it. Relaunch the OLD exe, which
    rollback left in place, with the boot gate's skip-once flag so it does
    not immediately respawn this installer. Guarded with try/except because a
    failure before the previous-install lookup leaves the app-dir constant
    unresolvable. }
  try
    ExePath := ExpandConstant('{app}\{#MyAppExeName}');
  except
    Exit;
  end;
  if FileExists(ExePath) then
    ExecAsOriginalUser(ExePath, '--no-update-handoff', ExtractFilePath(ExePath),
      SW_SHOWNORMAL, ewNoWait, ResultCode);
end;
