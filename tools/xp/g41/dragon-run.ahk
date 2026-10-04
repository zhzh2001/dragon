; G41 job: run the XP build once and collect what it says.
;
; The package is unzipped to BENCH\dragon\ (the share's dragon folder). The
; arguments go in BENCH\dragon\args.txt, one line, e.g.
;   --frames 600 --size 1024x768 --hide-panels --screenshot shot.bmp
; (a relative --screenshot lands in BENCH\dragon\). The log is the game's own
; (DRAGON_LOG_FILE, since the exe is GUI-subsystem); it and the screenshot
; are copied to BENCH\results\ with this run's time in their names. A run
; that does not end within 10 minutes is closed and reported as hung.
#NoEnv
name = %1%
BENCH = %2%
dir = %BENCH%\dragon
FileReadLine, args, %dir%\args.txt, 1
stamp = %A_Now%
log = %dir%\dragon-%stamp%.log
EnvSet, DRAGON_LOG_FILE, %log%
FileDelete, %dir%\shot.bmp
Run, "%dir%\dragon.exe" %args%, %dir%, , pid
Process, WaitClose, %pid%, 600
hung := ErrorLevel
if (hung)
{
    WinClose, ahk_pid %pid%
    Sleep, 3000
    Process, Close, %pid%
}
FileCopy, %log%, %BENCH%\results\dragon-%stamp%.log, 1
if FileExist(dir . "\shot.bmp")
    FileCopy, %dir%\shot.bmp, %BENCH%\results\dragon-%stamp%.bmp, 1
FileAppend, %stamp% %A_ComputerName% args: %args% hung: %hung%`n, %BENCH%\results\dragon-%stamp%.txt
ExitApp, % hung ? 2 : 0
