# Writes SHA256SUMS.txt for every file in a folder (relative paths, sha256sum format)
# so people can check a download against what CI built.
#   pwsh scripts/checksums.ps1 build/dist [out-file]
param(
    [Parameter(Mandatory)] [string]$Dir,
    [string]$Out
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Dir).ProviderPath
if (-not $Out) { $Out = Join-Path $root 'SHA256SUMS.txt' }
$lines = Get-ChildItem -LiteralPath $root -Recurse -File |
    Where-Object { $_.FullName -ne $Out } |
    Sort-Object FullName |
    ForEach-Object {
        $sha = [System.Security.Cryptography.SHA256]::Create()
        $fs = [System.IO.File]::OpenRead($_.FullName)
        try { $h = [System.BitConverter]::ToString($sha.ComputeHash($fs)).Replace('-', '').ToLowerInvariant() }
        finally { $fs.Dispose(); $sha.Dispose() }
        $rel = $_.FullName.Substring($root.Length).TrimStart('\', '/').Replace('\', '/')
        "$h  $rel"
    }
Set-Content -LiteralPath $Out -Value $lines -Encoding ascii
Write-Host "wrote $($lines.Count) checksums to $Out"
