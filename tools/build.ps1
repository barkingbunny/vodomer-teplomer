# Build / nahrani / monitor FW (ESP-IDF 6.1), nahrada drivejsiho `pio run`.
#
#   tools\build.ps1                    # debug build
#   tools\build.ps1 -Flash             # debug build + nahrani na COM10
#   tools\build.ps1 -Release -Flash    # release (bez logu)
#   tools\build.ps1 -Monitor           # seriovy monitor BEZ resetu desky
#   tools\build.ps1 -Menuconfig        # nastaveni sdkconfig
#
# Build adresar a sdkconfig jsou mimo Google Drive:
#   %LOCALAPPDATA%\idf-build\vodomer-teplomer\<debug|release>
param(
    [switch]$Release,
    [switch]$Flash,
    [switch]$Monitor,
    [switch]$Menuconfig,
    [switch]$Clean,
    [string]$Port = 'COM10'
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$variant = if ($Release) { 'release' } else { 'debug' }
$build = Join-Path $env:LOCALAPPDATA "idf-build\vodomer-teplomer\$variant"
$defaults = if ($Release) { 'sdkconfig.defaults;sdkconfig.release' } else { 'sdkconfig.defaults' }

# Aktivace ESP-IDF 6.1 (instalace pres EIM do C:\Espressif).
if (-not $env:IDF_PATH) {
    $activate = Get-ChildItem 'C:\Espressif\tools' -Filter '*v6.1*profile.ps1' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $activate) { throw 'ESP-IDF 6.1 nenalezeno v C:\Espressif (eim install -i v6.1)' }
    . $activate.FullName | Out-Null
}

# Stazene komponenty (M5GFX ~ desitky MB) nesmi do Google Drive. Component
# manager ma cestu <projekt>\managed_components napevno -> junction mimo Drive.
$managed = Join-Path $root 'managed_components'
if (-not (Test-Path $managed)) {
    $target = Join-Path $env:LOCALAPPDATA 'idf-build\vodomer-teplomer\managed_components'
    New-Item -ItemType Directory -Force $target | Out-Null
    New-Item -ItemType Junction -Path $managed -Target $target | Out-Null
}

$idfArgs = @('-C', $root, '-B', $build, "-DSDKCONFIG=$build\sdkconfig", "-DSDKCONFIG_DEFAULTS=$defaults")
# Port je globalni volba idf.py - musi byt pred prikazy.
if ($Flash -or $Monitor) { $idfArgs += @('-p', $Port) }
if ($Clean) { $idfArgs += 'fullclean' }
if ($Menuconfig) { $idfArgs += 'menuconfig' }
if (-not $Monitor -or $Flash) { $idfArgs += 'build' }
if ($Flash) { $idfArgs += 'flash' }
# --no-reset: otevreni monitoru nesmi desku resetovat (studeny start = ztrata casu a bufferu).
if ($Monitor) { $idfArgs += @('monitor', '--no-reset') }

# idf.py pise prubeh na stderr - v PowerShell 5.1 to se 'Stop' neni chyba.
$ErrorActionPreference = 'Continue'
idf.py @idfArgs
exit $LASTEXITCODE
