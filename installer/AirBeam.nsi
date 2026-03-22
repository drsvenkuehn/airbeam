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
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
Page custom BonjourPage BonjourPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ── Bonjour detection page ───────────────────────────────────────────────────
Var BonjourMissing

Function BonjourPage
    ; Check for dnssd.dll via registry
    ClearErrors
    ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Services\Bonjour Service" "ImagePath"
    ${If} ${Errors}
        StrCpy $BonjourMissing "1"
        nsDialogs::Create 1018
        Pop $Dialog
        ${NSD_CreateLabel} 0 0 100% 40u "Bonjour (required for speaker discovery) is not installed. Download it from support.apple.com/downloads/bonjour-for-windows and install it, then click Next."
        Pop $0
        nsDialogs::Show
    ${EndIf}
FunctionEnd

Function BonjourPageLeave
FunctionEnd

; ── Install section ──────────────────────────────────────────────────────────
Section "AirBeam" SecMain
    SectionIn RO
    SetOutPath "$INSTDIR"

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
