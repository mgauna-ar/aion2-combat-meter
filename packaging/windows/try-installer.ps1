# Installs AION 2 Combat Meter with its installer, silently, in a temporary folder and
# uninstalls it twice, checking what each leaves: the meter, its Start menu shortcut and
# its uninstall entry come and go, Update & restart's leftovers go with them, the user's
# data in %APPDATA% stays, and Start with Windows goes only when it starts this copy.
# Meant for a throwaway Windows such as a CI runner; it puts this user's Start with
# Windows entry back as it found it.
param([Parameter(Mandatory)][string]$Installer)

$ErrorActionPreference = 'Stop'
$temp = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$dir = Join-Path $temp 'AION 2 Combat Meter test'
$exeName = 'aion2-combat-meter.exe'
$exe = Join-Path $dir $exeName
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\AION2CombatMeter'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runValue = 'AION2CombatMeter'
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'AION 2 Combat Meter.lnk'
$dataDir = Join-Path $env:APPDATA 'aion2-combat-meter'
$dataMarker = Join-Path $dataDir 'try-installer.txt'
$dataDirExisted = Test-Path $dataDir
$setup = (Resolve-Path $Installer).Path

# NSIS reads the folder switch (/D= or _?=) last and unquoted, spaces and all
function Invoke-Silently([string]$Program, [string]$FolderSwitch) {
  $p = Start-Process -FilePath $Program -ArgumentList "/S $FolderSwitch$dir" -Wait -PassThru
  if ($p.ExitCode -ne 0) { throw "$Program exited with $($p.ExitCode)" }
}

function Install {
  Invoke-Silently $setup '/D='
  if (-not (Test-Path $exe)) { throw "the meter wasn't installed in $dir" }
  if (-not (Test-Path $shortcut)) { throw 'the Start menu shortcut is missing' }
  $entry = Get-ItemProperty $uninstallKey
  if ($entry.InstallLocation -ne $dir) { throw 'the uninstall entry is missing' }
  if (-not $entry.DisplayVersion) { throw 'the uninstall entry has no version' }
}

# _?= runs the uninstaller in place, so it's waited for (and can't delete itself)
function Uninstall {
  Invoke-Silently (Join-Path $dir 'uninstall.exe') '_?='
  if (Test-Path $shortcut) { throw 'the Start menu shortcut was left' }
  if (Test-Path $uninstallKey) { throw 'the uninstall entry was left' }
  $left = @(Get-ChildItem -Force $dir | Where-Object Name -ne 'uninstall.exe')
  if ($left.Count -ne 0) { throw "the uninstaller left $($left.Name -join ', ')" }
  Remove-Item -Recurse -Force $dir
  if (-not (Test-Path $dataMarker)) { throw "the user's data in $dataDir was removed" }
}

# What Update & restart leaves in the install folder (src/app/src/updater.cpp)
function Add-UpdateLeftovers {
  Set-Content (Join-Path $dir "$exeName.1.6.4.old") 'old build'
  $staging = New-Item -ItemType Directory -Force (Join-Path $dir '.aion2-combat-meter-update')
  Set-Content (Join-Path $staging $exeName) 'staged build'
}

# What the meter's Start with Windows writes for a copy in $Folder (os/auto_start.cpp)
function Set-StartWithWindows([string]$Folder) {
  Set-ItemProperty $runKey -Name $runValue -Value "`"$Folder\$exeName`""
}

function Get-StartWithWindows {
  (Get-ItemProperty $runKey -Name $runValue -ErrorAction SilentlyContinue).$runValue
}

$runBefore = Get-StartWithWindows
New-Item -ItemType Directory -Force $dataDir | Out-Null
Set-Content $dataMarker 'fights'
try {
  # A copy in another folder keeps its Start with Windows; the update leftovers go
  Install
  Add-UpdateLeftovers
  Set-StartWithWindows 'C:\Games\Aion2Meter'
  Uninstall
  if (-not (Get-StartWithWindows)) { throw "another copy's Start with Windows was removed" }

  # This copy's Start with Windows goes with it
  Install
  Set-StartWithWindows $dir
  Uninstall
  if (Get-StartWithWindows) { throw "this copy's Start with Windows was left" }
} finally {
  if ($runBefore) { Set-ItemProperty $runKey -Name $runValue -Value $runBefore }
  else { Remove-ItemProperty $runKey -Name $runValue -ErrorAction SilentlyContinue }
  Remove-Item $dataMarker -ErrorAction SilentlyContinue
  if (-not $dataDirExisted) { Remove-Item -Recurse -Force $dataDir -ErrorAction SilentlyContinue }
}

Write-Output 'The installer installs and uninstalls AION 2 Combat Meter, and keeps its data.'
