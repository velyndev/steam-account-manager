# Fetches the vendored dependencies into third_party/.
# Run this once after cloning the repo, and any time a dependency version changes.
#
# Versions are pinned by tag. Bumping a version means: change the tag here,
# delete the matching third_party/<name> directory, and run this script again.

[CmdletBinding()]
param(
    [switch] $Force,
    [switch] $Quiet
)

$ErrorActionPreference = "Stop"

# Git writes its progress to stderr; PowerShell, when it sees stderr output from
# a native command, treats it as a non-terminating error and (with strict modes)
# can flip $? to false even on a clean exit. Suppress that behaviour for the
# duration of this script.
$PSNativeCommandUseErrorActionPreference = $false

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Args)
    & git @Args
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Args -join ' ') failed with exit $LASTEXITCODE"
    }
}

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ThirdParty = Join-Path $RepoRoot "third_party"

if (-not (Test-Path $ThirdParty)) {
    New-Item -ItemType Directory -Path $ThirdParty | Out-Null
}

function Write-Step([string] $msg) {
    if (-not $Quiet) {
        Write-Host "[third_party] $msg" -ForegroundColor Cyan
    }
}

function Ensure-GitClone {
    param(
        [string] $Name,
        [string] $Url,
        [string] $Tag,
        [switch] $Shallow
    )

    $target = Join-Path $ThirdParty $Name

    if ((Test-Path $target) -and -not $Force) {
        Write-Step "$Name already present, skipping."
        return
    }

    if ($Force -and (Test-Path $target)) {
        Write-Step "removing existing $Name"
        Remove-Item -Recurse -Force $target
    }

    Write-Step "cloning $Name@$Tag"
    if ($Shallow) {
        Invoke-Git clone --depth 1 --branch $Tag $Url $target
    } else {
        Invoke-Git clone $Url $target
        Push-Location $target
        try {
            Invoke-Git checkout $Tag
        } finally {
            Pop-Location
        }
    }
}

function Ensure-SingleFile {
    param(
        [string] $Name,
        [string] $Url,
        [string] $RelPath
    )

    $target = Join-Path $ThirdParty (Join-Path $Name $RelPath)
    $dir = Split-Path -Parent $target

    if ((Test-Path $target) -and -not $Force) {
        Write-Step "$Name/$RelPath already present, skipping."
        return
    }

    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }

    Write-Step "downloading $Name/$RelPath"
    Invoke-WebRequest -Uri $Url -OutFile $target -UseBasicParsing
}

function Ensure-ZipDir {
    param(
        [string] $Name,
        [string] $Url,
        [string] $InnerDir
    )

    $target = Join-Path $ThirdParty $Name

    if ((Test-Path $target) -and -not $Force) {
        Write-Step "$Name already present, skipping."
        return
    }

    if ($Force -and (Test-Path $target)) {
        Write-Step "removing existing $Name"
        Remove-Item -Recurse -Force $target
    }

    $staging = Join-Path ([IO.Path]::GetTempPath()) ("sam-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $staging | Out-Null
    try {
        $zip = Join-Path $staging "download.zip"
        Write-Step "downloading $Name"
        Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath (Join-Path $staging "x")
        Move-Item -Path (Join-Path $staging (Join-Path "x" $InnerDir)) -Destination $target
    } finally {
        Remove-Item -Recurse -Force $staging -ErrorAction SilentlyContinue
    }
}

Ensure-GitClone -Name "imgui" `
    -Url "https://github.com/ocornut/imgui.git" `
    -Tag "v1.93.0-docking" `
    -Shallow

# Facet: the Dear ImGui design framework the UI is built on. Pinned to a commit, not a
# tag, so the clone is a full one. Built by scripts\build_facet.ps1 through the wrapper
# project in cmake\facet, which the vcxproj runs before compiling.
Ensure-GitClone -Name "facet" `
    -Url "https://github.com/velyndev/imgui-facet.git" `
    -Tag "bbbf05b27d537829e079d89c0960d3168d4f805c"

# FreeType: the rasterizer Facet needs to pick the Segoe UI Variable instances. Same tag
# and the same options as Facet's cmake\FacetDeps.cmake; built by the same wrapper project.
Ensure-GitClone -Name "freetype" `
    -Url "https://github.com/freetype/freetype.git" `
    -Tag "VER-2-15-0" `
    -Shallow

# protobuf sources of the release whose headers and Release library are tracked in
# third_party/protobuf. Only Debug builds use them: build_facet.ps1 compiles a /MTd
# libprotobufd.lib from them, which the tracked /MT library cannot replace.
Ensure-ZipDir -Name "protobuf-src" `
    -Url "https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.zip" `
    -InnerDir "protobuf-3.21.12"

Ensure-GitClone -Name "mbedtls" `
    -Url "https://github.com/Mbed-TLS/mbedtls.git" `
    -Tag "v3.6.2" `
    -Shallow

Ensure-GitClone -Name "spdlog" `
    -Url "https://github.com/gabime/spdlog.git" `
    -Tag "v1.14.1" `
    -Shallow

Ensure-GitClone -Name "doctest" `
    -Url "https://github.com/doctest/doctest.git" `
    -Tag "v2.4.11" `
    -Shallow

Ensure-GitClone -Name "utfcpp" `
    -Url "https://github.com/nemtrif/utfcpp.git" `
    -Tag "v4.0.5" `
    -Shallow

Ensure-SingleFile -Name "nlohmann_json" `
    -Url "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" `
    -RelPath "single_include/nlohmann/json.hpp"

Ensure-SingleFile -Name "stb" `
    -Url "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" `
    -RelPath "stb_image.h"

Ensure-SingleFile -Name "stb" `
    -Url "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" `
    -RelPath "stb_image_write.h"

# puff: Mark Adler's tiny DEFLATE decompressor (zlib license), used to inflate
# gzip-compressed CMsgMulti payloads from the Steam CM.
Ensure-SingleFile -Name "puff" `
    -Url "https://raw.githubusercontent.com/madler/zlib/v1.3.1/contrib/puff/puff.c" `
    -RelPath "puff.c"

Ensure-SingleFile -Name "puff" `
    -Url "https://raw.githubusercontent.com/madler/zlib/v1.3.1/contrib/puff/puff.h" `
    -RelPath "puff.h"

Ensure-GitClone -Name "minhook" `
    -Url "https://github.com/TsudaKageyu/minhook.git" `
    -Tag "v1.3.3" `
    -Shallow

Ensure-SingleFile -Name "xorstr" `
    -Url "https://raw.githubusercontent.com/JustasMasiulis/xorstr/master/include/xorstr.hpp" `
    -RelPath "xorstr.hpp"

Write-Step "done"
