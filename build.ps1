# Build Kadran from source and open it, on Windows 10/11 (x64). Run from build.cmd (double-click), build.sh (Git Bash)
# or PowerShell: powershell -ExecutionPolicy Bypass -File build.ps1 [options]
#
#   --no-install            do not install anything (the requirements are there)
#   --no-run                build only
#   --clean                 start from an empty build folder
#   --debug                 a Debug build
#
# It installs with winget what is missing: Git, CMake, Ninja, Python and the Visual Studio 2022
# Build Tools (C++). Qt (with aqtinstall) goes to build-deps\, at the version the CI uses (.github/workflows/build.yml).
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$Install = $true; $Run = $true; $Clean = $false; $Type = "Release"
foreach ($a in $args) {
    switch ($a) {
        "--no-install" { $Install = $false }
        "--no-run" { $Run = $false }
        "--clean" { $Clean = $true }
        "--debug" { $Type = "Debug" }
        { $_ -in "-h", "--help" } { Get-Content $PSCommandPath -TotalCount 11 | ForEach-Object { $_ -replace '^# ?', '' }; exit 0 }
        default { Write-Host "Unknown option: $a (see build.ps1 --help)" -ForegroundColor Red; exit 2 }
    }
}

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "`nError: $text" -ForegroundColor Red; exit 1 }
function Have($name) { [bool](Get-Command $name -ErrorAction SilentlyContinue) }
function Native($what) { if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit code $LASTEXITCODE)" } }
# What winget installed is on the PATH of new windows only: read it again
function RefreshPath {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
    foreach ($d in "$env:ProgramFiles\CMake\bin") {
        if ((Test-Path $d) -and ($env:Path -notlike "*$d*")) { $env:Path += ";$d" }
    }
}

# --- Requirements
if ($Install) {
    $packages = @(
        @{ Id = "Git.Git"; Cmd = "git" },
        @{ Id = "Kitware.CMake"; Cmd = "cmake" },
        @{ Id = "Ninja-build.Ninja"; Cmd = "ninja" },
        @{ Id = "Python.Python.3.12"; Cmd = "py" }
    )
    $missing = @($packages | Where-Object { -not (Have $_.Cmd) -and -not ($_.Cmd -eq "py" -and (Have python)) })
    if ($missing) {
        if (-not (Have winget)) {
            Fail "winget is needed to install $($missing.Id -join ', ') (App Installer, from the Microsoft Store); or install them yourself and use --no-install"
        }
        Step "Installing what is missing with winget"
        foreach ($p in $missing) {
            Write-Host "  $($p.Id)"
            winget install --id $p.Id -e --source winget --accept-package-agreements --accept-source-agreements --silent
            RefreshPath
        }
    }
}
RefreshPath
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
function FindVs {
    if (-not (Test-Path $vswhere)) { return $null }
    & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$vs = FindVs
if (-not $vs) {
    if (-not $Install) { Fail "Visual Studio 2022 (or its Build Tools) with the C++ tools is needed" }
    Step "Installing the Visual Studio 2022 Build Tools (C++), a few GB: this takes a while"
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget --accept-package-agreements --accept-source-agreements `
        --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait"
    $vs = FindVs
    if (-not $vs) { Fail "the C++ build tools are not found after their installation: run build again in a new window" }
}
foreach ($c in "git", "cmake", "ninja") { if (-not (Have $c)) { Fail "$c is not installed (open a new window if it was just installed)" } }
# Python by its executable's path: PowerShell names are case-insensitive, a function "py" would call itself
$launcher = Get-Command py -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
$pythonExe = Get-Command python -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $launcher -and -not $pythonExe) { Fail "Python 3 is not installed" }
function Invoke-Python {
    if ($launcher) { & $launcher.Source -3 @args } else { & $pythonExe.Source @args }
}

# The compiler's environment (cl, link, the Windows SDK), for x64
Step "Visual Studio: $vs"
Import-Module (Join-Path $vs "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
Set-Location $PSScriptRoot

# --- The version the CI builds with (the Windows job of .github/workflows/build.yml)
$workflow = Get-Content .github\workflows\build.yml -Raw
$m = [regex]::Match($workflow, "package-windows:.*?version:\s*(\d+\.\d+\.\d+)", "Singleline")
if (-not $m.Success) { Fail "Qt version not found in .github/workflows/build.yml" }
$qtVersion = $m.Groups[1].Value
$qtArch = "win64_msvc2022_64"
$deps = Join-Path $PSScriptRoot "build-deps"
New-Item -ItemType Directory -Force $deps | Out-Null

# --- Qt (with Qt WebSockets), through aqtinstall
$qtDir = Join-Path $deps "Qt\$qtVersion\$($qtArch -replace '^win64_', '')"
if (-not (Test-Path "$qtDir\bin\qmake.exe") -or -not (Test-Path "$qtDir\lib\cmake\Qt6WebSockets")) {
    if (-not $Install) { Fail "Qt $qtVersion is not in $qtDir" }
    Step "Installing Qt $qtVersion ($qtArch, with Qt WebSockets) in build-deps\Qt"
    Invoke-Python -m pip install --user --upgrade aqtinstall; Native "pip install aqtinstall"
    Invoke-Python -m aqt install-qt windows desktop $qtVersion $qtArch -m qtwebsockets -O (Join-Path $deps "Qt"); Native "Qt installation"
}

# --- Sources: the submodules (third_party\)
if (Test-Path .git) {
    Step "Fetching the submodules"
    git submodule update --init --recursive; Native "git submodule update"
} elseif (-not (Test-Path third_party\pxs-qt\CMakeLists.txt)) {
    Fail "third_party\ is empty: clone the repository with git (git clone --recurse-submodules https://github.com/PixelStereo/Kadran.git)"
}

# --- Build
if ($Clean -and (Test-Path build)) { Remove-Item -Recurse -Force build }
$cmakeArgs = @("-S", ".", "-B", "build", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=$Type", "-DCMAKE_PREFIX_PATH=$qtDir", "-DKADRAN_TESTS=OFF")
Step "Configuring (cmake $($cmakeArgs -join ' '))"
cmake @cmakeArgs; Native "CMake configure"
Step "Building"
cmake --build build; Native "Build"
# Qt's libraries and plugins next to the program
$appName = (Get-Content build/application.json -Raw | ConvertFrom-Json).name
$deployArgs = @(if ($Type -eq "Debug") { "--debug" } else { "--release" }) + @("--no-compiler-runtime", "build\${appName}.exe")
& "$qtDir\bin\windeployqt.exe" @deployArgs | Out-Null; Native "windeployqt"

# --- Open
$exe = Join-Path $PSScriptRoot "build\${appName}.exe"
if (-not (Test-Path $exe)) { Fail "build\${appName}.exe was not built" }
Write-Host "`nKadran is built: $exe" -ForegroundColor Green
if ($Run) {
    # A Kadran that is already running answers for the new one: closed first, so that this version is the one that opens
    $old = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like "Kadran *" }
    if ($old) { Step "Closing the Kadran that is running"; $old | Stop-Process -Force; Start-Sleep -Seconds 1 }
}
if ($Run) { Start-Process $exe -WorkingDirectory (Join-Path $PSScriptRoot "build") }
