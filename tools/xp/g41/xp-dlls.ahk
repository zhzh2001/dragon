; G41 job: copy XP's own system DLLs into BENCH\results\xp-dlls\, for
;   tools/xp/check_imports.py --record ~/mnt/g41/results/xp-dlls
; (docs/PORTING.md, "R6, as built"). Submit with qemu-gpu's
; tools/g41/g41job.sh (docs/G41_TESTBENCH.md there). Only the export names
; are recorded in the repository; the files stay on the share.
#NoEnv
name = %1%
BENCH = %2%
out = %BENCH%\results\xp-dlls
FileCreateDir, %out%
missing =
for i, dll in ["kernel32.dll", "user32.dll", "gdi32.dll", "msvcrt.dll", "shell32.dll", "d3d9.dll", "advapi32.dll", "ole32.dll", "winmm.dll", "psapi.dll", "ntdll.dll"]
{
    FileCopy, %A_WinDir%\system32\%dll%, %out%\%dll%, 1
    if ErrorLevel
        missing .= " " . dll
}
FileGetVersion, kver, %A_WinDir%\system32\kernel32.dll
FileGetVersion, cver, %A_WinDir%\system32\msvcrt.dll
FileGetVersion, dver, %A_WinDir%\system32\d3d9.dll
FileAppend, %A_Now% %A_ComputerName% %A_OSVersion% kernel32 %kver% msvcrt %cver% d3d9 %dver% missing:%missing%`n, %BENCH%\results\xp-dlls-%A_Now%.txt
ExitApp, % (missing = "") ? 0 : 1
