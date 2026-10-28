# configure + build + test, works from any dir:
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Config Release] [-NoTest]
param(
    [ValidateSet('Debug', 'Release')] [string]$Config = 'Release',
    [switch]$NoTest
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'

cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE) { throw 'cmake configure failed' }
cmake --build $build --config $Config --parallel
if ($LASTEXITCODE) { throw 'build failed' }
if (-not $NoTest) {
    ctest --test-dir $build -C $Config --output-on-failure
    if ($LASTEXITCODE) { throw 'tests failed' }
}
