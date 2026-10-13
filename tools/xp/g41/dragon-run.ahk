; G41 job: run the XP build once and collect what it says.
;
; The package is unzipped to BENCH\dragon\ (the share's dragon folder). The
; arguments go in BENCH\dragon\args.txt, one line, e.g.
;   --frames 600 --size 1024x768 --hide-panels --screenshot shot.bmp
; (a relative --screenshot lands in BENCH\dragon\). The log is the game's own
; (DRAGON_LOG_FILE, since the exe is GUI-subsystem). BENCH\results\
; dragon-<time>.txt records each step -- written first, so even a job that
; fails part-way says how far it got -- and the log and screenshot are
; copied beside it. A run that does not end within 10 minutes is closed and
; reported as hung.
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
; BENCH\dragon\sample.txt, if present: "DELAY SECONDS" -- after DELAY
; seconds (past the load), BENCH\swvp\sampler.exe samples the game for
; SECONDS into results\dragon-<time>-sample.txt (tools/d3d9/swvp).
FileReadLine, sample, %dir%\sample.txt, 1
if (!ErrorLevel && sample != "")
{
    StringSplit, sp, sample, %A_Space%
    Run, %ComSpec% /c ping -n %sp1% 127.0.0.1 >nul & "%BENCH%\swvp\sampler.exe" dragon.exe %sp2% "%BENCH%\results\dragon-%stamp%-sample.txt", %BENCH%\swvp, Hide
    FileAppend, sampler after %sp1% s for %sp2% s`n, %report%
}
started := A_TickCount
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
