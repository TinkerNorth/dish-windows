; ============================================================================
;  Dish Inno Setup Installer Script
;  Requires: Inno Setup 6.5+ (https://jrsoftware.org/isinfo.php). CI and
;  release.yml compile with the 6.7.4 that scripts/tools.ps1 pins.
;
;  Build:  cmake --build build-release --target Dish dish_setup_image
;          iscc /DMyAppVersion=1.0.0 installer.iss
;     or:  pwsh scripts/build-installer.ps1   (reads the version from
;          CMakeLists.txt and finds ISCC for you)
;  Output: dist\dish-setup.exe
;
;  The payload is the staged install image, not the build tree: [Files] ships
;  {#ImageDir}\* verbatim, and cmake/DishSetupImage.cmake is what decides the
;  image's contents (dish.exe, windeployqt output, libsodium/SDL2/opus/
;  libcrypto, sentry.dll and crashpad_handler.exe, the five app-local CRT
;  DLLs, licenses/). Keeping that in one reviewed script is what
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
;    * Signing happens after compile, not here: release.yml signs
;      dist\dish-setup.exe with signtool when the WIN_SIGN_* secrets exist,
;      before the round-trip, so the bytes tested are the bytes shipped. The
;      uninstaller inside it is not signed (that would need SignTool= here).
;    * Bosnian: the app ships bs, but Inno has no official Bosnian.isl. The
;      installer runs in the other five app languages; vendoring the
;      community Bosnian.isl is an open follow-up.
;
;  Switches beyond Inno's standard set (/VERYSILENT, /DIR=, /LOG=, ...):
;    /OTA    boot-handoff apply; wait for the app to exit, then own relaunch
;    /UPDATECHECK=yes|no, /CRASHREPORTS=yes|no, /CONTROLLERUPDATES=yes|no
;            the Privacy page's three answers on a run that shows no page
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

#define PrivacyUrl "https://github.com/TinkerNorth/dish-windows/blob/main/PRIVACY.md"
#define InstallDefaultsKey "Software\TinkerNorth\Dish\InstallDefaults"

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

[CustomMessages]
english.PrivacyTitle=Privacy
english.PrivacySubtitle=What leaves this PC, and your choices
english.PrivacyDescription=Dish talks only to the host you pair with, on your own network. Three things can leave this PC, and you can turn each one off: an update check against GitHub (about every four hours while Dish runs); when Dish crashes, one crash report to Sentry (a stack trace, a minidump of the crashed process and version numbers); and a check for a newer controller list against dish.tinkernorth.com, hosted by Amazon Web Services (about ten seconds after launch, then every six hours). Report a problem never sends anything by itself: you send the mail from your own mail app.
english.PrivacyUpdateCheck=Check for updates automatically (contacts GitHub)
english.PrivacyCrashReports=Send a crash report when Dish crashes (Sentry)
english.PrivacyControllerUpdates=Check for controller updates automatically (contacts dish.tinkernorth.com)
english.PrivacyKept=Your existing choices are kept. Change them at any time in Settings: Check for updates automatically, Share crash reports, and Check for controller updates automatically.
english.PrivacyPolicyLink=Full privacy policy:
german.PrivacyTitle=Datenschutz
german.PrivacySubtitle=Was diesen PC verlässt, und was Sie entscheiden
german.PrivacyDescription=Dish spricht nur mit dem Host, den Sie in Ihrem eigenen Netzwerk koppeln. Drei Dinge können diesen PC verlassen, und jedes lässt sich abschalten: eine Update-Prüfung bei GitHub (etwa alle vier Stunden, solange Dish läuft); wenn Dish abstürzt, ein Absturzbericht an Sentry (ein Stacktrace, ein Minidump des abgestürzten Prozesses und Versionsnummern); und eine Prüfung bei dish.tinkernorth.com auf eine neuere Controller-Liste, gehostet von Amazon Web Services (etwa zehn Sekunden nach dem Start, dann alle sechs Stunden). Ein Problem melden sendet nie etwas von selbst: Sie senden die Mail aus Ihrer eigenen Mail-App.
german.PrivacyUpdateCheck=Automatisch nach Updates suchen (kontaktiert GitHub)
german.PrivacyCrashReports=Bei einem Absturz einen Absturzbericht senden (Sentry)
german.PrivacyControllerUpdates=Automatisch nach Controller-Updates suchen (kontaktiert dish.tinkernorth.com)
german.PrivacyKept=Ihre bisherigen Einstellungen bleiben erhalten. Sie können sie jederzeit in den Einstellungen ändern: Automatisch nach Updates suchen, Absturzberichte senden und Automatisch nach Controller-Updates suchen.
german.PrivacyPolicyLink=Vollständige Datenschutzerklärung:
spanish.PrivacyTitle=Privacidad
spanish.PrivacySubtitle=Qué sale de este PC, y qué decide usted
spanish.PrivacyDescription=Dish solo habla con el host que usted empareja, en su propia red. Tres cosas pueden salir de este PC, y cada una se puede desactivar: una comprobación de actualizaciones en GitHub (cada cuatro horas aproximadamente mientras Dish está abierto); si Dish falla, un informe de fallo a Sentry (una traza de pila, un minivolcado del proceso que falló y los números de versión); y una consulta a dish.tinkernorth.com, alojado en Amazon Web Services, por una lista de mandos más reciente (unos diez segundos después de arrancar y luego cada seis horas). Informar de un problema nunca envía nada por sí solo: usted envía el correo desde su propia app de correo.
spanish.PrivacyUpdateCheck=Buscar actualizaciones automáticamente (contacta con GitHub)
spanish.PrivacyCrashReports=Enviar un informe de fallo cuando Dish falle (Sentry)
spanish.PrivacyControllerUpdates=Buscar actualizaciones de mandos automáticamente (contacta con dish.tinkernorth.com)
spanish.PrivacyKept=Se conservan sus opciones actuales. Puede cambiarlas en cualquier momento en Ajustes: Buscar actualizaciones automáticamente, Compartir informes de fallos y Buscar actualizaciones de mandos automáticamente.
spanish.PrivacyPolicyLink=Política de privacidad completa:
french.PrivacyTitle=Confidentialité
french.PrivacySubtitle=Ce qui quitte ce PC, et vos choix
french.PrivacyDescription=Dish ne communique qu'avec l'hôte que vous associez, sur votre propre réseau. Trois choses peuvent quitter ce PC, et vous pouvez désactiver chacune d'elles : une vérification des mises à jour auprès de GitHub (environ toutes les quatre heures tant que Dish est ouvert) ; si Dish plante, un rapport de plantage envoyé à Sentry (la pile d'appels, un minidump du processus planté et les numéros de version) ; et une recherche d'une liste de manettes plus récente auprès de dish.tinkernorth.com, hébergé par Amazon Web Services (environ dix secondes après le démarrage, puis toutes les six heures). Signaler un problème n'envoie jamais rien de lui-même : c'est vous qui envoyez le message depuis votre propre application de messagerie.
french.PrivacyUpdateCheck=Rechercher les mises à jour automatiquement (contacte GitHub)
french.PrivacyCrashReports=Envoyer un rapport de plantage quand Dish plante (Sentry)
french.PrivacyControllerUpdates=Rechercher les mises à jour des manettes automatiquement (contacte dish.tinkernorth.com)
french.PrivacyKept=Vos choix actuels sont conservés. Vous pouvez les modifier à tout moment dans les Paramètres : Rechercher les mises à jour automatiquement, Partager les rapports de plantage et Rechercher les mises à jour des manettes automatiquement.
french.PrivacyPolicyLink=Politique de confidentialité complète :
brazilianportuguese.PrivacyTitle=Privacidade
brazilianportuguese.PrivacySubtitle=O que sai deste PC, e o que você decide
brazilianportuguese.PrivacyDescription=O Dish só fala com o host que você emparelha, na sua própria rede. Três coisas podem sair deste PC, e cada uma pode ser desativada: uma verificação de atualizações no GitHub (cerca de a cada quatro horas enquanto o Dish está aberto); se o Dish travar, um relatório de falha para o Sentry (o rastreamento de pilha, um minidump do processo que travou e os números de versão); e uma consulta a dish.tinkernorth.com, hospedado na Amazon Web Services, em busca de uma lista de controles mais nova (cerca de dez segundos depois de iniciar e, depois, a cada seis horas). Relatar um problema nunca envia nada sozinho: é você quem envia o e-mail pelo seu próprio app de e-mail.
brazilianportuguese.PrivacyUpdateCheck=Verificar atualizações automaticamente (contata o GitHub)
brazilianportuguese.PrivacyCrashReports=Enviar um relatório de falha quando o Dish travar (Sentry)
brazilianportuguese.PrivacyControllerUpdates=Verificar atualizações de controles automaticamente (contata dish.tinkernorth.com)
brazilianportuguese.PrivacyKept=Suas escolhas atuais são mantidas. Você pode alterá-las a qualquer momento em Configurações: Verificar atualizações automaticamente, Compartilhar relatórios de falhas e Verificar atualizações de controles automaticamente.
brazilianportuguese.PrivacyPolicyLink=Política de privacidade completa:

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

[Registry]
Root: HKA; Subkey: "{#InstallDefaultsKey}"; Flags: deletekey; Check: WritesInstallDefaults
Root: HKA; Subkey: "{#InstallDefaultsKey}"; ValueType: string; ValueName: "Stamp"; ValueData: "{code:InstallStamp}"; Flags: uninsdeletekey; Check: WritesInstallDefaults
Root: HKA; Subkey: "{#InstallDefaultsKey}"; ValueType: dword; ValueName: "UpdateCheck"; ValueData: "{code:UpdateCheckValue}"; Check: WritesUpdateCheck
Root: HKA; Subkey: "{#InstallDefaultsKey}"; ValueType: dword; ValueName: "CrashReports"; ValueData: "{code:CrashReportsValue}"; Check: WritesCrashReports
Root: HKA; Subkey: "{#InstallDefaultsKey}"; ValueType: dword; ValueName: "ControllerUpdates"; ValueData: "{code:ControllerUpdatesValue}"; Check: WritesControllerUpdates

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
  UpgradeInstall: Boolean;
  InstallStampValue: String;
  PrivacyPage: TInputOptionWizardPage;
  PrivacyChoicesOffered: Boolean;

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

function SwitchValue(const Name: String): String;
var
  I: Integer;
  Param, Prefix: String;
begin
  Result := '';
  Prefix := '/' + Name + '=';
  for I := 1 to ParamCount do
  begin
    Param := ParamStr(I);
    if CompareText(Copy(Param, 1, Length(Prefix)), Prefix) = 0 then
    begin
      Result := Copy(Param, Length(Prefix) + 1, Length(Param));
      Exit;
    end;
  end;
end;

function SwitchGiven(const Name: String): Boolean;
var
  Value: String;
begin
  Value := SwitchValue(Name);
  Result := (CompareText(Value, 'yes') = 0) or (CompareText(Value, 'no') = 0);
end;

function SwitchEnabled(const Name: String): Boolean;
begin
  Result := CompareText(SwitchValue(Name), 'no') <> 0;
end;

function WritesInstallDefaults: Boolean;
begin
  Result := (not WantsOTA) and
    ((not UpgradeInstall) or SwitchGiven('UPDATECHECK') or SwitchGiven('CRASHREPORTS') or
     SwitchGiven('CONTROLLERUPDATES'));
end;

function WritesUpdateCheck: Boolean;
begin
  Result := WritesInstallDefaults and ((not UpgradeInstall) or SwitchGiven('UPDATECHECK'));
end;

function WritesCrashReports: Boolean;
begin
  Result := WritesInstallDefaults and ((not UpgradeInstall) or SwitchGiven('CRASHREPORTS'));
end;

function WritesControllerUpdates: Boolean;
begin
  Result := WritesInstallDefaults and ((not UpgradeInstall) or SwitchGiven('CONTROLLERUPDATES'));
end;

function DwordText(const Enabled: Boolean): String;
begin
  if Enabled then
    Result := '1'
  else
    Result := '0';
end;

function InstallStamp(Param: String): String;
begin
  Result := InstallStampValue;
end;

function UpdateCheckValue(Param: String): String;
begin
  if PrivacyChoicesOffered then
    Result := DwordText(PrivacyPage.Values[0])
  else
    Result := DwordText(SwitchEnabled('UPDATECHECK'));
end;

function CrashReportsValue(Param: String): String;
begin
  if PrivacyChoicesOffered then
    Result := DwordText(PrivacyPage.Values[1])
  else
    Result := DwordText(SwitchEnabled('CRASHREPORTS'));
end;

function ControllerUpdatesValue(Param: String): String;
begin
  if PrivacyChoicesOffered then
    Result := DwordText(PrivacyPage.Values[2])
  else
    Result := DwordText(SwitchEnabled('CONTROLLERUPDATES'));
end;

procedure PrivacyLinkClick(Sender: TObject);
var
  ErrorCode: Integer;
begin
  ShellExecAsOriginalUser('open', '{#PrivacyUrl}', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;

procedure AddPrivacyChoices;
begin
  PrivacyPage.Add(CustomMessage('PrivacyUpdateCheck'));
  PrivacyPage.Add(CustomMessage('PrivacyCrashReports'));
  PrivacyPage.Add(CustomMessage('PrivacyControllerUpdates'));
  PrivacyPage.Values[0] := SwitchEnabled('UPDATECHECK');
  PrivacyPage.Values[1] := SwitchEnabled('CRASHREPORTS');
  PrivacyPage.Values[2] := SwitchEnabled('CONTROLLERUPDATES');
  PrivacyChoicesOffered := True;
end;

procedure AddPrivacyKeptNote;
var
  Note: TNewStaticText;
begin
  PrivacyPage.CheckListBox.Visible := False;
  Note := TNewStaticText.Create(PrivacyPage);
  Note.AutoSize := False;
  Note.WordWrap := True;
  Note.Top := PrivacyPage.CheckListBox.Top;
  Note.Width := PrivacyPage.SurfaceWidth;
  Note.Caption := CustomMessage('PrivacyKept');
  Note.Parent := PrivacyPage.Surface;
  Note.AdjustHeight;
end;

procedure AddPrivacyLink;
var
  Link: TNewStaticText;
begin
  Link := TNewStaticText.Create(PrivacyPage);
  Link.AutoSize := False;
  Link.WordWrap := True;
  Link.Width := PrivacyPage.SurfaceWidth;
  Link.Caption := CustomMessage('PrivacyPolicyLink') + ' {#PrivacyUrl}';
  Link.Cursor := crHand;
  Link.Font.Style := [fsUnderline];
  Link.StyleElements := Link.StyleElements - [seFont];
  if IsDarkInstallMode then
    Link.Font.Color := StrToColor('#4DA3FF')
  else
    Link.Font.Color := StrToColor('#0063B1');
  Link.OnClick := @PrivacyLinkClick;
  Link.Parent := PrivacyPage.Surface;
  Link.AdjustHeight;
  Link.Top := PrivacyPage.SurfaceHeight - Link.Height;
  PrivacyPage.CheckListBox.Height := Link.Top - PrivacyPage.CheckListBox.Top - ScaleY(8);
end;

procedure InitializeWizard;
begin
  if WantsOTA then
    Exit;
  PrivacyPage := CreateInputOptionPage(wpSelectTasks, CustomMessage('PrivacyTitle'),
    CustomMessage('PrivacySubtitle'), CustomMessage('PrivacyDescription'), False, False);
  if UpgradeInstall then
    AddPrivacyKeptNote
  else
    AddPrivacyChoices;
  AddPrivacyLink;
end;

function InitializeSetup: Boolean;
var
  Waited: Integer;
begin
  Result := True;
  InstallStampValue := GetDateTimeString('yyyymmddhhnnss', #0, #0) + '-' +
    IntToStr(Random(1000000000));
  UpgradeInstall := RegKeyExists(HKA,
    ExpandConstant('Software\Microsoft\Windows\CurrentVersion\Uninstall\{#SetupSetting("AppId")}_is1'));
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
