$ErrorActionPreference = "Stop"

# Generators other than Visual Studio (e.g. Ninja) expect the MSVC developer environment to be
# active. Run vcvars64 and persist every variable it changes for subsequent GitHub Actions steps.

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installPath) {
    throw "No Visual Studio installation with C++ tools found"
}
$vcvars = Join-Path $installPath "VC\Auxiliary\Build\vcvars64.bat"

# vcvars itself looks for vswhere on the PATH
$env:Path = "$(Split-Path $vswhere);$env:Path"

$before = @{}
Get-ChildItem env: | ForEach-Object { $before[$_.Name] = $_.Value }

$after = cmd /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to run $vcvars"
}

$changed = $after | Where-Object { $_ -match '^([^=]+)=(.*)$' -and $before[$Matches[1]] -ne $Matches[2] }
# Without a BOM, which Windows PowerShell's utf8 encoding would add
[IO.File]::AppendAllLines($env:GITHUB_ENV, [string[]]$changed, [Text.UTF8Encoding]::new($false))

Write-Host "MSVC developer environment set up from $installPath ($($changed.Count) variables)"
