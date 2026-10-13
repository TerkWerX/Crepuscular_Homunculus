# SPDX-License-Identifier: GPL-3.0-only
param(
    [string]$Toolchain = $env:JIELI_TOOLCHAIN,
    [string]$Sdk = $env:AC79_SDK,
    [string]$Release,
    [string]$Product = 'FM-1_960'
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
if (!$Toolchain -or !(Test-Path -LiteralPath (Join-Path $Toolchain 'pi32v2/bin/clang'))) {
    throw 'Provide -Toolchain pointing at the extracted JieLi Linux toolchain.'
}
if (!$Sdk -or !(Test-Path -LiteralPath (Join-Path $Sdk 'cpu/wl82/tools/uboot.boot'))) {
    throw 'Provide -Sdk pointing at the pinned AC79 SDK files.'
}
$env:JIELI_TOOLCHAIN = (Resolve-Path -LiteralPath $Toolchain).Path
$env:AC79_SDK = (Resolve-Path -LiteralPath $Sdk).Path
$env:JIELI_DOCKER = '1'
$builderArgs = @('tools/build.py', '--product', $Product)
if ($Release) { $builderArgs += @('--release', $Release) }
if (Get-Command py -ErrorAction SilentlyContinue) { & py -3 @builderArgs }
else { & python @builderArgs }
if ($LASTEXITCODE -ne 0) { throw "TERKINSTEIN build failed ($LASTEXITCODE)." }
