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

; PATH is deliberately not touched. bm.exe is a console program and belonging
; on PATH is exactly what it wants, but a stock NSIS build holds a string in
; 1024 characters: read a longer PATH, and what gets written back is a
; truncated one. Breaking the system PATH of a machine that has a lot of
; software on it, to save typing a directory name, is not a trade worth making.

LangString DESC_APP ${LANG_ENGLISH} \
  "The BENCmouth window, the bm command-line program, the twenty-five voices \
and the songs. Adds a Start Menu entry."
LangString DESC_DESKTOP ${LANG_ENGLISH} \
  "A BENCmouth shortcut on the desktop."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_APP}     $(DESC_APP)
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} $(DESC_DESKTOP)
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

  ; Voices and songs saved anywhere else are untouched, and so is anything
  ; under Documents.
  DeleteRegKey HKLM "${REGKEY}"
SectionEnd
