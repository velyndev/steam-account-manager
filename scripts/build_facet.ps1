# Builds Facet and its libraries (lunasvg, plutovg, FreeType) through the wrapper project in
# cmake\facet, for one configuration and one MSVC toolset.
#
#   .\scripts\build_facet.ps1 -Configuration Debug -Toolset v145
#
# Debug also builds libprotobufd.lib from third_party\protobuf-src, because the tracked
# libprotobuf.lib is a Release /MT build that a /MTd exe cannot link.
#
# The libraries land in third_party\facet-build\<Toolset>\lib\<Configuration>\ and a stamp file
# third_party\facet-build\<Toolset>\<Configuration>.stamp is written after every successful
# build. steam-account-manager.vcxproj runs this script before it compiles (target BuildFacet)
# and links those libraries, so building the solution is enough; running it by hand is only
# needed to look at a CMake error in isolation.
#
# The first run configures the CMake project, which downloads lunasvg (Facet's FetchContent),
# so it needs network access once per toolset.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Debug", "Release")]
    [string] $Configuration,

    [Parameter(Mandatory = $true)]
    [ValidateSet("v143", "v145")]
    [string] $Toolset
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SourceDir = Join-Path $RepoRoot "cmake\facet"
$BuildDir = Join-Path $RepoRoot "third_party\facet-build\$Toolset"
$LibDir = Join-Path $BuildDir "lib"
$Stamp = Join-Path $BuildDir "$Configuration.stamp"

function Write-Step([string] $msg) {
    Write-Host "[facet] $msg"
}

function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) {
        return $onPath.Source
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installs = & $vswhere -all -products * -property installationPath
        foreach ($install in $installs) {
            $candidate = Join-Path $install "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }
    throw "cmake was not found on PATH or in a Visual Studio installation. Install CMake 3.28 or newer."
}

foreach ($required in @("third_party\facet\CMakeLists.txt", "third_party\freetype\CMakeLists.txt",
                        "third_party\imgui\imgui.h")) {
    if (-not (Test-Path (Join-Path $RepoRoot $required))) {
        throw "$required is missing; run scripts\init_third_party.ps1 first."
    }
}

$cmake = Find-CMake

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Step "configuring for $Toolset in $BuildDir"
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    & $cmake -S $SourceDir -B $BuildDir -A x64 -T $Toolset "-DSAM_LIB_DIR=$LibDir" `
        -DFETCHCONTENT_QUIET=ON -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
    if ($LASTEXITCODE -ne 0) {
        # A failed first configure leaves a cache the next run would trust.
        Remove-Item -Force (Join-Path $BuildDir "CMakeCache.txt") -ErrorAction SilentlyContinue
        throw "CMake configure failed with exit $LASTEXITCODE"
    }
}

$targets = @("sam_facet_libs")
$libs = @("facet.lib", "lunasvg.lib", "plutovg.lib", "freetype.lib")
if ($Configuration -eq "Debug") {
    # The tracked libprotobuf.lib is Release-only; Debug links one built from protobuf-src.
    $targets += "sam_protobuf_debug"
    $libs += "libprotobufd.lib"
}

Write-Step "building $Configuration ($Toolset)"
& $cmake --build $BuildDir --config $Configuration --target $targets -- /m /nologo /verbosity:minimal
if ($LASTEXITCODE -ne 0) {
    throw "Facet build failed with exit $LASTEXITCODE"
}

foreach ($lib in $libs) {
    $path = Join-Path $LibDir (Join-Path $Configuration $lib)
    if (-not (Test-Path $path)) {
        throw "the build did not produce $path"
    }
}

Set-Content -Path $Stamp -Value (Get-Date -Format o) -Encoding ascii
Write-Step "libraries in $(Join-Path $LibDir $Configuration)"
