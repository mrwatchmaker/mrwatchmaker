; MrWatchmaker Windows installer (NSIS)
; Usage: makensis -DAPPVERSION=1.0 -DSRCDIR=C:\mrwatchmaker -DOUTFILE=...\MrWatchmaker_Setup_1.0.exe packaging\installer.nsi
;
; Bundles the exe + all runtime DLLs + resources into a single Setup.exe.
; Installs per-user (no admin) so it runs on any PC, and the app can write
; camera.conf next to the exe without permission problems.

Unicode true

!define APPNAME "MrWatchmaker"
!define COMPANY "Marcello Mamino"
!define APPEXE "mrwatchmaker.exe"

!ifndef APPVERSION
  !define APPVERSION "1.0"
!endif
!ifndef SRCDIR
  !define SRCDIR "."
!endif
!ifndef OUTFILE
  !define OUTFILE "MrWatchmaker_Setup_${APPVERSION}.exe"
!endif

!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"

Name "${APPNAME} ${APPVERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${APPNAME}"
InstallDirRegKey HKCU "Software\${APPNAME}" "InstallDir"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

!include "MUI2.nsh"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APPEXE}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; First language is the default
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${APPVERSION}.0.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "CompanyName" "${COMPANY}"
VIAddVersionKey "FileVersion" "${APPVERSION}"
VIAddVersionKey "FileDescription" "${APPNAME} Installer"
VIAddVersionKey "LegalCopyright" "${COMPANY}"

Section "Install"
  SetOutPath "$INSTDIR"

  ; Executables
  File "${SRCDIR}\${APPEXE}"
  File /nonfatal "${SRCDIR}\port_finder.exe"

  ; All runtime DLLs (OpenCV/GTK etc. - matched to this build)
  File "${SRCDIR}\*.dll"

  ; Resources / docs
  File /nonfatal "${SRCDIR}\ch_baseline_ref.png"
  File /nonfatal "${SRCDIR}\reference_9oclock.png"
  File /nonfatal "${SRCDIR}\LICENSE"
  File /nonfatal "${SRCDIR}\README.md"

  ; Hardware calibration configs (include if present)
  File /nonfatal "${SRCDIR}\custom_positions.conf"
  File /nonfatal "${SRCDIR}\port.conf"
  File /nonfatal "${SRCDIR}\mrwatchmaker_coords.txt"

  ; Shortcuts
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\${APPEXE}" 0
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\uninstall.exe"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\${APPEXE}" 0

  ; Registry (uninstall info - per user)
  WriteRegStr HKCU "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${APPVERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "${COMPANY}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\${APPEXE}"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
  RMDir  "$SMPROGRAMS\${APPNAME}"
  Delete "$DESKTOP\${APPNAME}.lnk"

  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\${APPEXE}"
  Delete "$INSTDIR\port_finder.exe"
  Delete "$INSTDIR\ch_baseline_ref.png"
  Delete "$INSTDIR\reference_9oclock.png"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\*.conf"
  Delete "$INSTDIR\*.txt"
  Delete "$INSTDIR\uninstall.exe"

  RMDir "$INSTDIR"

  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "Software\${APPNAME}"
SectionEnd
