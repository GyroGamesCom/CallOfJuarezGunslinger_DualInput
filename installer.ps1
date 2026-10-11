# Dual Input mod for Call of Juarez: Gunslinger - installer / uninstaller.
# Run through install.bat or uninstall.bat.
param(
    [switch]$Uninstall,
    [string]$GameDir
)

$ErrorActionPreference = 'Stop'
$ExeName   = 'CoJGunslinger.exe'
$ModFiles  = 'dinput8.dll', 'xinput1_3.dll'
$IniName   = 'xinput_mouse.ini'
$LogName   = 'xinput_mouse.log'
$BackupDir = 'DualInput_backup'
# SHA-256 of the Steam build the mod was made and tested on.
$KnownExe  = 'CA1C4766900FEB867372E0E2E87EB5ADB92CA26EE537598A034ED7BE1313D93C'
$ModDir    = Join-Path $PSScriptRoot 'mod'

function Find-GameDir {
    $steam = $null
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam') {
        try {
            $p = Get-ItemProperty $key -ErrorAction Stop
            $steam = if ($p.SteamPath) { $p.SteamPath } else { $p.InstallPath }
            if ($steam) { break }
        } catch {}
    }
    if (-not $steam) { return $null }

    $libs = @($steam)
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
            $libs += $m.Groups[1].Value -replace '\\\\', '\'
        }
    }
    foreach ($lib in $libs | Select-Object -Unique) {
        $dir = Join-Path $lib 'steamapps\common\CoJ Gunslinger'
        if (Test-Path (Join-Path $dir $ExeName)) { return $dir }
    }
    return $null
}

Write-Host ''
Write-Host '=== Dual Input mod for Call of Juarez: Gunslinger ===' -ForegroundColor Cyan
Write-Host ''

if (-not $GameDir) { $GameDir = Find-GameDir }
while (-not $GameDir -or -not (Test-Path (Join-Path $GameDir $ExeName))) {
    if ($GameDir) { Write-Host "$ExeName not found in: $GameDir" -ForegroundColor Yellow }
    else { Write-Host 'The game was not found automatically.' -ForegroundColor Yellow }
    $GameDir = (Read-Host 'Paste the game folder (the one with CoJGunslinger.exe), or leave empty to cancel').Trim('" ')
    if (-not $GameDir) { Write-Host 'Cancelled.'; exit 1 }
}
Write-Host "Game folder: $GameDir"

$backup = Join-Path $GameDir $BackupDir

if ($Uninstall) {
    foreach ($f in $ModFiles + $LogName) {
        $path = Join-Path $GameDir $f
        if (Test-Path $path) { Remove-Item $path -Force; Write-Host "  removed  $f" }
    }
    if (Test-Path $backup) {
        Get-ChildItem $backup | ForEach-Object {
            Move-Item $_.FullName (Join-Path $GameDir $_.Name) -Force
            Write-Host "  restored $($_.Name) (from before the mod)"
        }
        Remove-Item $backup -Force -Recurse
    }
    $ini = Join-Path $GameDir $IniName
    if (Test-Path $ini) {
        $a = Read-Host "Delete your settings file $IniName too? (y/N)"
        if ($a -match '^[yY]') { Remove-Item $ini -Force; Write-Host "  removed  $IniName" }
        else { Write-Host "  kept     $IniName" }
    }
    Write-Host ''
    Write-Host 'Mod uninstalled.' -ForegroundColor Green
    exit 0
}

$hash = (Get-FileHash (Join-Path $GameDir $ExeName) -Algorithm SHA256).Hash
if ($hash -ne $KnownExe) {
    Write-Host ''
    Write-Host 'WARNING: your CoJGunslinger.exe is a different version than the one the mod' -ForegroundColor Yellow
    Write-Host 'was tested on (Steam). Gameplay mixing should still work, but the automatic' -ForegroundColor Yellow
    Write-Host 'menu-cursor switch will turn itself off if the game layout differs.' -ForegroundColor Yellow
    $a = Read-Host 'Install anyway? (y/N)'
    if ($a -notmatch '^[yY]') { Write-Host 'Cancelled.'; exit 1 }
}

foreach ($f in $ModFiles) {
    $src = Join-Path $ModDir $f
    $dst = Join-Path $GameDir $f
    if ((Test-Path $dst) -and (Get-FileHash $dst).Hash -ne (Get-FileHash $src).Hash) {
        # Another mod (or an older copy) uses the same file name: keep it, unless it is
        # an earlier version of this mod (it then reads xinput_mouse.ini).
        $isOurs = Select-String -Path $dst -Pattern $IniName -SimpleMatch -Quiet
        if (-not $isOurs -and -not (Test-Path (Join-Path $backup $f))) {
            New-Item -ItemType Directory -Path $backup -Force | Out-Null
            Copy-Item $dst (Join-Path $backup $f)
            Write-Host "  backed up existing $f to $BackupDir\" -ForegroundColor Yellow
        }
    }
    Copy-Item $src $dst -Force
    Write-Host "  installed $f"
}

$ini = Join-Path $GameDir $IniName
if (Test-Path $ini) {
    Write-Host "  kept your existing $IniName (settings unchanged)"
} else {
    Copy-Item (Join-Path $ModDir $IniName) $ini
    Write-Host "  installed $IniName"
}

Write-Host ''
Write-Host 'Done. Next steps:' -ForegroundColor Green
Write-Host '  1. Steam Input: gyro -> "As Mouse", right trackpad -> "Joystick" (see README).'
Write-Host "  2. Start the game. Settings: $ini"
Write-Host '  3. To remove the mod, run uninstall.bat.'
