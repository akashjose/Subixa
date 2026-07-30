; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Akash Jose
;
; The Windows installer. Driven by tools/make-installer-win.sh, which passes the
; version and the staged directory in; run ISCC against this directly only when
; testing the script itself.
;
; What it wraps is the output of tools/deploy-win.sh -- a folder that already
; runs. Nothing is added beyond the uninstaller and the shortcuts, which is the
; point: if the staged folder is wrong then the installer is wrong in exactly
; the same way, and there is no second packing step with its own bugs.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\subixa-win64"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif

[Setup]
; A reverse-DNS id rather than a GUID. Inno only requires this to be stable and
; unique -- it keys the uninstall entry -- and reusing the application id that
; already names the desktop entry and the AppStream metainfo keeps one identity
; across all three platforms instead of inventing a fourth.
AppId=com.akashjose.Subixa
AppName=Subixa
AppVersion={#AppVersion}
AppVerName=Subixa {#AppVersion}
AppPublisher=Akash Jose
AppPublisherURL=https://github.com/akashjose/Subixa
AppSupportURL=https://github.com/akashjose/Subixa/issues
AppUpdatesURL=https://github.com/akashjose/Subixa/releases
VersionInfoVersion={#AppVersion}

; {autopf} is Program Files when elevated and the per-user Programs folder when
; not, and PrivilegesRequiredOverridesAllowed lets the user choose which. The
; default is the unelevated one: nothing here needs administrator rights, and an
; unsigned installer asking for them is exactly the prompt people decline.
DefaultDirName={autopf}\Subixa
DefaultGroupName=Subixa
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; The staged tree is a 64-bit build against UCRT; there is no 32-bit variant to
; fall back to, so refuse rather than install something that cannot start.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

LicenseFile=..\LICENSE
SetupIconFile=..\icons\subixa.ico
UninstallDisplayIcon={app}\subixa.exe
WizardStyle=modern

; ~300 MB of Qt, libmpv and FFmpeg. Solid LZMA2 is slow to produce and roughly
; a third the size, which is the right trade for something downloaded far more
; often than it is built.
Compression=lzma2/max
SolidCompression=yes

OutputDir={#OutDir}
OutputBaseFilename=subixa-{#AppVersion}-win64-setup

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Subixa"; Filename: "{app}\subixa.exe"
Name: "{autodesktop}\Subixa"; Filename: "{app}\subixa.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\subixa.exe"; Description: "Launch Subixa"; Flags: nowait postinstall skipifsilent
