; G41 job: dragon-run.ahk with an Alt-Tab in the middle -- the game is
; minimised 8 s in and brought back 3 s later, which loses a fullscreen
; D3D9 device and makes the backend reset it (rhi/d3d9, reset()). The log
; then says "device reset"; a frame after it that draws is the test passed.
; Same files as dragon-run.ahk: BENCH\dragon\args.txt (with --fullscreen
; and enough --frames to outlast the 11 s), env.txt, results in BENCH\results\.
#NoEnv
name = %1%
BENCH = %2%
dir = %BENCH%\dragon
stamp = %A_Now%
report = %BENCH%\results\dragon-%stamp%.txt
FileAppend, %stamp% %A_ComputerName% job %name% bench %BENCH%`n, %report%
FileReadLine, args, %dir%\args.txt, 1
FileAppend, args [%args%] (read %ErrorLevel%)`n, %report%
log = %dir%\dragon-%stamp%.log
EnvSet, DRAGON_LOG_FILE, %log%
; BENCH\dragon\env.txt, if present: NAME=VALUE lines set for this run.
Loop, Read, %dir%\env.txt
{
    StringSplit, kv, A_LoopReadLine, =
    if (kv1 != "")
    {
        EnvSet, %kv1%, %kv2%
        FileAppend, env %kv1%=%kv2%`n, %report%
    }
}
FileDelete, %dir%\shot.bmp
; Dismiss a running screensaver first: one over the launch keeps a
; fullscreen game from the foreground, and it waits, lost, until the end.
; A synthetic mouse move does not end XP's logon.scr; closing it does (the
; G41's is not password-protected, so that is the desktop back).
RegRead, saver, HKEY_CURRENT_USER, Control Panel\Desktop, SCRNSAVE.EXE
SplitPath, saver, saver_name
if (saver_name != "")
{
    Process, Close, %saver_name%
    FileAppend, screensaver %saver_name% closed %ErrorLevel%`n, %report%
}
Sleep, 1000
Run, "%dir%\dragon.exe" %args%, %dir%, UseErrorLevel, pid
FileAppend, run error %ErrorLevel% pid %pid%`n, %report%
started := A_TickCount
Sleep, 8000
WinMinimize, ahk_pid %pid%
FileAppend, minimised`n, %report%
Sleep, 3000
WinRestore, ahk_pid %pid%
WinActivate, ahk_pid %pid%
FileAppend, restored`n, %report%
Process, WaitClose, %pid%, 600
hung := ErrorLevel
seconds := (A_TickCount - started) / 1000
FileAppend, ended after %seconds% s hung %hung%`n, %report%
if (hung)
{
    WinClose, ahk_pid %pid%
    Sleep, 3000
    Process, Close, %pid%
}
FileCopy, %log%, %BENCH%\results\dragon-%stamp%.log, 1
FileAppend, log copy %ErrorLevel%`n, %report%
if FileExist(dir . "\shot.bmp")
    FileCopy, %dir%\shot.bmp, %BENCH%\results\dragon-%stamp%.bmp, 1
ExitApp, % hung ? 2 : 0
