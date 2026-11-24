# The Windows half of tools/release/check_package.sh, run on the Windows host
# under PowerShell 7.
#
#   pwsh -NoProfile -File check_package.ps1 -Base D:\scratch\dragon-check-V -Version V [-Pairs 6]
#
# Base holds Dragon-V-windows.zip and root\ (tools\golden, tests\golden\metal,
# assets\embercrest.glb). It needs Python 3 with Pillow on the PATH. Both
# backends are checked: Direct3D 12 and Vulkan.
param([string]$Base, [string]$Version, [int]$Pairs = 6)

Set-Location $Base
if (Test-Path Dragon) { Remove-Item -Recurse -Force Dragon }
Expand-Archive "Dragon-$Version-windows.zip" -DestinationPath .
New-Item -ItemType Directory -Force "$Base\out" | Out-Null
$exe = "$Base\Dragon\dragon.exe"

foreach ($backend in @(@('Direct3D 12', ''), @('Vulkan', '--gpu-driver vulkan'))) {
  Set-Location "$Base\root"
  $log = python tools\golden\golden.py compare --set metal --loose --extra $backend[1] --binary $exe --out "$Base\golden-$($backend[0] -replace ' ', '')" 2>&1 | Out-String
  if ($LASTEXITCODE -eq 0) { "PASS the eight reference frames on $($backend[0]), loose against the metal set" }
  else { "FAIL reference frames on $($backend[0]): " + (($log -split "`n" | Where-Object { $_ -and $_ -notmatch '^ok' } | Select-Object -First 3) -join ' ') }
}

Set-Location "$Base\Dragon"
$flap = & $exe --headless --hide-ui --frames 215 --studio 1 --inspect 300 14 8 --screenshot "$Base\out\flap.bmp" 2>&1 | Out-String
if ($flap -match 'flight clips: 2' -and $flap -match 'model roster: 13' -and (Test-Path "$Base\out\flap.bmp")) {
  "PASS a flap: 13 dragons in the roster, the keyframed wingbeat loaded"
} else {
  "FAIL a flap: " + (($flap -split "`n" | Select-String -Pattern 'model roster|flight clips|ERROR' | Select-Object -First 3 | ForEach-Object { $_.Line }) -join ' ')
}

$run = & $exe --headless --menu-start 2 --frames 240 --hide-panels --screenshot "$Base\out\run.bmp" 2>&1 | Out-String
if ($run -match 'run: seed' -and (Test-Path "$Base\out\run.bmp")) { "PASS a hoard run from the start screen" }
else { "FAIL no run started from the start screen" }

foreach ($driver in @('direct3d12', 'vulkan')) {
  $hung = 0
  foreach ($n in 1..$Pairs) {
    $other = Start-Process -FilePath $exe -ArgumentList "--headless --gpu-driver $driver --frames 300" -PassThru -WindowStyle Hidden -RedirectStandardError "$Base\other.log"
    $p = Start-Process -FilePath $exe -ArgumentList "--headless --gpu-driver $driver --frames 120 --screenshot $Base\pair.bmp" -PassThru -WindowStyle Hidden -RedirectStandardError "$Base\pair.log"
    if (-not $p.WaitForExit(60000)) { $hung++; $p.Kill() }
    $other.WaitForExit(60000) | Out-Null
    if (-not $other.HasExited) { $other.Kill() }
  }
  if ($hung -eq 0) { "PASS $Pairs of $Pairs runs beside a second render on $driver" }
  else { "FAIL $hung of $Pairs runs beside a second render on $driver hung" }
}
