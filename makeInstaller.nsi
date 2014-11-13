Name "M8M"
OutFile "M8M-0.0.520(Alpha).exe" /* version */
InstallDir $APPDATA\M8M
InstallDirRegKey HKLM "Software\M8M_UserInstall" "CurrentlyInstalled"
RequestExecutionLevel user
Icon installer.ico
UninstallIcon uninst.ico

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

LoadLanguageFile "${NSISDIR}\Contrib\Language files\English.nlf"
VIProductVersion "0.0.520.0" /* maj, min, commit, MMDD */
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "M8M"
VIAddVersionKey /LANG=${LANG_ENGLISH} "Comments" "ALPHA"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "Cryptocurrency miner."
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "Copyright Massimo Del Zotto"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "ALPHA"
  
  

Section "IrrelevantName"  
  # SetShellVarContext all
  CreateDirectory "$SMPROGRAMS\M8M"
  CreateShortcut "$SMPROGRAMS\M8M\Uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe"
  SetOutPath "$INSTDIR\miner" # working directory for the following link
  CreateShortcut "$SMPROGRAMS\M8M\Launch miner.lnk" "$INSTDIR\miner\M8M.exe" "" "$INSTDIR\linkIcon.ico" 0 "" "" "A minimalistic, (hopefully) educational cryptocurrency  miner."
  
  SetOutPath $INSTDIR
  File linkIcon.ico
  
  SetOutPath $INSTDIR\web
  File /a /r web\*.*
  
  SetOutPath $INSTDIR\miner
  File /a release\M8M.exe
  
  SetOutPath $INSTDIR\miner\kernels
  File /a M8M\kernels\*
  
  WriteRegStr HKLM Software\M8M_UserInstall "CurrentlyInstalled" "$INSTDIR"
  # uninstall keys for windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\M8M_UserInstall" "DisplayName" "M8M - An (hopefully) educational cryptocurrency miner."
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\M8M_UserInstall" "DisplayVersion" "0.0.520"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\M8M_UserInstall" "DisplayIcon" "$INSTDIR\linkIcon.ico"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\M8M_UserInstall" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\M8M_UserInstall" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\M8M_UserInstall" "NoRepair" 1
  WriteUninstaller "$INSTDIR\uninstall.exe"
  
  /* Add firewall exception. Plugins don't seem to do the trick for me so let's go for some admin commands. */
  # Exec 'netsh advfirewall firewall add rule name="M8M cryptocurrency miner" dir=in action=allow program="$INSTDIR\miner\M8M.exe" enable=yes'
  # Exec 'netsh advfirewall firewall add rule name="M8M web monitor" dir=out action=allow program="$INSTDIR\miner\M8M.exe" enable=yes'
  # Exec 'netsh advfirewall firewall add rule name="M8M web admin" dir=out action=allow program="$INSTDIR\miner\M8M.exe" enable=yes'
SectionEnd



Section "Uninstall"  
  SetShellVarContext all
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\M8M_UserInstall"
  DeleteRegKey HKLM "SOFTWARE\M8M_UserInstall"
  RMDir /r "$INSTDIR"
  RMDir /r "$SMPROGRAMS\M8M"
  # Exec 'netsh advfirewall firewall delete rule name="M8M cryptocurrency miner" program="$INSTDIR\miner\M8M.exe"'
  # Exec 'netsh advfirewall firewall delete rule name="M8M web monitor" program="$INSTDIR\miner\M8M.exe"'
  # Exec 'netsh advfirewall firewall delete rule name="M8M web admin" program="$INSTDIR\miner\M8M.exe"'
SectionEnd
