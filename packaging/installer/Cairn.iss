; Inno Setup script for Cairn (formerly "Project Roadmap"; renamed in Issue #82). ASCII only on purpose (no BOM needed). Messages come from the language files.
; Build:
;   ISCC /DAppVersion=0.3.0 /DSourceDir=<portable folder> /DRepoDir=<repo root> /DOutDir=<output folder> Cairn.iss
; <portable folder> is what "cmake --install <build> --component portable --prefix <dir>" makes.
;
; Install location: per-user by default (no administrator rights): %LOCALAPPDATA%\Programs\Cairn.
; An upgrade over a version installed as "Project Roadmap" keeps that folder (same AppId, so Inno Setup reuses the previous folder);
; the old exe and the old shortcuts are removed by [InstallDelete]. The data folder and roadmap_prefs.json are not touched.
; The user can choose "for all users" (Program Files); the app then saves under %LOCALAPPDATA% if the data folder is not writable.
; The data folder (data\projects) and roadmap_prefs.json are NEVER removed by the uninstaller or overwritten by an upgrade.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir is required
#endif
#ifndef RepoDir
  #define RepoDir "..\.."
#endif
#ifndef OutDir
  #define OutDir "."
#endif

[Setup]
; Never change AppId: it is how upgrades and the uninstaller find this app (it is also what lets Cairn upgrade a "Project Roadmap" install).
AppId={{DECF5CA4-1926-49F6-B7A9-0E818177512D}
AppName=Cairn
AppPublisher=Hidenori24
AppCopyright=Copyright (c) 2026 Hidenori24
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Cairn
DefaultGroupName=Cairn
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutDir}
OutputBaseFilename=Cairn-Setup-{#AppVersion}
SetupIconFile={#RepoDir}\app\resources\app.ico
UninstallDisplayIcon={app}\Cairn.exe
UninstallDisplayName=Cairn
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Dirs]
Name: "{app}\data\projects"
; Only an all-users install needs the ACL change (a per-user install already owns the folder).
Name: "{app}\data\projects"; Permissions: users-modify; Check: IsAdminInstallMode

[Files]
Source: "{#SourceDir}\Cairn.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\rmcli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\THIRD_PARTY.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\samples\*"; DestDir: "{app}\samples"; Flags: ignoreversion recursesubdirs
; Not overwritten if the user changed or removed it, and left behind on uninstall.
Source: "{#SourceDir}\data\projects\README.txt"; DestDir: "{app}\data\projects"; Flags: onlyifdoesntexist uninsneveruninstall

[InstallDelete]
; Leftovers of the old name ("Project Roadmap"), so an upgrade does not leave a second exe or duplicate shortcuts.
; Only the exe and the shortcuts are removed. The data folder, roadmap_prefs.json and project.settings.json keep their names and stay.
Type: files; Name: "{app}\ProjectRoadmap.exe"
Type: files; Name: "{autoprograms}\Project Roadmap.lnk"
Type: files; Name: "{autodesktop}\Project Roadmap.lnk"
Type: files; Name: "{userprograms}\Project Roadmap.lnk"
Type: files; Name: "{userdesktop}\Project Roadmap.lnk"

[Icons]
Name: "{autoprograms}\Cairn"; Filename: "{app}\Cairn.exe"
Name: "{autodesktop}\Cairn"; Filename: "{app}\Cairn.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Cairn.exe"; Description: "{cm:LaunchProgram,Cairn}"; Flags: nowait postinstall skipifsilent
