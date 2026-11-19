<#
.SYNOPSIS
    Removes Grav from Windows (what install.ps1 set up).

.DESCRIPTION
    Removes grav.cmd and its PATH entry, and (unless -KeepWsl) runs
    uninstall.sh inside the WSL distribution grav was installed into. WSL and
    the distribution themselves stay: remove them with 'wsl --unregister <name>'
    if you no longer want them.

.PARAMETER Yes
    Ask nothing (keeps the package cache unless -Purge).
.PARAMETER Purge
    Also delete the package cache inside WSL.
.PARAMETER KeepWsl
    Only remove the Windows side; leave grav installed inside WSL.
#>
[CmdletBinding()]
param(
    [switch]$Yes,
    [switch]$Purge,
    [switch]$KeepWsl
)

$ErrorActionPreference = "Stop"
$ShimDir = Join-Path $env:LOCALAPPDATA "Programs\grav\bin"
$Record  = Join-Path $env:LOCALAPPDATA "Programs\grav\install.json"
$env:WSL_UTF8 = "1"

function Ok($m)  { Write-Host "  + $m" -ForegroundColor Green }
function Warn($m) { Write-Host "  ! $m" -ForegroundColor Yellow }

$info = $null
if (Test-Path $Record) { $info = Get-Content $Record -Raw | ConvertFrom-Json }
else { Warn "no install record at $Record; removing what can be found" }

if (-not $Yes) {
    $what = if ($KeepWsl) { "grav.cmd and its PATH entry" } else { "grav from Windows and from WSL ($($info.distro))" }
    $r = Read-Host "Remove $what? (y/n) [n]"
    if ($r -notmatch '^[Yy]') { Write-Host "Nothing removed."; exit 0 }
}

# ── inside WSL ──────────────────────────────────────────────────────────────────
if (-not $KeepWsl -and $info -and $info.distro) {
    $sh = "$($info.prefix)/share/grav/src/uninstall.sh"
    $flags = @("--yes")
    if ($Purge) { $flags += "--purge" }
    # uninstall.sh lives in the source checkout; fall back to GitHub when the
    # checkout was elsewhere.
    $script = "if [ -f '$sh' ]; then u='$sh'; else . '$($info.prefix)/share/grav/install.env' 2>/dev/null; u=`"`$GRAV_SRC/uninstall.sh`"; fi; " +
              "if [ ! -f `"`$u`" ]; then u=/tmp/grav-uninstall.sh; curl -fsSL https://raw.githubusercontent.com/venomous-maker/Grav/main/uninstall.sh -o `$u; fi; " +
              "bash `"`$u`" --prefix '$($info.prefix)' $($flags -join ' ')"
    $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($script))
    & wsl.exe -d $info.distro --exec sh -c "echo $b64 | base64 -d | sh"
    if ($LASTEXITCODE -eq 0) { Ok "removed grav from $($info.distro)" }
    else { Warn "uninstall.sh failed in $($info.distro); the Windows side is still removed" }
}

# ── the Windows side ────────────────────────────────────────────────────────────
$dir = if ($info -and $info.shim) { $info.shim } else { $ShimDir }
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir; Ok "removed $dir" }
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath) {
    $kept = ($userPath -split ";") | Where-Object { $_ -and $_ -ne $dir }
    if (($kept -join ";") -ne $userPath) {
        [Environment]::SetEnvironmentVariable("Path", ($kept -join ";"), "User")
        Ok "removed $dir from your PATH"
    }
}
if (Test-Path $Record) { Remove-Item -Force $Record }
$parent = Split-Path $Record
if ((Test-Path $parent) -and -not (Get-ChildItem $parent)) { Remove-Item $parent }

Write-Host ""
Write-Host "grav is uninstalled." -ForegroundColor Green
if ($info -and $info.distro) { Write-Host "WSL and '$($info.distro)' are still installed (remove with: wsl --unregister $($info.distro))." }
