; BENCmouth for Windows - installer and upgrader.
;
; Machine-wide, into Program Files, which means it asks for administrator
; rights once and installs for everyone on the box. That is the right shape
; here: BENCmouth is one GUI and one console program with no per-user state to
; speak of, and nothing it writes goes near its own directory.
;
; Upgrading is the point. An installer that leaves the previous version beside
; the new one, or merges into it and leaves whatever the new build stopped
; shipping, is worse than no installer. This runs the old uninstaller first -
; after the person has clicked Install, not in .onInit, so cancelling on the
; directory page cannot leave them with nothing.
;
;   makensis -DSRCDIR=stage -DVERSION=v0.2.2 tools/windows-installer.nsi
;
; Graphics are the disk image's, in the sizes MUI fixes: assets/brand made by
; tools/make-installer-art.sh, and the same .ico the GUI's window already uses.

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!ifndef VERSION
  !define VERSION "dev"
!endif
!ifndef SRCDIR
  !define SRCDIR "stage"
!endif
!ifndef ICONFILE
  ; Relative to wherever makensis was invoked, which is the repository root.
  ; Forward slashes: the compiler reads this on the build machine, which is
  ; Linux, where a backslash is not a path separator and the error you get is
  ; "can't open file" three macros deep.
  !define ICONFILE "assets/icon/bencmouth.ico"
!endif
!ifndef ARTDIR
  !define ARTDIR "assets/brand"
!endif

Name "BENCmouth ${VERSION}"
!ifndef OUTFILE
  !define OUTFILE "bencmouth-${VERSION}-windows-setup.exe"
!endif
; Absolute when the caller says so - makensis writes a relative OutFile beside
; the script, not into the working directory.
OutFile "${OUTFILE}"
Unicode true
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES64\BENCmouth"
ShowInstDetails show
ShowUninstDetails show

; Solid, because most of what goes in here is the 124,910-word CMU dictionary
; and it goes in twice - once inside bencmouth-gui.exe and once inside bm.exe.
; Compressed as one stream the second copy costs almost nothing; compressed
; per file, which is what the zlib default does, it is paid for twice.
;
; Measured on a 6.3 MB payload with only one of the two carrying the
; dictionary: 2.08 MB solid LZMA against 3.24 MB zlib. The shipped pair both
; carry it, so the real gap is wider than that.
SetCompressor /SOLID lzma

; The strip along the bottom of every page. It says Nullsoft otherwise, which
; is true and is not the name anyone is looking for there.
BrandingText "BENCO Holdings - MIT licensed"

!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\BENCmouth"

; Four numbers or the compiler refuses it, so the workflow passes this in
; alongside the display version. Without it the setup .exe has no version at
; all in its properties, which is the one place a person checks when they have
; two of them in Downloads and no idea which is newer.
!ifndef VIVERSION
  !define VIVERSION "0.0.0.0"
!endif
VIProductVersion "${VIVERSION}"
VIAddVersionKey "ProductName"     "BENCmouth"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "FileVersion"     "${VIVERSION}"
VIAddVersionKey "FileDescription" "BENCmouth formant speech synthesizer - setup"
VIAddVersionKey "CompanyName"     "BENCO Holdings"
VIAddVersionKey "LegalCopyright"  "MIT licensed"

; ---------------------------------------------------------------- interface

!define MUI_ABORTWARNING
!define MUI_ICON   "${ICONFILE}"
!define MUI_UNICON "${ICONFILE}"

!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "${ARTDIR}/nsis-header.bmp"
!define MUI_HEADERIMAGE_RIGHT
!define MUI_WELCOMEFINISHPAGE_BITMAP "${ARTDIR}/nsis-welcome.bmp"

; MUI_BGCOLOR is deliberately left alone. It would paint the header strip and
; the welcome page in the phosphor near-black, which is the look - and leave
; MUI's own header text black on top of it, which is not readable. The art is
; dark tiles on MUI's white; that part is by design.

!define MUI_WELCOMEPAGE_TITLE "BENCmouth ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "A formant speech synthesizer. Text in, speech out.$\r$\n$\r$\nThis installs the BENCmouth window and the bm command-line program, with the voices and the songs, into Program Files for everyone who uses this computer.$\r$\n$\r$\nAn existing BENCmouth is replaced, not installed beside."

!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Run BENCmouth"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchUnelevated

; A link rather than MUI_FINISHPAGE_SHOWREADME, which would hand README.md to
; the shell - and a stock Windows has nothing registered for .md, so the last
; thing a successful install would do is ask which program to open it with.
!define MUI_FINISHPAGE_LINK "github.com/bropple/BENCmouth"
!define MUI_FINISHPAGE_LINK_LOCATION "https://github.com/bropple/BENCmouth"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}/LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var PrevUninst      ; the old version's uninstaller, "" if this is a fresh box
Var PrevDir
Var PrevVersion
Var WorkDir         ; what the shortcuts start in - see the comment in .onInit
Var WantPath        ; /PATH was on the command line

; The installer runs elevated, so anything it starts is elevated too, and a
; program launched that way saves files nobody can then edit. Handing the path
; to Explorer - which is running as the person at the keyboard - starts it as
; them. It is a trick, but it is the one that works without a plugin.
Function LaunchUnelevated
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\bencmouth-gui.exe"'
FunctionEnd

Function .onInit
  ; A 32-bit installer sees a redirected registry and a redirected Program
  ; Files unless it says otherwise. Everything below - including the entry
  ; Apps & Features reads - belongs in the 64-bit view, because the program
  ; being installed is 64-bit.
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP \
      "BENCmouth is built for 64-bit Windows and this is a 32-bit system."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all      ; Start Menu and Desktop for every user

  ; The shortcuts start in the person's Documents rather than in Program
  ; Files. Loading looks beside the executable no matter what the working
  ; directory is, so this costs nothing there - but saving a voice or exporting
  ; a WAV opens wherever the program started, and that folder has to be one
  ; they can write to. Program Files is not.
  ;
  ; "current" while reading it, because with the context still set to all this
  ; is Public\Documents. Under an over-the-shoulder UAC prompt this resolves to
  ; the administrator whose password was typed rather than to the person using
  ; the machine; there is no way to ask NSIS for the latter, and the dialog
  ; simply opens somewhere else if that folder is not theirs.
  SetShellVarContext current
  StrCpy $WorkDir "$DOCUMENTS"
  SetShellVarContext all
  ${If} $WorkDir == ""
    StrCpy $WorkDir "$INSTDIR"
  ${EndIf}

  ; Find whatever is already installed, and install over the top of it rather
  ; than into the default directory - someone who moved it to D: meant it.
  ReadRegStr $PrevUninst   HKLM "${REGKEY}" "UninstallString"
  ReadRegStr $PrevDir      HKLM "${REGKEY}" "InstallLocation"
  ReadRegStr $PrevVersion  HKLM "${REGKEY}" "DisplayVersion"
  ${If} $PrevUninst != ""
    ; Written quoted, read back quoted. ExecWait wants it that way and
    ; ${GetParent} does not.
    StrCpy $0 $PrevUninst 1
    ${If} $0 == '"'
      StrLen $1 $PrevUninst
      IntOp $1 $1 - 2
      StrCpy $PrevUninst $PrevUninst $1 1
    ${EndIf}
    ${If} $PrevDir == ""
      ${GetParent} "$PrevUninst" $PrevDir
    ${EndIf}
    ${If} $PrevDir != ""
      StrCpy $INSTDIR "$PrevDir"
    ${EndIf}
  ${EndIf}

  ; /PATH asks for the optional PATH entry without anyone clicking anything.
  ; A silent install takes the default selection and that section is off by
  ; default, so without a switch there is no way to get it unattended - and no
  ; way for CI to reach the code at all. That second reason is the load-bearing
  ; one: makensis cannot check a System::Call, which is parsed by the plugin at
  ; run time, so an installer that compiles is not an installer that works.
  ; Registers rather than $0/$1, which the block above is using.
  ClearErrors
  ${GetParameters} $R8
  ${GetOptions} $R8 "/PATH" $R9
  ${IfNot} ${Errors}
    StrCpy $WantPath 1
  ${EndIf}

  ; An upgrade keeps whatever the previous install was asked for. The tickbox
  ; cannot express this - a silent upgrade never sees it, and ${SEC_PATH} does
  ; not exist yet at this point in the script - so it goes through the same
  ; variable the switch uses.
  ReadRegDWORD $R7 HKLM "${REGKEY}" "OnPath"
  ${If} $R7 == 1
    StrCpy $WantPath 1
  ${EndIf}
FunctionEnd

; ---------------------------------------------------------------- upgrade
;
; Runs after the components and directory pages, so a person who gets this far
; and then cancels still has the version they started with. The old uninstaller
; takes its own files away, including any the new build no longer ships, which
; is the whole reason not to just overwrite: voices/ and songs/ are directories
; whose contents change between releases.

Section -Upgrade
  ; $PrevDir has to be non-empty as well: it is the value of _?=, and an empty
  ; one turns a silent uninstall into a syntax error the person never sees.
  ${If} $PrevUninst != ""
  ${AndIf} $PrevDir != ""
  ${AndIf} ${FileExists} "$PrevUninst"
    DetailPrint "Removing BENCmouth $PrevVersion"
    ; _?= keeps the uninstaller where it is instead of copying itself to a
    ; temporary directory and returning immediately - without it ExecWait does
    ; not wait, and the removal races the installation that follows.
    ExecWait '"$PrevUninst" /S _?=$PrevDir' $0
    DetailPrint "  uninstaller returned $0"
    Delete "$PrevUninst"    ; _?= means it cannot delete itself
  ${EndIf}
SectionEnd

; ---------------------------------------------------------------- program

Section "BENCmouth (the program)" SEC_APP
  SectionIn RO
  SetOutPath "$INSTDIR"
  SetOverwrite on

  ; The window and the console program. The GUI carries its font, its icon,
  ; the wordmark and every licence text inside the executable, so there is no
  ; assets folder to keep beside it and no way for it to end up looking wrong
  ; because a file went missing.
  File "${SRCDIR}/bencmouth-gui.exe"
  File "${SRCDIR}/bm.exe"
  File "${SRCDIR}/README.md"
  File "${SRCDIR}/LICENSE"
  File "${SRCDIR}/NOTICE"

  ; Cleared before they are written, because a voice that was withdrawn should
  ; not survive an upgrade just because nothing happened to overwrite it. (The
  ; upgrade section has usually done this already; this also covers installing
  ; over a directory somebody unpacked the portable .zip into.)
  ;
  ; By extension rather than RMDir /r, which is the same rule the uninstaller
  ; follows: take away what was shipped and leave anything else alone.
  Delete "$INSTDIR\voices\*.bmvoice"
  Delete "$INSTDIR\songs\*.bmsong"
  SetOutPath "$INSTDIR\voices"
  File "${SRCDIR}/voices/*.bmvoice"
  SetOutPath "$INSTDIR\songs"
  File "${SRCDIR}/songs/*.bmsong"

  ; The Start Menu entry starts in Documents; the uninstaller starts where it
  ; lives. NSIS takes a shortcut's working directory from the last SetOutPath.
  CreateDirectory "$SMPROGRAMS\BENCmouth"
  SetOutPath "$WorkDir"
  CreateShortCut "$SMPROGRAMS\BENCmouth\BENCmouth.lnk" \
                 "$INSTDIR\bencmouth-gui.exe" "" "$INSTDIR\bencmouth-gui.exe" 0
  SetOutPath "$INSTDIR"
  CreateShortCut "$SMPROGRAMS\BENCmouth\Uninstall BENCmouth.lnk" \
                 "$INSTDIR\uninstall.exe"
SectionEnd

Section "Desktop shortcut" SEC_DESKTOP
  SetOutPath "$WorkDir"
  CreateShortCut "$DESKTOP\BENCmouth.lnk" \
                 "$INSTDIR\bencmouth-gui.exe" "" "$INSTDIR\bencmouth-gui.exe" 0
  SetOutPath "$INSTDIR"
SectionEnd

; Unticked by default - the /o. Everything else this installer does lives in
; its own directory, its own Start Menu folder and its own uninstall key; this
; is the one thing that reaches into a setting somebody else owns, so it is
; asked for rather than assumed. See the PATH block below for how it is done
; and what it cannot promise.
Section /o "Add bm to PATH" SEC_PATH
  Call bm_PathAdd
  ; Remembered so that an upgrade keeps it. The upgrade runs the old
  ; uninstaller first, which takes the entry back out, and this box is off by
  ; default - so without this a person who asked for PATH once would lose it
  ; silently at the next release, which is a worse surprise than not having had
  ; it. Read back in .onInit.
  WriteRegDWORD HKLM "${REGKEY}" "OnPath" 1
SectionEnd

; The same thing for /PATH on the command line. Separate from the section
; because a section's tick state is what a silent install cannot set, and
; declared after it so that the switch and the checkbox cannot get out of order
; on the components page.
;
; Calling bm_PathAdd twice is harmless - it declines when the directory is
; already there - so this does not need to know whether the box was ticked.
Section -PathFromSwitch
  ${If} $WantPath == 1
    Call bm_PathAdd
    WriteRegDWORD HKLM "${REGKEY}" "OnPath" 1
  ${EndIf}
SectionEnd

; ---------------------------------------------------------------- finish

Section -Post
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr   HKLM "${REGKEY}" "DisplayName"     "BENCmouth"
  WriteRegStr   HKLM "${REGKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr   HKLM "${REGKEY}" "Publisher"       "BENCO Holdings"
  WriteRegStr   HKLM "${REGKEY}" "URLInfoAbout"    "https://github.com/bropple/BENCmouth"
  WriteRegStr   HKLM "${REGKEY}" "DisplayIcon"     "$INSTDIR\bencmouth-gui.exe"
  WriteRegStr   HKLM "${REGKEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegStr   HKLM "${REGKEY}" "QuietUninstallString" \
                                                   "$\"$INSTDIR\uninstall.exe$\" /S"
  WriteRegStr   HKLM "${REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${REGKEY}" "EstimatedSize" "$0"
SectionEnd

; ---------------------------------------------------------------- PATH
;
; bm.exe is a console program, and a console program belongs on PATH.
;
; This edits the *account's* PATH - HKCU\Environment - and not the machine's,
; and that choice is most of what makes it safe. The machine PATH on a
; developer's box is routinely thousands of characters; the account's is
; usually a handful of entries, and getting it wrong costs one login rather
; than one computer.
;
; The value never enters an NSIS variable. A stock NSIS holds a string in 1024
; characters, so the obvious implementation - ReadRegStr, append, WriteRegStr -
; silently truncates any PATH longer than that, which is why this installer
; shipped without the feature rather than with that. Here System.dll reads the
; value into memory it allocated, edits it in place, and writes back exactly
; what it holds, so the length of what is already there never matters.
;
; Nothing is written unless every step before it succeeded. The failure mode of
; a PATH editor has to be "did nothing", never "wrote half".
;
; One limitation, stated because it cannot be fixed from here: an elevated
; installer sees the *elevating* account's HKCU. When Windows shows a consent
; prompt to somebody who is already an administrator - the ordinary case - that
; is the same account and this does what it says. When somebody types a
; different administrator's credentials into the prompt, the entry lands on
; that administrator's PATH. The alternative is editing the machine PATH, which
; is the one with the bad failure mode, so this is the trade taken.

!define ENV_HKCU      0x80000001
!define ENV_KEY       "Environment"
!define ENV_ACCESS    0x2001F            ; KEY_READ | KEY_WRITE
!define ENV_REG_SZ    1
!define ENV_REG_EXP   2

; Tells every running program that the environment moved. Without it the entry
; is real but nothing that is already open can see it, and the first thing
; anyone does is open a terminal and report that it did not work.
!macro BM_ENV_BROADCAST
  System::Call 'user32::SendMessageTimeoutW(p 0xFFFF, i 0x1A, p 0, \
                                            t "Environment", i 2, i 5000, *p .r0)'
!macroend

; Opens HKCU\Environment and reads Path into a fresh buffer.
;   out  $1 = key handle, 0 if this failed
;        $2 = the value's type, to be written back unchanged
;        $3 = bytes read, 0 when there is no Path at all
;        $4 = buffer, 0 if this failed; `extra` bytes larger than needed
!macro BM_PATH_READ EXTRA
  StrCpy $1 0
  StrCpy $2 ${ENV_REG_EXP}
  StrCpy $3 0
  StrCpy $4 0

  System::Call 'advapi32::RegCreateKeyExW(p ${ENV_HKCU}, w "${ENV_KEY}", i 0, \
                p 0, i 0, i ${ENV_ACCESS}, p 0, *p .r1, *i) i .r0'
  ${If} $0 <> 0
    DetailPrint "  cannot open HKCU\${ENV_KEY} (error $0) - PATH left alone"
    StrCpy $1 0
    Goto bm_read_done
  ${EndIf}

  ; Ask for the size first. A missing Path is not an error - a fresh profile
  ; genuinely has none - and leaves $3 at zero.
  System::Call 'advapi32::RegQueryValueExW(p r1, w "Path", p 0, *i .r2, p 0, \
                *i .r3) i .r0'
  ${If} $0 <> 0
    StrCpy $2 ${ENV_REG_EXP}
    StrCpy $3 0
  ${ElseIf} $2 <> ${ENV_REG_SZ}
  ${AndIf}  $2 <> ${ENV_REG_EXP}
    ; REG_MULTI_SZ or something stranger. Not ours to reinterpret.
    DetailPrint "  PATH is registry type $2, not a string - left alone"
    System::Call 'advapi32::RegCloseKey(p r1)'
    StrCpy $1 0
    Goto bm_read_done
  ${EndIf}

  IntOp $5 $3 + ${EXTRA}
  System::Alloc $5
  Pop $4
  ${If} $4 = 0
    DetailPrint "  out of memory - PATH left alone"
    System::Call 'advapi32::RegCloseKey(p r1)'
    StrCpy $1 0
    Goto bm_read_done
  ${EndIf}

  ${If} $3 > 0
    System::Call 'advapi32::RegQueryValueExW(p r1, w "Path", p 0, p 0, p r4, \
                  *i r3) i .r0'
    ${If} $0 <> 0
      DetailPrint "  cannot read PATH (error $0) - left alone"
      System::Free $4
      StrCpy $4 0
      System::Call 'advapi32::RegCloseKey(p r1)'
      StrCpy $1 0
    ${EndIf}
  ${EndIf}
  bm_read_done:
!macroend

; Writes $4 back to Path as type $2, closes $1, and tells the world.
!macro BM_PATH_WRITE
  System::Call 'kernel32::lstrlenW(p r4) i .r5'
  IntOp $5 $5 + 1
  IntOp $5 $5 * 2
  System::Call 'advapi32::RegSetValueExW(p r1, w "Path", i 0, i r2, p r4, \
                i r5) i .r0'
  ${If} $0 <> 0
    DetailPrint "  cannot write PATH (error $0) - unchanged"
  ${Else}
    !insertmacro BM_ENV_BROADCAST
  ${EndIf}
!macroend

; Two macros rather than one, because the installer needs only the add and
; the uninstaller only the remove - and makensis -WX rejects a generated
; un.bm_PathAdd that nothing calls, which is the warning doing its job.
!macro BM_PATH_ADD_FUNC UN
Function ${UN}bm_PathAdd
  Push $0
  Push $1
  Push $2
  Push $3
  Push $4
  Push $5

  ; Room for a separator, the directory, and the terminator.
  StrLen $0 "$INSTDIR"
  IntOp $0 $0 + 4
  IntOp $0 $0 * 2
  !insertmacro BM_PATH_READ $0
  ${If} $1 = 0
    Goto add_done
  ${EndIf}

  ${If} $3 > 2
    ; Substring, so "C:\...\BENCmouth" also matches "C:\...\BENCmouth2" and we
    ; decline to add. Declining when we should have added is a directory
    ; somebody types by hand; adding twice is a PATH that grows on every
    ; reinstall, so the conservative direction is the right one.
    System::Call 'shlwapi::StrStrIW(p r4, w "$INSTDIR") p .r0'
    ${If} $0 <> 0
      DetailPrint "  $INSTDIR is already on PATH"
      Goto add_free
    ${EndIf}
    System::Call 'kernel32::lstrcatW(p r4, w ";$INSTDIR")'
  ${Else}
    ; No Path, or an empty one. No leading separator: a PATH that starts with
    ; a semicolon has an empty first entry, which is the current directory.
    System::Call 'kernel32::lstrcpyW(p r4, w "$INSTDIR")'
  ${EndIf}

  DetailPrint "  adding $INSTDIR to your PATH"
  !insertmacro BM_PATH_WRITE

  add_free:
  System::Free $4
  System::Call 'advapi32::RegCloseKey(p r1)'
  add_done:
  Pop $5
  Pop $4
  Pop $3
  Pop $2
  Pop $1
  Pop $0
FunctionEnd
!macroend

!macro BM_PATH_REMOVE_FUNC UN
Function ${UN}bm_PathRemove
  Push $0
  Push $1
  Push $2
  Push $3
  Push $4
  Push $5
  Push $R0
  Push $R1
  Push $R2

  !insertmacro BM_PATH_READ 2
  ${If} $1 = 0
    Goto rm_done
  ${EndIf}
  ${If} $3 <= 2
    Goto rm_free                      ; nothing there to remove
  ${EndIf}

  ; The whole of PATH is us: it becomes empty rather than a stray semicolon.
  System::Call 'kernel32::lstrcmpiW(p r4, w "$INSTDIR") i .r0'
  ${If} $0 = 0
    System::Call 'kernel32::lstrcpyW(p r4, w "")'
    DetailPrint "  removing $INSTDIR from your PATH"
    !insertmacro BM_PATH_WRITE
    Goto rm_free
  ${EndIf}

  ; Otherwise take out exactly the ";$INSTDIR" this installer put in, and shift
  ; the tail down over it. Anything else in the string is somebody else's and
  ; is not touched.
  System::Call 'shlwapi::StrStrIW(p r4, w ";$INSTDIR") p .r0'
  ${If} $0 = 0
    DetailPrint "  $INSTDIR was not on your PATH"
    Goto rm_free
  ${EndIf}

  System::Call 'kernel32::lstrlenW(p r4) i .R0'   ; characters in the whole value
  IntOp $R1 $0 - $4                               ; bytes from the start to the match
  IntOp $R1 $R1 / 2                               ; ... as characters
  StrLen $R2 ";$INSTDIR"                          ; characters we are cutting out

  IntOp $5 $R0 - $R1
  IntOp $5 $5 - $R2
  IntOp $5 $5 + 1                                 ; keep the terminator
  IntOp $5 $5 * 2                                 ; bytes to move
  IntOp $R2 $R2 * 2
  IntOp $R2 $0 + $R2                              ; source: just past the match

  System::Call 'kernel32::RtlMoveMemory(p r0, p $R2, i $5)'
  DetailPrint "  removing $INSTDIR from your PATH"
  !insertmacro BM_PATH_WRITE

  rm_free:
  System::Free $4
  System::Call 'advapi32::RegCloseKey(p r1)'
  rm_done:
  Pop $R2
  Pop $R1
  Pop $R0
  Pop $5
  Pop $4
  Pop $3
  Pop $2
  Pop $1
  Pop $0
FunctionEnd
!macroend

!insertmacro BM_PATH_ADD_FUNC ""
!insertmacro BM_PATH_REMOVE_FUNC "un."

LangString DESC_APP ${LANG_ENGLISH} \
  "The BENCmouth window, the bm command-line program, the twenty-five voices \
and the songs. Adds a Start Menu entry."
LangString DESC_DESKTOP ${LANG_ENGLISH} \
  "A BENCmouth shortcut on the desktop."
LangString DESC_PATH ${LANG_ENGLISH} \
  "Add BENCmouth to your account's PATH, so that typing bm in a terminal \
finds it. Off by default: it is the only thing here that changes a setting \
outside BENCmouth's own folder. Removed again when you uninstall."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_APP}     $(DESC_APP)
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} $(DESC_DESKTOP)
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_PATH}    $(DESC_PATH)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------- uninstall

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Section "Uninstall"
  Delete "$INSTDIR\bencmouth-gui.exe"
  Delete "$INSTDIR\bm.exe"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\NOTICE"
  Delete "$INSTDIR\uninstall.exe"

  ; What was installed, by name. A voice somebody wrote themselves and put here
  ; is theirs; RMDir /r would take it with the rest, and RMDir without /r
  ; leaves the directory exactly when there is something in it worth leaving.
  Delete "$INSTDIR\voices\*.bmvoice"
  Delete "$INSTDIR\songs\*.bmsong"
  RMDir  "$INSTDIR\voices"
  RMDir  "$INSTDIR\songs"
  RMDir  "$INSTDIR"

  Delete "$SMPROGRAMS\BENCmouth\BENCmouth.lnk"
  Delete "$SMPROGRAMS\BENCmouth\Uninstall BENCmouth.lnk"
  RMDir  "$SMPROGRAMS\BENCmouth"
  Delete "$DESKTOP\BENCmouth.lnk"

  ; Unconditional, and it has to be: whether the box was ticked is not recorded
  ; anywhere, and the remove is a no-op when the entry is not there. Taking out
  ; exactly the one entry it put in is what makes that safe to run blind.
  Call un.bm_PathRemove

  ; Voices and songs saved anywhere else are untouched, and so is anything
  ; under Documents.
  DeleteRegKey HKLM "${REGKEY}"
SectionEnd
