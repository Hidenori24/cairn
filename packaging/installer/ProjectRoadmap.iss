; Inno Setup script for Project Roadmap. ASCII only on purpose (no BOM needed). Messages come from the language files.
; Build:
;   ISCC /DAppVersion=0.3.0 /DSourceDir=<portable folder> /DRepoDir=<repo root> /DOutDir=<output folder> ProjectRoadmap.iss
; <portable folder> is what "cmake --install <build> --component portable --prefix <dir>" makes.
;
; Install location: per-user by default (no administrator rights): %LOCALAPPDATA%\Programs\ProjectRoadmap.
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
; Never change AppId: it is how upgrades and the uninstaller find this app.
AppId={{DECF5CA4-1926-49F6-B7A9-0E818177512D}
AppName=Project Roadmap
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\ProjectRoadmap
DefaultGroupName=Project Roadmap
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutDir}
OutputBaseFilename=ProjectRoadmap-Setup-{#AppVersion}
SetupIconFile={#RepoDir}\app\resources\app.ico
UninstallDisplayIcon={app}\ProjectRoadmap.exe
UninstallDisplayName=Project Roadmap
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
Source: "{#SourceDir}\ProjectRoadmap.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\rmcli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\THIRD_PARTY.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\samples\*"; DestDir: "{app}\samples"; Flags: ignoreversion recursesubdirs
; Not overwritten if the user changed or removed it, and left behind on uninstall.
Source: "{#SourceDir}\data\projects\README.txt"; DestDir: "{app}\data\projects"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{autoprograms}\Project Roadmap"; Filename: "{app}\ProjectRoadmap.exe"
Name: "{autodesktop}\Project Roadmap"; Filename: "{app}\ProjectRoadmap.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ProjectRoadmap.exe"; Description: "{cm:LaunchProgram,Project Roadmap}"; Flags: nowait postinstall skipifsilent
