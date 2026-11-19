<#
.SYNOPSIS
    Installs the Grav compiler on Windows, through WSL.

.DESCRIPTION
    Grav builds Linux programs, so on Windows it runs inside WSL (the Windows
    Subsystem for Linux). This script:
      1. checks Windows (WSL 2 needs Windows 10 2004 / build 19041, or Windows 11),
      2. installs WSL and Ubuntu if they are missing (needs an administrator
         PowerShell and usually a restart; run the script again afterwards),
      3. runs install.sh inside the distribution (it checks and installs what
         Grav needs there, and asks where to put things unless -Yes),
      4. puts grav.cmd on your PATH, so `grav` works in PowerShell and cmd:
         it runs grav in WSL from the current folder, and turns Windows paths
         (C:\...) in its arguments into WSL paths.

    Run it from a checkout (.\install.ps1) or straight from GitHub:
      irm https://raw.githubusercontent.com/venomous-maker/Grav/main/install.ps1 | iex

.PARAMETER Distro
    The WSL distribution to install into (default: the default distribution
    when it is Debian or Ubuntu, else Ubuntu).
.PARAMETER Prefix
    Where grav goes inside WSL (default: ~/.local there).
.PARAMETER Yes
    Take every default and ask nothing.
.PARAMETER Optional
    Also install the optional libraries in WSL (TLS, zlib, database clients).
.PARAMETER Check
    Only check the system; change nothing.
#>
[CmdletBinding()]
param(
    [string]$Distro = "",
    [string]$Prefix = "",
    [switch]$Yes,
    [switch]$Optional,
    [switch]$Check
)

$ErrorActionPreference = "Stop"
$RawBase = "https://raw.githubusercontent.com/venomous-maker/Grav/main"
$ShimDir = Join-Path $env:LOCALAPPDATA "Programs\grav\bin"
$Record  = Join-Path $env:LOCALAPPDATA "Programs\grav\install.json"

function Step($m) { Write-Host ""; Write-Host "==> $m" -ForegroundColor White }
function Ok($m)   { Write-Host "  + $m" -ForegroundColor Green }
function Warn($m) { Write-Host "  ! $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host ""; Write-Host "error: $m" -ForegroundColor Red; exit 1 }
function Ask($q, $def) {
    if ($Yes -or -not [Environment]::UserInteractive) { return $def }
    $r = Read-Host "  $q [$def]"
    if ([string]::IsNullOrWhiteSpace($r)) { return $def } else { return $r.Trim() }
}
function Confirm($q, $def) { return (Ask "$q (y/n)" $def) -match '^[Yy]' }

# wsl.exe writes UTF-16 unless told otherwise.
$env:WSL_UTF8 = "1"
# Runs wsl.exe and returns its output as one trimmed string. Commands go
# through --exec (no Linux shell re-reading the quotes).
function Wsl {
    $out = & wsl.exe @args 2>&1
    $script:WslCode = $LASTEXITCODE
    return (($out | Out-String) -replace "`0", "").Trim()
}
# Runs a shell script in the distribution. It travels as base64: Windows
# PowerShell mangles quotes in native arguments and adds CRLFs to piped text.
function WslScript($d, $text) {
    $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($text -replace "`r`n", "`n")))
    return Wsl -d $d --exec sh -c "echo $b64 | base64 -d | sh"
}

# ── the system ──────────────────────────────────────────────────────────────────
Step "Checking the system"
if ($PSVersionTable.PSEdition -eq "Core" -and -not $IsWindows) {
    Die "this is the Windows installer; on Linux run ./install.sh (macOS is not supported yet)"
}
$os = [Environment]::OSVersion.Version
$build = $os.Build
if ($os.Major -lt 10 -or $build -lt 19041) {
    Die "Windows 10 version 2004 (build 19041) or newer is needed for WSL 2; this is build $build"
}
Ok ("Windows {0} (build {1}, {2})" -f $(if ($build -ge 22000) { "11" } else { "10" }), $build, $env:PROCESSOR_ARCHITECTURE)
if ($env:PROCESSOR_ARCHITECTURE -notin @("AMD64", "ARM64")) { Warn "$env:PROCESSOR_ARCHITECTURE is untested" }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

$haveWsl = [bool](Get-Command wsl.exe -ErrorAction SilentlyContinue)
$distros = @()
if ($haveWsl) {
    $list = Wsl --list --quiet
    if ($WslCode -eq 0) { $distros = @($list -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
}
if ($haveWsl -and $WslCode -eq 0) { Ok "WSL is installed ($($distros.Count) distribution(s))" }
else { Warn "WSL is not installed" }

if ($Check) {
    if ($haveWsl -and $distros.Count -gt 0) { Write-Host "`nReady: install.ps1 will install grav into WSL."; exit 0 }
    Write-Host "`nWSL (with a distribution) is missing: install.ps1 can install it from an administrator PowerShell."
    exit 1
}

# ── WSL ─────────────────────────────────────────────────────────────────────────
if (-not $haveWsl -or $distros.Count -eq 0) {
    Step "Installing WSL and Ubuntu"
    if (-not $isAdmin) {
        Die "installing WSL needs an administrator PowerShell: right-click PowerShell, 'Run as administrator', and run this script again"
    }
    if (-not (Confirm "Install WSL with Ubuntu now? (a restart is usually needed)" "y")) { Die "WSL is needed to run grav on Windows" }
    & wsl.exe --install -d Ubuntu
    Write-Host ""
    Write-Host "WSL is being installed. Restart Windows if asked, open Ubuntu once from the Start menu to"
    Write-Host "create your Linux user, then run this script again (as yourself, not as administrator)."
    exit 0
}

# Which distribution: Debian or Ubuntu, so install.sh can use apt.
function Family($d) {
    $rel = Wsl -d $d --exec cat /etc/os-release
    if ($WslCode -ne 0) { return "" }
    if ($rel -match '(?m)^(ID|ID_LIKE)=.*\b(debian|ubuntu)\b') { return "debian" }
    return "other"
}
if (-not $Distro) {
    $default = (Wsl --list --verbose) -split "`r?`n" | Where-Object { $_ -match '^\s*\*' } |
        ForEach-Object { ($_ -replace '^\s*\*\s*', '' -split '\s+')[0] } | Select-Object -First 1
    $Distro = $default
    if (-not $Distro -or (Family $Distro) -ne "debian") {
        $Distro = $distros | Where-Object { (Family $_) -eq "debian" } | Select-Object -First 1
    }
    if (-not $Distro) { Die "no Debian or Ubuntu distribution in WSL: install one with 'wsl --install -d Ubuntu'" }
    $Distro = Ask "WSL distribution to install into" $Distro
}
if ($distros -notcontains $Distro) { Die "WSL has no distribution named '$Distro' (have: $($distros -join ', '))" }
$fam = Family $Distro
if ($fam -eq "") { Die "'$Distro' does not start; open it once from the Start menu to finish setting it up" }
if ($fam -ne "debian") { Warn "'$Distro' is not Debian or Ubuntu: install.sh will list what is missing instead of installing it" }
Ok "distribution: $Distro"

# ── grav inside WSL ─────────────────────────────────────────────────────────────
Step "Installing grav in $Distro"
$shArgs = @()
if ($Yes) { $shArgs += "--yes" }
if ($Optional) { $shArgs += "--optional" }
if ($Prefix) { $shArgs += @("--prefix", $Prefix) }
$here = if ($PSScriptRoot) { $PSScriptRoot } else { "" }
if ($here -and (Test-Path (Join-Path $here "install.sh")) -and (Test-Path (Join-Path $here "CMakeLists.txt"))) {
    # From a checkout: build it in place (WSL sees it under /mnt/<drive>).
    $script = Wsl -d $Distro --exec wslpath -a ($here + "\install.sh")
    & wsl.exe -d $Distro --exec bash $script @shArgs
} else {
    & wsl.exe -d $Distro --exec bash -c "curl -fsSL $RawBase/install.sh -o /tmp/grav-install.sh && bash /tmp/grav-install.sh $($shArgs -join ' ')"
}
if ($LASTEXITCODE -ne 0) { Die "install.sh failed in $Distro (see above)" }

# Where it went, from install.sh's record.
$wslPrefix = WslScript $Distro @'
for f in "$HOME/.local/share/grav/install.env" /usr/local/share/grav/install.env; do
    if [ -f "$f" ]; then . "$f"; echo "$GRAV_PREFIX"; exit 0; fi
done
'@
if ($Prefix) { $wslPrefix = WslScript $Distro "cd $Prefix 2>/dev/null && pwd" }
if (-not $wslPrefix) { Die "grav was installed but its install record cannot be found in $Distro" }

# A small wrapper on the WSL side: Windows paths in the arguments become WSL
# paths, and the environment install.sh wrote is loaded.
$wrapper = @'
#!/bin/sh
# Written by Grav's install.ps1: runs grav for grav.cmd on Windows.
here="$(dirname "$(readlink -f "$0")")"
[ -f "$here/env.sh" ] && . "$here/env.sh"
n=$#
while [ "$n" -gt 0 ]; do
    a="$1"; shift
    case "$a" in
        [A-Za-z]:\\*|[A-Za-z]:/*) a="$(wslpath -a "$a")" ;;
    esac
    set -- "$@" "$a"
    n=$((n - 1))
done
exec "$here/../../bin/grav" "$@"
'@
$wrapperPath = "$wslPrefix/share/grav/grav-windows"
$b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($wrapper -replace "`r`n", "`n")))
$null = WslScript $Distro "echo $b64 | base64 -d > '$wrapperPath' && chmod +x '$wrapperPath'"
if ($WslCode -ne 0) { Die "could not write $wrapperPath in $Distro" }

# ── the Windows side ────────────────────────────────────────────────────────────
Step "Adding grav to the Windows PATH"
New-Item -ItemType Directory -Force -Path $ShimDir | Out-Null
$cmd = "@echo off`r`nwsl.exe -d $Distro --cd `"%CD%`" --exec `"$wrapperPath`" %*`r`nexit /b %ERRORLEVEL%`r`n"
Set-Content -Path (Join-Path $ShimDir "grav.cmd") -Value $cmd -NoNewline -Encoding ASCII
Ok (Join-Path $ShimDir "grav.cmd")

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (-not $userPath) { $userPath = "" }
if (($userPath -split ";") -notcontains $ShimDir) {
    [Environment]::SetEnvironmentVariable("Path", ($userPath.TrimEnd(";") + ";" + $ShimDir).TrimStart(";"), "User")
    Ok "added $ShimDir to your PATH"
} else { Ok "$ShimDir is already on your PATH" }
if (($env:Path -split ";") -notcontains $ShimDir) { $env:Path = "$env:Path;$ShimDir" }

@{ distro = $Distro; prefix = $wslPrefix; shim = $ShimDir; installedAt = (Get-Date).ToUniversalTime().ToString("o") } |
    ConvertTo-Json | Set-Content -Path $Record -Encoding UTF8

# ── try it ──────────────────────────────────────────────────────────────────────
Step "Trying it"
$v = & (Join-Path $ShimDir "grav.cmd") env 2>&1 | Out-String
if ($LASTEXITCODE -eq 0 -and $v -match "GRAV_VERSION") { Ok "grav runs from Windows" }
else { Write-Host $v; Die "grav.cmd could not run grav in $Distro" }

Write-Host ""
Write-Host "grav is installed (in WSL: $Distro)." -ForegroundColor Green
Write-Host "Open a new terminal, then: grav run hello.grav"
Write-Host "Programs grav builds are Linux programs: run them with 'grav run', or 'wsl ./app'."
Write-Host "Update later with: grav self-update"
