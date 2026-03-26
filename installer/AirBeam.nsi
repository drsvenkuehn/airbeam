; AirBeam NSIS installer
; Requires NSIS 3.x, Unicode mode

Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"

Name "AirBeam"
OutFile "..\build\AirBeamSetup.exe"
InstallDir "$PROGRAMFILES64\AirBeam"
InstallDirRegKey HKLM "Software\AirBeam" "InstallDir"
RequestExecutionLevel admin

!define MUI_ABORTWARNING
!define MUI_ICON "..\resources\icons\airbeam_idle.ico"
!define MUI_UNICON "..\resources\icons\airbeam_idle.ico"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\installer\licenses\combined-license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ── Install section ──────────────────────────────────────────────────────────
Section "AirBeam" SecMain
    SectionIn RO
    SetOutPath "$INSTDIR"

    ; ── Bonjour detection + silent install ───────────────────────────────────
    ; Skip if Bonjour Service is already registered in the SCM registry key.
    ClearErrors
    ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Services\Bonjour Service" "ImagePath"
    ${If} ${Errors}
        ; Bonjour not present — extract and run silent installer
        SetOutPath "$PLUGINSDIR"
        File "..\installer\deps\BonjourPSSetup.exe"
        ExecWait '"$PLUGINSDIR\BonjourPSSetup.exe" /quiet /norestart' $0
        ${If} $0 != 0
            MessageBox MB_OK|MB_ICONSTOP \
                "Bonjour installation failed (exit code $0). AirBeam requires Bonjour for speaker discovery.$\n$\nPlease install Bonjour manually from:$\nhttps://support.apple.com/downloads/bonjour-for-windows$\n$\nSetup will now exit."
            Abort
        ${EndIf}
        SetOutPath "$INSTDIR"
    ${EndIf}

    File "..\build\Release\AirBeam.exe"
    File "..\WinSparkle.dll"
    File /nonfatal "..\dnssd.dll"

    ; Store install dir
    WriteRegStr HKLM "Software\AirBeam" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\AirBeam" "Version"    "${VERSION}"

    ; Uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AirBeam" \
        "DisplayName"     "AirBeam"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AirBeam" \
        "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AirBeam" \
        "DisplayIcon"     "$INSTDIR\AirBeam.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AirBeam" \
        "Publisher"       "AirBeam Contributors"

    ; Start menu shortcut
    CreateDirectory "$SMPROGRAMS\AirBeam"
    CreateShortCut  "$SMPROGRAMS\AirBeam\AirBeam.lnk" "$INSTDIR\AirBeam.exe"
    CreateShortCut  "$SMPROGRAMS\AirBeam\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

; ── Uninstall section ─────────────────────────────────────────────────────────
Section "Uninstall"
    ; Remove application files only — preserve %APPDATA%\AirBeam (user data)
    Delete "$INSTDIR\AirBeam.exe"
    Delete "$INSTDIR\WinSparkle.dll"
    Delete "$INSTDIR\dnssd.dll"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir  "$INSTDIR"

    Delete "$SMPROGRAMS\AirBeam\AirBeam.lnk"
    Delete "$SMPROGRAMS\AirBeam\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\AirBeam"

    ; Remove Run key (launch at startup) if set
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "AirBeam"

    ; Remove install registry keys
    DeleteRegKey HKLM "Software\AirBeam"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AirBeam"
SectionEnd
