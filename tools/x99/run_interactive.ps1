# Run a command in x99-windows' interactive desktop session, from SSH.
#
#   pwsh -File run_interactive.ps1 -Dir D:\scratch\r0 -Command 'r0.exe --frames 60' [-Timeout 120]
#
# An SSH session is session 0, which has no display: D3D12 renders offscreen
# there, but Direct3D 9 finds no adapter (CreateDevice fails with
# D3DERR_INVALIDCALL) and a window cannot be shown. So a D3D9 or windowed run
# goes through a one-off scheduled task with an Interactive logon for the
# signed-in user, which runs on the console desktop (docs/PORTING.md, "Using
# x99 for this"). The command's stdout and stderr land in run_interactive.log
# in -Dir, which this prints when the task ends.
param(
    [Parameter(Mandatory = $true)][string]$Dir,
    [Parameter(Mandatory = $true)][string]$Command,
    [int]$Timeout = 120,
    [string]$User = $env:USERNAME
)
$log = Join-Path $Dir "run_interactive.log"
Remove-Item $log -ErrorAction SilentlyContinue
$name = "DragonInteractive"
$action = New-ScheduledTaskAction -Execute "cmd.exe" -Argument "/c $Command > `"$log`" 2>&1" -WorkingDirectory $Dir
$principal = New-ScheduledTaskPrincipal -UserId $User -LogonType Interactive
Register-ScheduledTask -TaskName $name -Action $action -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName $name
$deadline = (Get-Date).AddSeconds($Timeout)
do {
    Start-Sleep -Milliseconds 500
    $state = (Get-ScheduledTask -TaskName $name).State
} while ($state -eq "Running" -and (Get-Date) -lt $deadline)
if ($state -eq "Running") {
    Stop-ScheduledTask -TaskName $name
    "run_interactive: timed out after $Timeout s"
}
$result = (Get-ScheduledTaskInfo -TaskName $name).LastTaskResult
Unregister-ScheduledTask -TaskName $name -Confirm:$false
if (Test-Path $log) { Get-Content $log }
"run_interactive: exit $result"
