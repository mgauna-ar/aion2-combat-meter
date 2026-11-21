; AION 2 Combat Meter's Windows installer: a per-user install (no administrator prompt)
; of the same aion2-combat-meter.exe the release zip holds. CI builds it around
; build-windows' exe (.github/workflows/ci.yml, the installer job):
;
;   makensis /DEXE=C:\path\aion2-combat-meter.exe /DVERSION=1.7.0 /DOUTFILE=C:\path\setup.exe installer.nsi
;
; The folder it installs to is the user's own, so Update & restart swaps the exe in
; place there, as in a folder the zip was extracted to. The updater keeps downloading
; the zip: this installer is no part of what installed copies expect of a release.
; Settings, fights, recordings and the log are in %APPDATA%\aion2-combat-meter, not
; here: uninstalling keeps them unless the user asks otherwise.
;
; Npcap is not bundled: its licence does not allow it. The installer only says so, and
; offers its site, when System32\Npcap\wpcap.dll (what the meter loads) is missing.

Unicode true
SetCompressor /SOLID lzma
ManifestDPIAware true

!ifndef EXE
  !error "Pass the meter with /DEXE=C:\path\aion2-combat-meter.exe"
!endif
; include/aion2/version.hpp's AION2_VERSION_STRING, always MAJOR.MINOR.PATCH
!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef OUTFILE
  !define OUTFILE "aion2-combat-meter-windows-x64-setup.exe"
!endif

!define PRODUCT_NAME "AION 2 Combat Meter"
!define PUBLISHER "aion2-combat-meter contributors"
!define PRODUCT_URL "https://github.com/mgauna-ar/aion2-combat-meter"
; UPDATE_EXE_NAME (src/app/include/app/update_check.hpp): Update & restart replaces it
; in its folder, installed or not
!define EXE_NAME "aion2-combat-meter.exe"
; os/install_entry.cpp keeps its DisplayVersion in step after an in-app update
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\AION2CombatMeter"
; Start with Windows (os/auto_start.cpp): the exe, quoted
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define RUN_VALUE "AION2CombatMeter"
; The app window's class (win32/app_window.hpp) and the overlays' (win32/overlay_window.cpp):
; both exist while the meter runs, also when it is only in the tray
!define APP_WINDOW_CLASS "Aion2CombatMeterApp"
!define OVERLAY_WINDOW_CLASS "Aion2CombatMeterOverlay"
; What Update & restart leaves in the install folder (app/updater.cpp)
!define UPDATE_STAGING ".aion2-combat-meter-update"
; os::app_data_dir()
!define DATA_DIR "$APPDATA\aion2-combat-meter"
!define NPCAP_URL "https://npcap.com/#download"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "WinVer.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

Name "${PRODUCT_NAME}"
OutFile "${OUTFILE}"
BrandingText "${PRODUCT_NAME} ${VERSION}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${PRODUCT_NAME}"
; Installing again goes to the folder it went to before
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
ShowInstDetails show
ShowUninstDetails show

!define MUI_ICON "..\..\src\app\resources\app.ico"
!define MUI_UNICON "..\..\src\app\resources\app.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE_NAME}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${PRODUCT_NAME} now"

!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE CheckNotRunning
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${VERSION}.0"
VIFileVersion "${VERSION}.0"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "${PRODUCT_NAME} Setup"
VIAddVersionKey /LANG=${LANG_ENGLISH} "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "Copyright (c) ${PUBLISHER}"

Function .onInit
  ${IfNot} ${AtLeastWin10}
  ${OrIfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "${PRODUCT_NAME} needs 64-bit Windows 10 or later." /SD IDOK
    Abort
  ${EndIf}
  ; With no pages, a running meter would fail the copy halfway: refuse at once
  ${If} ${Silent}
    Call CheckNotRunning
  ${EndIf}
FunctionEnd

; The exe cannot be replaced or removed while the meter runs, and closing its window
; leaves it in the tray: ask to exit it first. Cancel stays on the page (the uninstaller
; stops). A copy running from another folder counts too: the single-instance lock
; would keep the installed one from starting.
!macro CHECK_NOT_RUNNING UN
Function ${UN}CheckNotRunning
  retry:
    FindWindow $0 "${APP_WINDOW_CLASS}"
    ${If} $0 = 0
      FindWindow $0 "${OVERLAY_WINDOW_CLASS}"
    ${EndIf}
    ${If} $0 <> 0
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
        "${PRODUCT_NAME} is running. Exit it from its icon in the notification area, then click Retry." \
        /SD IDCANCEL IDRETRY retry
      Abort
    ${EndIf}
FunctionEnd
!macroend
!insertmacro CHECK_NOT_RUNNING ""
!insertmacro CHECK_NOT_RUNNING "un."

Function un.onInit
  Call un.CheckNotRunning
FunctionEnd

; The meter loads wpcap.dll from the 64-bit System32\Npcap, which a 32-bit installer
; sees only with the file system redirection off.
Function CheckNpcap
  ${DisableX64FSRedirection}
  ${If} ${FileExists} "$SYSDIR\Npcap\wpcap.dll"
    StrCpy $0 1
  ${Else}
    StrCpy $0 0
  ${EndIf}
  ${EnableX64FSRedirection}
  ${If} $0 = 1
    Return
  ${EndIf}

  DetailPrint "Npcap is not installed: the meter needs it to read the game's traffic."
  MessageBox MB_YESNO|MB_ICONINFORMATION \
    "${PRODUCT_NAME} needs Npcap to read the game's traffic, and Npcap's licence does not let it be bundled.$\r$\n$\r$\n\
In Npcap's installer:$\r$\n\
- untick $\"Restrict Npcap driver's access to Administrators only$\",$\r$\n\
- tick $\"Support loopback traffic$\" if you play through a ping reducer or a VPN.$\r$\n$\r$\n\
Open npcap.com now?" \
    /SD IDNO IDNO done
  ExecShell "open" "${NPCAP_URL}"
  done:
FunctionEnd

Section "Install"
  SetShellVarContext current

  SetOutPath "$INSTDIR"
  File "/oname=${EXE_NAME}" "${EXE}"
  CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}.lnk" "$INSTDIR\${EXE_NAME}"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "${PRODUCT_URL}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EXE_NAME}"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/M=${EXE_NAME} /S=0K /G=0" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" "$0"

  Call CheckNpcap
SectionEnd

Section "Uninstall"
  SetShellVarContext current

  ; Start with Windows goes only when it starts this copy; a copy in another folder
  ; keeps its own.
  ReadRegStr $0 HKCU "${RUN_KEY}" "${RUN_VALUE}"
  ${If} $0 == '"$INSTDIR\${EXE_NAME}"'
    DeleteRegValue HKCU "${RUN_KEY}" "${RUN_VALUE}"
  ${EndIf}

  Delete "$INSTDIR\${EXE_NAME}"
  ; Update & restart's backups, "<name>.<old version>.old", and a download it staged
  Delete "$INSTDIR\${EXE_NAME}.*.old"
  RMDir /r "$INSTDIR\${UPDATE_STAGING}"
  Delete "$INSTDIR\uninstall.exe"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}.lnk"
  DeleteRegKey HKCU "${UNINST_KEY}"
  ; Only when empty: whatever else the user put there stays
  RMDir "$INSTDIR"

  ; Fights and recordings hold character names: offer to take them too, never by
  ; default (a silent uninstall keeps them).
  ${If} ${FileExists} "${DATA_DIR}\*.*"
    MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 \
      "Also delete your settings, fight history and recordings?$\r$\n$\r$\n\
They are in ${DATA_DIR}, which every copy of the meter on this PC shares." \
      /SD IDNO IDNO keep
    RMDir /r "${DATA_DIR}"
    keep:
  ${EndIf}
SectionEnd
