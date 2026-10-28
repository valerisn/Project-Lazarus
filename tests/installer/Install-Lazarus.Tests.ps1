# Pester 3.x tests (what ships with Windows PowerShell 5.1). Everything runs
# against throwaway fake game folders in $TestDrive - never a real install.

$installer = Join-Path $PSScriptRoot '..\..\installer\Install-Lazarus.ps1'
. $installer

$Yes = $true  # functions read these from the calling scope
$Force = $false
$KeepSettings = $false

function New-FakeGame([string]$root) {
    $data = Join-Path $root 'The Walking Dead Destinies_Data\il2cpp_data\Metadata'
    New-Item -ItemType Directory -Force -Path $data | Out-Null
    Set-Content -LiteralPath (Join-Path $root 'The Walking Dead Destinies.exe') -Value 'exe' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $root 'UnityPlayer.dll') -Value 'unity' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $root 'GameAssembly.dll') -Value 'game code' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $data 'global-metadata.dat') -Value 'meta' -Encoding ASCII
    return $root
}

function New-FakeSource([string]$root) {
    $gfx = Join-Path $root 'Lazarus\mods\graphics-settings'
    New-Item -ItemType Directory -Force -Path $gfx | Out-Null
    Set-Content -LiteralPath (Join-Path $root 'version.dll') -Value 'lazarus proxy' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $root 'VERSION.txt') -Value '9.9.9' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $root 'Lazarus\lazarus.ini') -Value '[loader]' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $gfx 'mod.ini') -Value '[mod]' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $gfx 'config.ini') -Value 'default' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $gfx 'graphics_settings.dll') -Value 'mod' -Encoding ASCII
    return $root
}

# path -> hash for every file, so we can prove nothing original changed
function Get-Snapshot([string]$root) {
    $h = @{}
    Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {
        $h[$_.FullName.Substring($root.Length)] = Get-FileHashHex $_.FullName
    }
    return $h
}

function Assert-SameSnapshot($a, $b) {
    ($a.Keys | Sort-Object) -join ',' | Should Be (($b.Keys | Sort-Object) -join ',')
    foreach ($k in $a.Keys) { $b[$k] | Should Be $a[$k] }
}

$n = 0
function New-Case {
    $script:n++
    $base = Join-Path $TestDrive "case$script:n"
    return @{ Game = New-FakeGame (Join-Path $base 'game'); Src = New-FakeSource (Join-Path $base 'src'); Base = $base }
}

Describe 'Test-GameDir' {
    It 'names the exact missing file' {
        $c = New-Case
        Remove-Item (Join-Path $c.Game 'GameAssembly.dll')
        { Test-GameDir $c.Game } | Should Throw "'GameAssembly.dll' is missing"
    }
    It 'warns about an untested build but does not block' {
        $c = New-Case
        $w = @(Test-GameDir $c.Game)
        ($w -join "`n") | Should Match 'has not been tested'
    }
    It 'flags a Mono build' {
        $c = New-Case
        New-Item -ItemType Directory -Path (Join-Path $c.Game 'The Walking Dead Destinies_Data\Managed') | Out-Null
        ((@(Test-GameDir $c.Game)) -join "`n") | Should Match 'Mono backend'
    }
}

Describe 'Install and uninstall' {
    It 'installs only new files and leaves game files untouched' {
        $c = New-Case
        $before = Get-Snapshot $c.Game
        $SourceDir = $c.Src
        Invoke-Install $c.Game
        Test-Path (Join-Path $c.Game 'version.dll') | Should Be $true
        Test-Path (Join-Path $c.Game 'Lazarus\mods\graphics-settings\graphics_settings.dll') | Should Be $true
        Test-Path (Join-Path $c.Game 'Lazarus\logs') | Should Be $true
        $m = Read-Manifest $c.Game
        $m.state | Should Be 'installed'
        $m.version | Should Be '9.9.9'
        $after = Get-Snapshot $c.Game
        foreach ($k in $before.Keys) { $after[$k] | Should Be $before[$k] }
    }

    It 'uninstall restores the folder exactly' {
        $c = New-Case
        $before = Get-Snapshot $c.Game
        $SourceDir = $c.Src
        Invoke-Install $c.Game
        Invoke-Uninstall $c.Game
        Assert-SameSnapshot $before (Get-Snapshot $c.Game)
        Test-Path (Join-Path $c.Game 'Lazarus') | Should Be $false
    }

    It 'backs up a foreign version.dll and restores it' {
        $c = New-Case
        Set-Content -LiteralPath (Join-Path $c.Game 'version.dll') -Value 'someone elses dll' -Encoding ASCII
        $before = Get-Snapshot $c.Game
        $SourceDir = $c.Src
        Invoke-Install $c.Game
        (Get-Content (Join-Path $c.Game 'version.dll')) | Should Be 'lazarus proxy'
        Test-Path (Join-Path $c.Game 'Lazarus\backup\version.dll.orig') | Should Be $true
        Invoke-Uninstall $c.Game
        Assert-SameSnapshot $before (Get-Snapshot $c.Game)
    }

    It 'reinstall keeps user-edited settings' {
        $c = New-Case
        $SourceDir = $c.Src
        Invoke-Install $c.Game
        Set-Content -LiteralPath (Join-Path $c.Game 'Lazarus\lazarus.ini') -Value 'mine' -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $c.Game 'Lazarus\mods\graphics-settings\config.ini') -Value 'my fps' -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $c.Src 'Lazarus\mods\graphics-settings\graphics_settings.dll') -Value 'mod v2' -Encoding ASCII
        Invoke-Install $c.Game
        (Get-Content (Join-Path $c.Game 'Lazarus\lazarus.ini')) | Should Be 'mine'
        (Get-Content (Join-Path $c.Game 'Lazarus\mods\graphics-settings\config.ini')) | Should Be 'my fps'
        (Get-Content (Join-Path $c.Game 'Lazarus\mods\graphics-settings\graphics_settings.dll')) | Should Be 'mod v2'
    }

    It 'refuses to delete a version.dll it did not install unless forced' {
        $c = New-Case
        $SourceDir = $c.Src
        Invoke-Install $c.Game
        Set-Content -LiteralPath (Join-Path $c.Game 'version.dll') -Value 'replaced later' -Encoding ASCII
        { Invoke-Uninstall $c.Game } | Should Throw 'has been replaced'
        Test-Path (Join-Path $c.Game 'version.dll') | Should Be $true
        $Force = $true
        Invoke-Uninstall $c.Game
        Test-Path (Join-Path $c.Game 'version.dll') | Should Be $false
    }

    It 'KeepSettings keeps only the settings files' {
        $c = New-Case
        $SourceDir = $c.Src
        Invoke-Install $c.Game
        $KeepSettings = $true
        Invoke-Uninstall $c.Game
        Test-Path (Join-Path $c.Game 'Lazarus\lazarus.ini') | Should Be $true
        Test-Path (Join-Path $c.Game 'Lazarus\mods\graphics-settings\config.ini') | Should Be $true
        Test-Path (Join-Path $c.Game 'Lazarus\mods\graphics-settings\graphics_settings.dll') | Should Be $false
        Test-Path (Join-Path $c.Game 'version.dll') | Should Be $false
    }

    It 'keeps mods the user added themselves (security review #2)' {
        $c = New-Case
        $SourceDir = $c.Src
        Invoke-Install $c.Game
        $theirs = Join-Path $c.Game 'Lazarus\mods\someones-mod'
        New-Item -ItemType Directory -Force -Path $theirs | Out-Null
        Set-Content -LiteralPath (Join-Path $theirs 'mod.ini') -Value '[mod]' -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $theirs 'cool.dll') -Value 'theirs' -Encoding ASCII
        # runtime output that should go: logs, crash state, our mod's generated dump
        Set-Content -LiteralPath (Join-Path $c.Game 'Lazarus\logs\lazarus.log') -Value 'log' -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $c.Game 'Lazarus\state\quarantine.txt') -Value 'q' -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $c.Game 'Lazarus\mods\graphics-settings\actions-Player.json') -Value '{}' -Encoding ASCII
        Invoke-Uninstall $c.Game
        Test-Path (Join-Path $theirs 'cool.dll') | Should Be $true
        Test-Path (Join-Path $theirs 'mod.ini') | Should Be $true
        Test-Path (Join-Path $c.Game 'Lazarus\mods\graphics-settings') | Should Be $false
        Test-Path (Join-Path $c.Game 'Lazarus\logs') | Should Be $false
        Test-Path (Join-Path $c.Game 'Lazarus\state') | Should Be $false
        Test-Path (Get-ManifestPath $c.Game) | Should Be $false
        Test-Path (Join-Path $c.Game 'version.dll') | Should Be $false
    }

    It 'uninstall on a clean folder explains itself' {
        $c = New-Case
        { Invoke-Uninstall $c.Game } | Should Throw 'does not appear to be installed'
    }

    It 'missing install files is a clear error' {
        $c = New-Case
        Remove-Item (Join-Path $c.Src 'version.dll')
        $SourceDir = $c.Src
        { Invoke-Install $c.Game } | Should Throw 'Install files not found'
    }
}

Describe 'Manifest path validation (security review #3)' {
    It 'accepts our own paths' {
        Test-SafeRelPath 'version.dll' | Should Be $true
        Test-SafeRelPath 'Lazarus\mods\x\x.dll' | Should Be $true
        Test-SafeRelPath 'Lazarus\backup\version.dll.orig' 'Lazarus\backup' | Should Be $true
    }
    It 'rejects anything outside the footprint' {
        foreach ($bad in @('..\evil.dll', 'Lazarus\..\..\Windows\x', 'C:\Windows\x.dll', '\\server\share\x',
                           'GameAssembly.dll', 'Lazarus', 'Lazarus\.\x', 'Lazarus\\x', 'D:x.dll', '')) {
            Test-SafeRelPath $bad | Should Be $false
        }
        Test-SafeRelPath 'Lazarus\mods\x.dll' 'Lazarus\backup' | Should Be $false
    }
    It 'refuses to uninstall from a tampered manifest and deletes nothing' {
        $c = New-Case
        $SourceDir = $c.Src
        Invoke-Install $c.Game
        $victim = Join-Path $c.Base 'victim.txt'
        Set-Content -LiteralPath $victim -Value 'keep me' -Encoding ASCII
        Add-Content -LiteralPath (Get-ManifestPath $c.Game) -Value 'file=Lazarus\..\..\victim.txt|x'
        { Invoke-Uninstall $c.Game } | Should Throw 'unsafe path'
        Test-Path $victim | Should Be $true
        Test-Path (Join-Path $c.Game 'version.dll') | Should Be $true
    }
    It 'refuses a backup path outside Lazarus\backup' {
        $c = New-Case
        $SourceDir = $c.Src
        Invoke-Install $c.Game
        Add-Content -LiteralPath (Get-ManifestPath $c.Game) -Value 'backup=version.dll|GameAssembly.dll'
        { Invoke-Uninstall $c.Game } | Should Throw 'unsafe backup'
    }
}

Describe 'Interrupted install' {
    It 'can be uninstalled cleanly and reinstalled' {
        $c = New-Case
        $before = Get-Snapshot $c.Game
        # what a crash after copying version.dll looks like
        Copy-Item (Join-Path $c.Src 'version.dll') (Join-Path $c.Game 'version.dll')
        Write-Manifest $c.Game @{ state = 'installing'; version = '9.9.9'
            files = @{ 'version.dll' = (Get-FileHashHex (Join-Path $c.Game 'version.dll')) }; backups = @{} }
        Invoke-Uninstall $c.Game
        Assert-SameSnapshot $before (Get-Snapshot $c.Game)

        $SourceDir = $c.Src
        Copy-Item (Join-Path $c.Src 'version.dll') (Join-Path $c.Game 'version.dll')
        Write-Manifest $c.Game @{ state = 'installing'; version = '9.9.9'
            files = @{ 'version.dll' = (Get-FileHashHex (Join-Path $c.Game 'version.dll')) }; backups = @{} }
        Invoke-Install $c.Game
        (Read-Manifest $c.Game).state | Should Be 'installed'
        # must not have "backed up" our own half-installed proxy as if it were foreign
        Test-Path (Join-Path $c.Game 'Lazarus\backup') | Should Be $false
    }
}

Describe 'Steam discovery' {
    It 'finds the game through libraryfolders.vdf and appmanifest' {
        $steam = Join-Path $TestDrive 'Steam'
        $lib2 = Join-Path $TestDrive 'OtherDrive\SteamLibrary'
        New-Item -ItemType Directory -Force -Path (Join-Path $steam 'steamapps') | Out-Null
        $vdf = "`"libraryfolders`"`n{`n  `"0`"`n  {`n    `"path`"  `"$($steam -replace '\\','\\')`"`n  }`n  `"1`"`n  {`n    `"path`"  `"$($lib2 -replace '\\','\\')`"`n  }`n}`n"
        Set-Content -LiteralPath (Join-Path $steam 'steamapps\libraryfolders.vdf') -Value $vdf
        $game = New-FakeGame (Join-Path $lib2 'steamapps\common\The Walking Dead Destinies')
        Set-Content -LiteralPath (Join-Path $lib2 'steamapps\appmanifest_2192790.acf') -Value "`"AppState`"`n{`n  `"installdir`"  `"The Walking Dead Destinies`"`n}`n"

        $SteamRoot = $steam
        $found = @(Find-InSteamLibraries)
        $found.Count | Should Be 1
        $found[0] | Should Be $game
    }

    It 'finds the game in common\ even without an appmanifest' {
        $steam = Join-Path $TestDrive 'Steam2'
        $game = New-FakeGame (Join-Path $steam 'steamapps\common\TWD-renamed')
        $SteamRoot = $steam
        @(Find-InSteamLibraries)[0] | Should Be $game
    }

    It 'returns nothing when Steam has no install' {
        $steam = Join-Path $TestDrive 'Steam3'
        New-Item -ItemType Directory -Force -Path (Join-Path $steam 'steamapps\common') | Out-Null
        $SteamRoot = $steam
        @(Find-InSteamLibraries).Count | Should Be 0
    }
}
