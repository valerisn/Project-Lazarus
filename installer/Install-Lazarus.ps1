<#
.SYNOPSIS
    Installs, removes or inspects Project Lazarus for The Walking Dead: Destinies.

.DESCRIPTION
    Install adds exactly two things to the game folder: version.dll and a
    Lazarus\ folder. No original game file is modified. If a version.dll
    already exists (some other tool), it is backed up and restored on
    uninstall.

    Everything installed is recorded in Lazarus\install-manifest.txt, which is
    what -Uninstall works from.

.EXAMPLE
    .\Install-Lazarus.ps1                      # find the game, confirm, install
    .\Install-Lazarus.ps1 -GameDir "E:\Games\TWD Destinies"
    .\Install-Lazarus.ps1 -Uninstall
    .\Install-Lazarus.ps1 -Status
#>
[CmdletBinding(DefaultParameterSetName = 'Install')]
param(
    [string]$GameDir,
    [Parameter(ParameterSetName = 'Uninstall')][switch]$Uninstall,
    [Parameter(ParameterSetName = 'Uninstall')][switch]$KeepSettings,
    [Parameter(ParameterSetName = 'Status')][switch]$Status,
    [Parameter(ParameterSetName = 'Logs')][switch]$OpenLogs,
    [switch]$Yes,        # don't ask for confirmation
    [switch]$Force,      # proceed past version / ownership warnings
    [string]$SourceDir,  # where version.dll + Lazarus\ come from (default: next to this script)
    [string]$SteamRoot   # override Steam location (mostly for tests)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$script:AppId = '2192790'
$script:ExeName = 'The Walking Dead Destinies.exe'
$script:ExpectedUnity = '2021.3.17'
# SHA-256 prefixes of GameAssembly.dll for builds Lazarus was checked against.
# keep in sync with kKnownBuilds in src/core/state.cpp
$script:KnownBuilds = @{ '1b579b75b1486c0e' = 'Steam build 12839205 (December 2023 release)' }
$script:ManifestName = 'install-manifest.txt'
# user-editable files: never overwritten on reinstall
$script:UserFiles = @('Lazarus\lazarus.ini', 'Lazarus\mods\*\config.ini')
# folders under Lazarus\ that only the loader writes to (cleared on uninstall)
$script:RuntimeDirs = @('logs', 'state', 'backup')
# files our mods write into their own folder at runtime (key-rebind's action dumps)
$script:GeneratedFiles = @('actions-*.json')

class LazarusError : System.Exception {
    LazarusError([string]$m) : base($m) {}
}

function Write-Step([string]$msg) { Write-Host "  $msg" }
function Fail([string]$msg) { throw [LazarusError]::new($msg) }

# ------------------------------------------------------------------ discovery

function Get-SteamRoot {
    if ($SteamRoot) { return $SteamRoot }
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        try {
            $p = Get-ItemProperty -Path $key -ErrorAction Stop
            foreach ($name in 'SteamPath', 'InstallPath') {
                if ($p.PSObject.Properties[$name] -and $p.$name) { return ($p.$name -replace '/', '\') }
            }
        } catch {}
    }
    return $null
}

# libraryfolders.vdf is Valve's KeyValues format; we only need the "path" lines
function Get-SteamLibraries([string]$root) {
    $libs = New-Object System.Collections.Generic.List[string]
    if (-not $root) { return $libs }
    $libs.Add($root)
    $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        foreach ($m in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) {
            $p = $m.Groups[1].Value -replace '\\\\', '\'
            if (-not ($libs -contains $p)) { $libs.Add($p) }
        }
    }
    return $libs
}

function Find-InSteamLibraries {
    $found = New-Object System.Collections.Generic.List[string]
    foreach ($lib in (Get-SteamLibraries (Get-SteamRoot))) {
        $apps = Join-Path $lib 'steamapps'
        $acf = Join-Path $apps "appmanifest_$($script:AppId).acf"
        if (Test-Path -LiteralPath $acf) {
            $m = [regex]::Match((Get-Content -LiteralPath $acf -Raw), '"installdir"\s+"([^"]+)"')
            if ($m.Success) {
                $dir = Join-Path $apps ("common\" + $m.Groups[1].Value)
                if (Test-Path -LiteralPath (Join-Path $dir $script:ExeName)) { $found.Add($dir) }
            }
        }
        # manifest can be missing (moved library, manual copy) - check common\ directly too
        $common = Join-Path $apps 'common'
        if (Test-Path -LiteralPath $common) {
            Get-ChildItem -LiteralPath $common -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                if ((Test-Path -LiteralPath (Join-Path $_.FullName $script:ExeName)) -and -not ($found -contains $_.FullName)) {
                    $found.Add($_.FullName)
                }
            }
        }
    }
    return $found
}

# last resort: shallow walk of every fixed drive. depth 4 covers things like
# D:\Games\TWD\The Walking Dead Destinies.exe without crawling all of C:\
function Find-OnDrives([int]$MaxDepth = 4) {
    $found = New-Object System.Collections.Generic.List[string]
    # these trees are enormous and never hold a game install. the first
    # version of this scan didn't skip them and took 5 minutes on a dev box.
    $skip = @('Windows', '$Recycle.Bin', 'System Volume Information', 'ProgramData', 'Recovery', 'AppData',
        'WindowsApps', 'node_modules', '.git', 'Microsoft', 'Windows Kits', 'Microsoft Visual Studio')
    $sys = $env:SystemDrive.TrimEnd('\') + '\'
    # other drives first: a game on a custom path is almost always on a
    # secondary drive, and C:\ is the slowest to walk
    $drives = @(Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -match '^[A-Z]:\\$' -and $_.Free -ne $null } |
        Sort-Object @{ Expression = { $_.Root -eq $sys } }, Root)
    foreach ($d in $drives) {
        if ($d.Root -eq $sys -and $found.Count) { break }  # already found it elsewhere, don't bother with C:
        Write-Step "searching $($d.Root)"
        $queue = New-Object System.Collections.Generic.Queue[object]
        $queue.Enqueue(@($d.Root, 0))
        while ($queue.Count) {
            $item = $queue.Dequeue()
            $dir = $item[0]; $depth = $item[1]
            if (Test-Path -LiteralPath (Join-Path $dir $script:ExeName)) { $found.Add($dir.TrimEnd('\')); continue }
            if ($depth -ge $MaxDepth) { continue }
            try {
                foreach ($sub in [System.IO.Directory]::EnumerateDirectories($dir)) {
                    if ($skip -contains [System.IO.Path]::GetFileName($sub)) { continue }
                    $queue.Enqueue(@($sub, $depth + 1))
                }
            } catch {}  # access denied etc, just move on
        }
    }
    return $found
}

function Resolve-GameDir {
    if ($GameDir) { return (Resolve-Path -LiteralPath $GameDir -ErrorAction Stop).ProviderPath }
    Write-Host 'Looking for The Walking Dead: Destinies...'
    $c = @(Find-InSteamLibraries)
    if (-not $c.Count) {
        Write-Step 'Not found in Steam libraries, searching drives (this can take a minute)...'
        $c = @(Find-OnDrives)
    }
    $c = @($c | Select-Object -Unique)
    if ($c.Count -eq 0) {
        Fail "Could not find the game automatically. Run again with -GameDir `"<folder containing $($script:ExeName)>`""
    }
    if ($c.Count -eq 1) { return $c[0] }
    if ($Yes) { Fail ("Found several installs, pick one with -GameDir:`n  " + ($c -join "`n  ")) }
    Write-Host 'Found more than one install:'
    for ($i = 0; $i -lt $c.Count; $i++) { Write-Host "  [$($i + 1)] $($c[$i])" }
    $pick = Read-Host 'Which one? (number)'
    $n = 0
    if (-not [int]::TryParse($pick, [ref]$n) -or $n -lt 1 -or $n -gt $c.Count) { Fail 'No install selected.' }
    return $c[$n - 1]
}

# ----------------------------------------------------------------- validation

# plain .NET instead of Get-FileHash: when Windows PowerShell is started from
# pwsh 7 it inherits pwsh's PSModulePath and Get-FileHash just isn't there.
# found that one the hard way on CI.
function Get-FileHashHex([string]$file) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $fs = [System.IO.File]::OpenRead($file)
        try { $bytes = $sha.ComputeHash($fs) } finally { $fs.Dispose() }
    } finally { $sha.Dispose() }
    return [System.BitConverter]::ToString($bytes).Replace('-', '').ToLowerInvariant()
}

function Get-Fingerprint([string]$file) { (Get-FileHashHex $file).Substring(0, 16) }

# returns a list of warnings; throws on anything that makes install impossible
function Test-GameDir([string]$dir) {
    $warn = New-Object System.Collections.Generic.List[string]
    $data = Join-Path $dir 'The Walking Dead Destinies_Data'
    $required = [ordered]@{
        $script:ExeName                  = 'the game executable'
        'UnityPlayer.dll'                = 'the Unity engine; Lazarus hooks in through it'
        'GameAssembly.dll'               = 'the game code (IL2CPP); Lazarus needs it to find game methods'
        'The Walking Dead Destinies_Data' = 'the game data folder'
    }
    foreach ($k in $required.Keys) {
        if (-not (Test-Path -LiteralPath (Join-Path $dir $k))) {
            Fail "'$k' is missing from $dir. That is $($required[$k]). Is this the right folder? (Verify game files in Steam if it should be.)"
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $data 'il2cpp_data\Metadata\global-metadata.dat'))) {
        $warn.Add('il2cpp metadata not found where expected; the game layout may have changed.')
    }
    if (Test-Path -LiteralPath (Join-Path $data 'Managed')) {
        $warn.Add('This build has a Managed\ folder (Mono backend). Lazarus was built for the IL2CPP build and may not work.')
    }

    $ver = (Get-Item -LiteralPath (Join-Path $dir 'UnityPlayer.dll')).VersionInfo.FileVersion
    if (-not $ver) {
        $warn.Add('Could not read the Unity version from UnityPlayer.dll.')
    } elseif (-not $ver.StartsWith($script:ExpectedUnity)) {
        $warn.Add("Unity version is $ver; Lazarus was built against $($script:ExpectedUnity). The loader may not start.")
    }

    $fp = Get-Fingerprint (Join-Path $dir 'GameAssembly.dll')
    if ($script:KnownBuilds.ContainsKey($fp)) {
        Write-Step "Game build: $($script:KnownBuilds[$fp]) [$fp]"
    } else {
        $warn.Add("Game build $fp has not been tested with Lazarus. The loader will still run, but mods pinned to a specific build will be skipped.")
    }
    return $warn
}

# ------------------------------------------------------------------- manifest

function Get-ManifestPath([string]$dir) { Join-Path $dir "Lazarus\$($script:ManifestName)" }

# The manifest decides what uninstall deletes and what it copies over
# version.dll, so a path in it has to stay inside our own footprint: exactly
# version.dll, or something under Lazarus\. no rooted paths, no drive letters,
# no ..\ climbing out. (security review #3)
function Test-SafeRelPath([string]$rel, [string]$under = $null) {
    if (-not $rel -or [System.IO.Path]::IsPathRooted($rel) -or $rel.Contains(':')) { return $false }
    foreach ($part in ($rel -split '[\\/]')) {
        if ($part -eq '..' -or $part -eq '.' -or $part -eq '') { return $false }
    }
    if ($under) { return $rel -like "$under\*" }
    return ($rel -eq 'version.dll') -or ($rel -like 'Lazarus\*')
}

function Read-Manifest([string]$dir) {
    $p = Get-ManifestPath $dir
    if (-not (Test-Path -LiteralPath $p)) { return $null }
    $m = @{ state = ''; version = ''; files = @{}; backups = @{} }
    foreach ($line in Get-Content -LiteralPath $p) {
        if ($line -match '^state=(.*)$') { $m.state = $Matches[1] }
        elseif ($line -match '^version=(.*)$') { $m.version = $Matches[1] }
        elseif ($line -match '^file=([^|]+)\|(.*)$') {
            if (-not (Test-SafeRelPath $Matches[1])) {
                Fail "The install record lists an unsafe path ('$($Matches[1])'), so it may have been tampered with. Not touching anything. Remove version.dll and the Lazarus folder by hand."
            }
            $m.files[$Matches[1]] = $Matches[2]
        }
        elseif ($line -match '^backup=([^|]+)\|(.*)$') {
            if ($Matches[1] -ne 'version.dll' -or -not (Test-SafeRelPath $Matches[2] 'Lazarus\backup')) {
                Fail "The install record lists an unsafe backup ('$($Matches[1])' -> '$($Matches[2])'). Not touching anything."
            }
            $m.backups[$Matches[1]] = $Matches[2]
        }
    }
    return $m
}

function Write-Manifest([string]$dir, $m) {
    $lines = @(
        '# Project Lazarus install record. Used by -Uninstall; do not edit.',
        "state=$($m.state)",
        "version=$($m.version)"
    )
    foreach ($k in ($m.files.Keys | Sort-Object)) { $lines += "file=$k|$($m.files[$k])" }
    foreach ($k in ($m.backups.Keys | Sort-Object)) { $lines += "backup=$k|$($m.backups[$k])" }
    $p = Get-ManifestPath $dir
    New-Item -ItemType Directory -Force -Path (Split-Path $p) | Out-Null
    # write-then-rename so a crash mid-write can't leave a half manifest
    Set-Content -LiteralPath "$p.tmp" -Value $lines -Encoding UTF8
    Move-Item -LiteralPath "$p.tmp" -Destination $p -Force
}


function Test-IsUserFile([string]$rel) {
    foreach ($pat in $script:UserFiles) { if ($rel -like $pat) { return $true } }
    return $false
}

# ------------------------------------------------------------------- actions

function Invoke-Install([string]$dir) {
    $src = if ($SourceDir) { $SourceDir } else { $PSScriptRoot }
    if (-not (Test-Path -LiteralPath (Join-Path $src 'version.dll')) -or -not (Test-Path -LiteralPath (Join-Path $src 'Lazarus'))) {
        Fail "Install files not found in $src (expected version.dll and a Lazarus folder next to this script)."
    }
    $version = '0.0.0'
    $vf = Join-Path $src 'VERSION.txt'
    if (Test-Path -LiteralPath $vf) { $version = (Get-Content -LiteralPath $vf -TotalCount 1).Trim() }

    $existing = Read-Manifest $dir
    $m = if ($existing) { $existing } else { @{ state = ''; version = ''; files = @{}; backups = @{} } }
    if ($existing -and $existing.state -eq 'installing') {
        Write-Step 'A previous install was interrupted; finishing it now.'
    }

    # what we're about to put in place, relative to the game dir
    $plan = New-Object System.Collections.Generic.List[string]
    $plan.Add('version.dll')
    $lzSrc = Join-Path $src 'Lazarus'
    Get-ChildItem -LiteralPath $lzSrc -Recurse -File | ForEach-Object {
        $plan.Add('Lazarus\' + $_.FullName.Substring($lzSrc.Length).TrimStart('\'))
    }

    # foreign version.dll (another mod/tool) -> back it up rather than clobber it
    $target = Join-Path $dir 'version.dll'
    if ((Test-Path -LiteralPath $target) -and -not $m.files.ContainsKey('version.dll')) {
        $backupRel = 'Lazarus\backup\version.dll.orig'
        $backupAbs = Join-Path $dir $backupRel
        New-Item -ItemType Directory -Force -Path (Split-Path $backupAbs) | Out-Null
        Copy-Item -LiteralPath $target -Destination $backupAbs -Force
        $m.backups['version.dll'] = $backupRel
        Write-Step "Backed up existing version.dll to $backupRel"
    }

    $m.state = 'installing'
    $m.version = $version
    Write-Manifest $dir $m

    foreach ($rel in $plan) {
        $from = Join-Path $src $rel
        $to = Join-Path $dir $rel
        if ((Test-IsUserFile $rel) -and (Test-Path -LiteralPath $to)) {
            Write-Step "Keeping your existing $rel"
            if (-not $m.files.ContainsKey($rel)) { $m.files[$rel] = 'user' }
            continue
        }
        New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
        try {
            Copy-Item -LiteralPath $from -Destination $to -Force
        } catch [System.UnauthorizedAccessException] {
            Fail "Access denied writing $to. Close the game if it is running. If the game is under Program Files, run this installer as administrator."
        } catch [System.IO.IOException] {
            Fail "Could not write $to ($($_.Exception.Message)). Close the game if it is running and try again."
        }
        $m.files[$rel] = Get-FileHashHex $to
        # record progress as we go so an interrupted install can be cleanly undone
        Write-Manifest $dir $m
    }
    foreach ($d in 'logs', 'state') { New-Item -ItemType Directory -Force -Path (Join-Path $dir "Lazarus\$d") | Out-Null }
    $m.state = 'installed'
    Write-Manifest $dir $m
}

function Invoke-Uninstall([string]$dir) {
    $m = Read-Manifest $dir
    if (-not $m) {
        if (Test-Path -LiteralPath (Join-Path $dir 'Lazarus')) {
            Fail "No install record found in $dir\Lazarus. Remove version.dll and the Lazarus folder by hand, or reinstall first."
        }
        Fail "Project Lazarus does not appear to be installed in $dir."
    }

    # only delete version.dll if it's still the one we put there
    $vd = Join-Path $dir 'version.dll'
    if ($m.files.ContainsKey('version.dll') -and (Test-Path -LiteralPath $vd)) {
        if ((Get-FileHashHex $vd) -ne $m.files['version.dll'] -and -not $Force) {
            Fail 'version.dll has been replaced since Lazarus was installed (another tool?). Not removing it. Use -Force to remove it anyway.'
        }
        Remove-Item -LiteralPath $vd -Force
        Write-Step 'Removed version.dll'
    }
    if ($m.backups.ContainsKey('version.dll')) {
        $b = Join-Path $dir $m.backups['version.dll']
        if (Test-Path -LiteralPath $b) {
            Copy-Item -LiteralPath $b -Destination $vd -Force
            Write-Step 'Restored the original version.dll'
        } else {
            Write-Warning "Backup $($m.backups['version.dll']) is missing; could not restore the previous version.dll."
        }
    }

    # Only what we put there, plus what Lazarus itself writes at runtime.
    # This used to be Remove-Item Lazarus -Recurse, which also took out any
    # mod the user had installed on their own (security review #2).
    $lz = Join-Path $dir 'Lazarus'
    foreach ($rel in @($m.files.Keys)) {
        if ($rel -eq 'version.dll') { continue }
        if ($KeepSettings -and (Test-IsUserFile $rel)) { continue }
        $p = Join-Path $dir $rel
        if (Test-Path -LiteralPath $p -PathType Leaf) { Remove-Item -LiteralPath $p -Force }
    }
    # runtime output: flat folders, so no recursive delete needed (or wanted)
    foreach ($d in $script:RuntimeDirs) {
        $p = Join-Path $lz $d
        if (Test-Path -LiteralPath $p -PathType Container) {
            Get-ChildItem -LiteralPath $p -File -Force | Remove-Item -Force
        }
    }
    # files our own mods generate next to themselves (only in folders we installed)
    $ourModDirs = @($m.files.Keys | Where-Object { $_ -like 'Lazarus\mods\*\*' } |
        ForEach-Object { Split-Path (Join-Path $dir $_) } | Select-Object -Unique)
    foreach ($md in $ourModDirs) {
        foreach ($pat in $script:GeneratedFiles) {
            Get-ChildItem -LiteralPath $md -Filter $pat -File -ErrorAction SilentlyContinue | Remove-Item -Force
        }
    }
    $mf = Get-ManifestPath $dir
    if (Test-Path -LiteralPath $mf) { Remove-Item -LiteralPath $mf -Force }

    # drop folders that ended up empty, deepest first. a folder with anything
    # left in it (someone else's mod) stays.
    if (Test-Path -LiteralPath $lz) {
        $dirs = @(Get-ChildItem -LiteralPath $lz -Directory -Recurse -Force | Sort-Object { $_.FullName.Length } -Descending)
        foreach ($d in ($dirs + (Get-Item -LiteralPath $lz))) {
            if (-not (Get-ChildItem -LiteralPath $d.FullName -Force)) { Remove-Item -LiteralPath $d.FullName -Force }
        }
    }

    if (Test-Path -LiteralPath $lz) {
        $left = @(Get-ChildItem -LiteralPath $lz -Recurse -File -Force |
            ForEach-Object { $_.FullName.Substring($lz.Length + 1) })
        if ($KeepSettings) { Write-Step 'Removed Lazarus (kept your settings files)' }
        else { Write-Step 'Removed Lazarus' }
        if ($left.Count) {
            Write-Step "Left in place, not installed by Lazarus or kept on request ($($left.Count) file(s)):"
            $left | Select-Object -First 20 | ForEach-Object { Write-Step "  Lazarus\$_" }
            if ($left.Count -gt 20) { Write-Step "  ... and $($left.Count - 20) more" }
        }
    } else {
        Write-Step 'Removed the Lazarus folder'
    }
}

function Show-Status([string]$dir) {
    $m = Read-Manifest $dir
    Write-Host "Game folder: $dir"
    if (-not $m) { Write-Host 'Project Lazarus: not installed'; return }
    Write-Host "Project Lazarus: $($m.state) (version $($m.version))"
    $vd = Join-Path $dir 'version.dll'
    if (-not (Test-Path -LiteralPath $vd)) { Write-Host '  version.dll: MISSING (loader will not start)' }
    elseif ((Get-FileHashHex $vd) -ne $m.files['version.dll']) { Write-Host '  version.dll: replaced by something else' }
    else { Write-Host '  version.dll: ok' }
    $log = Join-Path $dir 'Lazarus\logs\lazarus.log'
    if (Test-Path -LiteralPath $log) {
        Write-Host "  log: $log"
        Get-Content -LiteralPath $log -Tail 15 | ForEach-Object { Write-Host "    $_" }
    } else {
        $boot = Join-Path $dir 'Lazarus\logs\bootstrap.log'
        if (Test-Path -LiteralPath $boot) {
            Write-Host "  lazarus.log not found, but bootstrap.log exists - the loader was injected but did not start. Contents:"
            Get-Content -LiteralPath $boot | ForEach-Object { Write-Host "    $_" }
        } else {
            Write-Host '  no logs yet (start the game once)'
        }
    }
    $q = Join-Path $dir 'Lazarus\state\quarantine.txt'
    if (Test-Path -LiteralPath $q) { Write-Host "  quarantined mods listed in $q" }
}

function Confirm-Action([string]$question) {
    if ($Yes) { return $true }
    $a = Read-Host "$question [y/N]"
    return $a -match '^(y|yes)$'
}

function Main {
    try {
        $dir = Resolve-GameDir
        if ($Status) { Show-Status $dir; return 0 }
        if ($OpenLogs) {
            $logs = Join-Path $dir 'Lazarus\logs'
            if (-not (Test-Path -LiteralPath $logs)) { Fail "No logs folder at $logs yet." }
            Invoke-Item -LiteralPath $logs
            return 0
        }
        if ($Uninstall) {
            if (-not (Confirm-Action "Remove Project Lazarus from $dir ?")) { Write-Host 'Cancelled.'; return 1 }
            Invoke-Uninstall $dir
            Write-Host 'Project Lazarus has been removed. The game is back to its original state.'
            return 0
        }

        Write-Host "Game folder: $dir"
        $warnings = @(Test-GameDir $dir)
        foreach ($w in $warnings) { Write-Warning $w }
        $blocking = @($warnings | Where-Object { $_ -like 'Unity version is*' -or $_ -like '*Mono backend*' })
        if ($blocking.Count -and -not $Force) {
            Fail 'This does not look like a supported build (see warnings above). Use -Force to install anyway.'
        }
        Write-Host ''
        Write-Host 'This will add version.dll and a Lazarus folder to the game folder.'
        Write-Host 'No original game files are changed. Uninstall with: Install-Lazarus.ps1 -Uninstall'
        Write-Host 'Mods are native code. Only enable mods you trust, in Lazarus\lazarus.ini.'
        if (-not (Confirm-Action 'Install?')) { Write-Host 'Cancelled.'; return 1 }
        Invoke-Install $dir
        Write-Host ''
        Write-Host 'Installed. Start the game normally; logs go to Lazarus\logs\lazarus.log.'
        Write-Host 'Graphics settings: edit Lazarus\mods\graphics-settings\config.ini'
        Write-Host 'If the game misbehaves: hold SHIFT while it starts (safe mode) or run -Uninstall.'
        return 0
    } catch [LazarusError] {
        Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
        return 2
    }
}

# dot-sourcing (tests) just loads the functions
if ($MyInvocation.InvocationName -ne '.') { exit (Main) }
