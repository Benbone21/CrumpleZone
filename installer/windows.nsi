; Crumple Zone Windows installer (NSIS). Built by `make windows-installer` from build/win/CrumpleZone.
; makensis -DVERSION=x.y.z -DSRC=build/win/CrumpleZone -DOUT=build/win/CrumpleZone-setup.exe installer/windows.nsi
Unicode true
Target amd64-unicode
!include "MUI2.nsh"
Name "Crumple Zone ${VERSION}"
OutFile "${OUT}"
InstallDir "$LOCALAPPDATA\Programs\Crumple Zone"
InstallDirRegKey HKCU "Software\CrumpleZone" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\crumplezone.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Crumple Zone"
  SetOutPath "$INSTDIR"
  File /r "${SRC}/*"
  WriteRegStr HKCU "Software\CrumpleZone" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\Crumple Zone"
  CreateShortcut "$SMPROGRAMS\Crumple Zone\Crumple Zone.lnk" "$INSTDIR\crumplezone.exe"
  CreateShortcut "$SMPROGRAMS\Crumple Zone\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\Crumple Zone.lnk" "$INSTDIR\crumplezone.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CrumpleZone" "DisplayName" "Crumple Zone"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CrumpleZone" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CrumpleZone" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\Crumple Zone.lnk"
  RMDir /r "$SMPROGRAMS\Crumple Zone"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CrumpleZone"
  DeleteRegKey HKCU "Software\CrumpleZone"
SectionEnd
