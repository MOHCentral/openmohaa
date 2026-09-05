# OpenMoHAA updater — called by openmohaa.exe (the wrapper).
# Official source: https://github.com/openmoh/openmohaa/releases
#
# The file the user starts is openmohaa.exe (wrapper). The real engine is
# openmohaa_game.exe. Official zips also contain openmohaa.exe; that file
# must be copied onto openmohaa_game.exe, NEVER onto the wrapper.
#
# Architecture is chosen by the user (never auto-picked):
#   first run: "Architectuur? [1] x86  [2] x64"
#   later: saved arch= in launcher_state
#   override: -x86 / -x64 (saved)

[CmdletBinding()]
param(
    [switch]$SkipLaunch,
    [switch]$CheckOnly,
    [switch]$Force,
    [switch]$x86,
    [switch]$x64
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$GameDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$GameExe = Join-Path $GameDir 'openmohaa_game.exe'
$WrapperExe = Join-Path $GameDir 'openmohaa.exe'
$ApiUrl = if ($env:OPENMOHAA_API_URL) { $env:OPENMOHAA_API_URL } else { 'https://api.github.com/repos/openmoh/openmohaa/releases/latest' }
$UserAgent = 'OpenMoHAA-Launcher/1.0'
$StateDir = Join-Path $env:APPDATA 'openmohaa'
$StateFile = Join-Path $StateDir 'launcher_state.txt'
$LocalStateFile = Join-Path $GameDir 'launcher_state.txt'
$StagingRoot = Join-Path $env:TEMP 'openmohaa_official_update'
$ApiTimeoutSec = 12
$DownloadTimeoutSec = 90
$ApiCacheHours = 6
# Same idea as engine PR_MAX_DOWNLOAD_BYTES: refuse huge GitHub zips.
$MaxZipBytes = 400MB

$ProtectedNames = @(
    'openmohaa.exe'
    'launch_openmohaa.cmd'
    'launch_openmohaa.ps1'
    'openmohaa_wrapper.c'
    '_build_openmohaa_wrapper.bat'
    'launcher_state.txt'
)

function Write-Info([string]$Message) { Write-Host $Message -ForegroundColor Cyan }
function Write-Ok([string]$Message) { Write-Host $Message -ForegroundColor Green }
function Write-WarnLine([string]$Message) { Write-Host $Message -ForegroundColor Yellow }
function Write-ErrLine([string]$Message) { Write-Host $Message -ForegroundColor Red }

function Get-Full([string]$Path) {
    [System.IO.Path]::GetFullPath($Path)
}

function Test-TimeoutError($ErrorRecord) {
    $ex = $ErrorRecord.Exception
    while ($ex) {
        if ($ex -is [TimeoutException]) { return $true }
        if ($ex -is [System.Threading.Tasks.TaskCanceledException]) { return $true }
        if ($ex.Message -match '(?i)timeout|timed out|canceled|cancelled') { return $true }
        $ex = $ex.InnerException
    }
    return $false
}

function Get-GitHubApiRelease([string]$Url, [int]$TimeoutSec, [string]$ETag) {
    Add-Type -AssemblyName System.Net.Http -ErrorAction SilentlyContinue
    $client = New-Object System.Net.Http.HttpClient
    $req = $null
    $resp = $null
    try {
        $client.Timeout = [TimeSpan]::FromSeconds($TimeoutSec)
        $null = $client.DefaultRequestHeaders.UserAgent.TryParseAdd($UserAgent)
        $null = $client.DefaultRequestHeaders.Accept.TryParseAdd('application/vnd.github+json')
        $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, $Url)
        if ($ETag) {
            [void]$req.Headers.TryAddWithoutValidation('If-None-Match', $ETag)
        }
        $resp = $client.SendAsync($req).GetAwaiter().GetResult()
        $outEtag = $null
        if ($resp.Headers.ETag) {
            $outEtag = $resp.Headers.ETag.ToString()
        } elseif ($resp.Headers.Contains('ETag')) {
            $outEtag = @($resp.Headers.GetValues('ETag'))[0]
        }
        if ([int]$resp.StatusCode -eq 304) {
            return @{ NotModified = $true; Release = $null; ETag = $(if ($outEtag) { $outEtag } else { $ETag }) }
        }
        [void]$resp.EnsureSuccessStatusCode()
        $json = $resp.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        $release = $json | ConvertFrom-Json
        if (-not $release -or -not $release.tag_name) {
            throw 'No release tag.'
        }
        return @{ NotModified = $false; Release = $release; ETag = $outEtag }
    } catch [System.Threading.Tasks.TaskCanceledException] {
        throw [TimeoutException]'Timeout.'
    } finally {
        if ($resp) { $resp.Dispose() }
        if ($req) { $req.Dispose() }
        $client.Dispose()
    }
}

function Save-HttpFile([string]$Url, [string]$OutFile, [int]$TimeoutSec) {
    Add-Type -AssemblyName System.Net.Http -ErrorAction SilentlyContinue
    $client = New-Object System.Net.Http.HttpClient
    $resp = $null
    $inStream = $null
    $outStream = $null
    try {
        $client.Timeout = [TimeSpan]::FromSeconds($TimeoutSec)
        $null = $client.DefaultRequestHeaders.UserAgent.TryParseAdd($UserAgent)
        $resp = $client.GetAsync($Url, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        [void]$resp.EnsureSuccessStatusCode()
        $contentLength = $resp.Content.Headers.ContentLength
        if ($contentLength -and $contentLength -gt $MaxZipBytes) {
            throw "Zip too large ($contentLength bytes, max $MaxZipBytes)."
        }
        $inStream = $resp.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $outStream = [System.IO.File]::Create($OutFile)
        $buffer = New-Object byte[] 81920
        $total = [int64]0
        while (($read = $inStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $total += $read
            if ($total -gt $MaxZipBytes) {
                throw 'Zip too large (max 400 MB).'
            }
            $outStream.Write($buffer, 0, $read)
        }
        $outStream.Flush()
    } catch [System.Threading.Tasks.TaskCanceledException] {
        throw [TimeoutException]'Timeout.'
    } finally {
        if ($outStream) { $outStream.Dispose() }
        if ($inStream) { $inStream.Dispose() }
        if ($resp) { $resp.Dispose() }
        $client.Dispose()
    }
    if (-not (Test-Path -LiteralPath $OutFile) -or (Get-Item -LiteralPath $OutFile).Length -lt 1) {
        throw 'Empty download.'
    }
}

function Get-AssetSha256($Asset) {
    if (-not $Asset) { return $null }
    $digest = [string]$Asset.digest
    if ($digest -match '^sha256:([0-9a-fA-F]{64})$') {
        return $Matches[1].ToUpperInvariant()
    }
    return $null
}

function Copy-FileReplace([string]$Source, [string]$Dest) {
    $destDir = Split-Path -Parent $Dest
    if (-not (Test-Path -LiteralPath $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }

    $newPath = "$Dest.new"
    $bakPath = "$Dest.bak"
    if (Test-Path -LiteralPath $newPath) { Remove-Item -LiteralPath $newPath -Force }
    Copy-Item -LiteralPath $Source -Destination $newPath -Force
    if ((Get-Item -LiteralPath $newPath).Length -lt 1) {
        Remove-Item -LiteralPath $newPath -Force -ErrorAction SilentlyContinue
        throw "Empty copy: $Dest"
    }

    $hadOld = Test-Path -LiteralPath $Dest
    if ($hadOld) {
        if (Test-Path -LiteralPath $bakPath) { Remove-Item -LiteralPath $bakPath -Force }
        Move-Item -LiteralPath $Dest -Destination $bakPath -Force
    }

    try {
        Move-Item -LiteralPath $newPath -Destination $Dest -Force
    } catch {
        if ($hadOld -and (Test-Path -LiteralPath $bakPath) -and -not (Test-Path -LiteralPath $Dest)) {
            Move-Item -LiteralPath $bakPath -Destination $Dest -Force
        }
        throw
    }

    if (Test-Path -LiteralPath $bakPath) {
        Remove-Item -LiteralPath $bakPath -Force -ErrorAction SilentlyContinue
    }
}

function Get-PeArch([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $fs = [System.IO.File]::OpenRead($Path)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Seek(0x3C, 'Begin') | Out-Null
        $peOffset = $br.ReadInt32()
        $fs.Seek([int64]$peOffset + 4, 'Begin') | Out-Null
        switch ($br.ReadUInt16()) {
            0x014C { 'x86' }
            0x8664 { 'x64' }
            0xAA64 { 'arm64' }
            default { 'unknown' }
        }
    } finally {
        $fs.Dispose()
    }
}

function Get-LocalVersion([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }

    $info = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    foreach ($candidate in @($info.ProductVersion, $info.FileVersion)) {
        if ($candidate -and $candidate.Trim().Length -gt 0) { return $candidate.Trim() }
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    $m = [regex]::Match($text, 'OpenMoHAA (\d+\.\d+\.\d+\S*)')
    if ($m.Success) { return $m.Groups[1].Value.Trim() }
    $m = [regex]::Match($text, '(\d+\.\d+\.\d+(?:-[A-Za-z0-9.+]+)?)')
    if ($m.Success) { return $m.Groups[1].Value.Trim() }
    return $null
}

function Get-VersionParts([string]$Text) {
    if (-not $Text) { return $null }
    $m = [regex]::Match($Text, '(\d+)\.(\d+)(?:\.(\d+))?')
    if (-not $m.Success) { return $null }
    $patch = 0
    if ($m.Groups[3].Success -and $m.Groups[3].Value) { $patch = [int]$m.Groups[3].Value }
    ,@([int]$m.Groups[1].Value, [int]$m.Groups[2].Value, $patch)
}

function Compare-VersionParts($Left, $Right) {
    for ($i = 0; $i -lt 3; $i++) {
        if ($Left[$i] -gt $Right[$i]) { return 1 }
        if ($Left[$i] -lt $Right[$i]) { return -1 }
    }
    return 0
}

function Read-StateFile([string]$Path) {
    $state = @{
        Tag          = $null
        LastCheckUtc = $null
        ETag         = $null
        Arch         = $null
        LocalVersion = $null
    }
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return $state }
    foreach ($line in Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue) {
        if ($line -match '^last_seen_tag=(.+)$') { $state.Tag = $Matches[1].Trim() }
        elseif ($line -match '^last_check_utc=(.+)$') { $state.LastCheckUtc = $Matches[1].Trim() }
        elseif ($line -match '^etag=(.+)$') { $state.ETag = $Matches[1].Trim() }
        elseif ($line -match '^local_version=(.+)$') { $state.LocalVersion = $Matches[1].Trim() }
        elseif ($line -match '^arch=(.+)$') {
            $a = $Matches[1].Trim().ToLowerInvariant()
            if ($a -eq 'x86' -or $a -eq 'x64') { $state.Arch = $a }
        }
    }
    return $state
}

function Get-LauncherState {
    $state = Read-StateFile $StateFile
    $local = Read-StateFile $LocalStateFile
    if ($local.Arch) { $state.Arch = $local.Arch }
    if (-not $state.Tag -and $local.Tag) { $state.Tag = $local.Tag }
    if (-not $state.ETag -and $local.ETag) { $state.ETag = $local.ETag }
    if (-not $state.LastCheckUtc -and $local.LastCheckUtc) { $state.LastCheckUtc = $local.LastCheckUtc }
    if (-not $state.LocalVersion -and $local.LocalVersion) { $state.LocalVersion = $local.LocalVersion }
    return $state
}

function Write-StateFile([string]$Path, $State) {
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $lines = @()
    if ($State.Tag) { $lines += "last_seen_tag=$($State.Tag)" }
    if ($State.LocalVersion) { $lines += "local_version=$($State.LocalVersion)" }
    if ($State.LastCheckUtc) { $lines += "last_check_utc=$($State.LastCheckUtc)" }
    if ($State.ETag) { $lines += "etag=$($State.ETag)" }
    if ($State.Arch) { $lines += "arch=$($State.Arch)" }
    $lines | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Save-LauncherState {
    param(
        [string]$Tag,
        [string]$LocalVersion,
        [string]$ETag,
        [string]$Arch,
        [switch]$TouchedApi
    )
    $prev = Get-LauncherState
    $state = @{
        Tag          = $(if ($Tag) { $Tag } else { $prev.Tag })
        LocalVersion = $(if ($LocalVersion) { $LocalVersion } else { $prev.LocalVersion })
        LastCheckUtc = $prev.LastCheckUtc
        ETag         = $(if ($PSBoundParameters.ContainsKey('ETag')) { $ETag } else { $prev.ETag })
        Arch         = $(if ($Arch) { $Arch } else { $prev.Arch })
    }
    if ($TouchedApi) {
        $state.LastCheckUtc = (Get-Date).ToUniversalTime().ToString('o')
    }
    $appOk = $false
    try {
        Write-StateFile -Path $StateFile -State $state
        $appOk = $true
    } catch {
        $appOk = $false
    }
    if ((Test-Path -LiteralPath $LocalStateFile) -or -not $appOk) {
        Write-StateFile -Path $LocalStateFile -State $state
    }
}

function Test-RecentApiCheck($State) {
    if (-not $State.LastCheckUtc) { return $false }
    try {
        $last = [datetime]::Parse($State.LastCheckUtc, $null, [System.Globalization.DateTimeStyles]::RoundtripKind)
        if ($last.Kind -eq [System.DateTimeKind]::Unspecified) {
            $last = [datetime]::SpecifyKind($last, 'Utc')
        }
        $age = [datetime]::UtcNow - $last.ToUniversalTime()
        return ($age.TotalHours -ge 0 -and $age.TotalHours -lt $ApiCacheHours)
    } catch {
        return $false
    }
}

function Read-ArchitectureChoice {
    Write-Host 'Architectuur? [1] x86  [2] x64'
    try {
        if ([Console]::IsInputRedirected) {
            return $null
        }
    } catch {
        return $null
    }
    while ($true) {
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq 'D1' -or $key.Key -eq 'NumPad1' -or $key.KeyChar -eq '1' -or $key.Key -eq 'Enter') {
            Write-Host 'x86'
            return 'x86'
        }
        if ($key.Key -eq 'D2' -or $key.Key -eq 'NumPad2' -or $key.KeyChar -eq '2') {
            Write-Host 'x64'
            return 'x64'
        }
    }
}

function Resolve-Architecture([string]$SavedArch) {
    if ($x86 -and $x64) {
        Write-WarnLine 'Both -x86 and -x64 given; using x86.'
        return 'x86'
    }
    if ($x86) { return 'x86' }
    if ($x64) { return 'x64' }
    if ($SavedArch -eq 'x86' -or $SavedArch -eq 'x64') { return $SavedArch }
    return Read-ArchitectureChoice
}

function Get-WindowsAssetForArch($Release, [string]$Arch) {
    $zips = @($Release.assets | Where-Object {
        $_.name -match '^openmohaa-.*-windows-.+\.zip$' -and
        $_.name -notmatch '-pdb\.zip$'
    })

    $asset = $zips | Where-Object { $_.name -match "windows-$Arch\.zip$" } | Select-Object -First 1
    if ($asset) {
        return @{ Asset = $asset; Arch = $Arch; Note = $null }
    }

    return @{
        Asset = $null
        Arch  = $Arch
        Note  = "No official Windows $Arch zip."
    }
}

function Test-GameRunning {
    @(Get-Process -Name 'openmohaa_game', 'omohaaded' -ErrorAction SilentlyContinue)
}

function Get-InstallDestination([string]$Rel, [string]$Name) {
    if ($Name -ieq 'openmohaa.exe') {
        return $GameExe
    }
    return (Join-Path $GameDir $Rel)
}

function Install-OfficialZip($Asset) {
    if (Test-Path -LiteralPath $StagingRoot) {
        Remove-Item -LiteralPath $StagingRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $StagingRoot -Force | Out-Null

    $zipPath = Join-Path $StagingRoot $Asset.name
    $extractDir = Join-Path $StagingRoot 'extract'
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null

    $expectedSha = Get-AssetSha256 $Asset
    if (-not $expectedSha) {
        throw 'No SHA256 digest on the GitHub asset; update refused (fail-closed).'
    }

    Save-HttpFile -Url $Asset.browser_download_url -OutFile $zipPath -TimeoutSec $DownloadTimeoutSec

    $actualSha = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actualSha -ne $expectedSha) {
        Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
        throw "SHA256 mismatch (got $actualSha, expected $expectedSha)."
    }
    Write-Ok "SHA256 ok ($actualSha)."

    Write-Host 'Extracting...'
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir -Force

    $foundExe = Get-ChildItem -LiteralPath $extractDir -Recurse -Filter 'openmohaa.exe' | Select-Object -First 1
    if (-not $foundExe) {
        throw 'The official zip does not contain openmohaa.exe.'
    }
    $payloadRoot = $foundExe.Directory.FullName
    $wrapperFull = Get-Full $WrapperExe

    $copied = 0
    Get-ChildItem -LiteralPath $payloadRoot -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($payloadRoot.Length).TrimStart('\')
        if ($_.Extension -notin @('.exe', '.dll')) { return }
        if ($_.Name -match '\.pdb$') { return }
        if ($_.Name -ieq 'openmohaa.exe') {
            # keep going — remapped to openmohaa_game.exe
        } elseif ($_.Name -in $ProtectedNames) {
            return
        }

        $dest = Get-InstallDestination -Rel $rel -Name $_.Name
        $destFull = Get-Full $dest
        if ($destFull -ieq $wrapperFull) {
            Write-WarnLine 'Wrapper openmohaa.exe was not overwritten.'
            return
        }

        Copy-FileReplace -Source $_.FullName -Destination $dest
        $copied++
    }

    if ($copied -lt 1) {
        throw 'No .exe/.dll files copied from the official zip.'
    }
    if (-not (Test-Path -LiteralPath $GameExe)) {
        throw 'openmohaa_game.exe is missing after install.'
    }
    if (-not (Test-Path -LiteralPath $WrapperExe)) {
        throw 'Wrapper openmohaa.exe disappeared — this must not happen.'
    }

    Write-Ok "Done ($copied files)."
}

function Start-OpenMohaa {
    if (-not (Test-Path -LiteralPath $GameExe)) {
        Write-ErrLine 'openmohaa_game.exe is missing.'
        exit 1
    }

    $wrongMain = Join-Path $GameDir 'main\openmohaa_game.exe'
    if ((Get-Full $GameExe) -eq (Get-Full $wrongMain)) {
        Write-ErrLine 'Refusing to start from main\ — use the game folder.'
        exit 1
    }

    Write-Host 'Starting...'
    Start-Process -FilePath $GameExe -WorkingDirectory $GameDir
}

$localVersion = Get-LocalVersion $GameExe
$localArch = Get-PeArch $GameExe
$state = Get-LauncherState
$arch = Resolve-Architecture $state.Arch
if ($arch) {
    if ($arch -ne $state.Arch) {
        Save-LauncherState -Arch $arch
        $state.Arch = $arch
    }
} else {
    Write-ErrLine 'No architecture chosen.'
}

$running = Test-GameRunning
$release = $null
$needUpdate = $false
$updateReason = $null
$tag = $null
$apiEtag = $state.ETag
$cliOverride = [bool]($x86 -or $x64)
$wrongArch = [bool]($localArch -and $arch -and ($localArch -ne $arch))
$skipCache = $Force -or $cliOverride -or $wrongArch

if (-not $arch) {
    Write-Host 'No update.'
} elseif (-not $skipCache -and (Test-RecentApiCheck $state)) {
    Write-Host "Check skipped (cache < $ApiCacheHours h)."
    if ($CheckOnly) {
        Write-Host "Official (cached): $($state.Tag)"
        Write-Host "Arch: $arch"
        Write-Host "Local: $localVersion"
        Write-Host 'No update.'
    }
} else {
    try {
        $api = Get-GitHubApiRelease -Url $ApiUrl -TimeoutSec $ApiTimeoutSec -ETag $apiEtag
        if ($api.NotModified) {
            Write-Host 'GitHub: unchanged (304).'
            $tag = $state.Tag
            if ($CheckOnly) {
                Write-Host "Official: $tag"
                Write-Host "Arch: $arch"
                Write-Host "Local: $localVersion"
            }
            Write-Host 'No update.'
            Save-LauncherState -Tag $tag -LocalVersion $localVersion -ETag $api.ETag -Arch $arch -TouchedApi
        } else {
            $release = $api.Release
            $tag = [string]$release.tag_name
            $choice = Get-WindowsAssetForArch $release $arch

            if ($CheckOnly) {
                Write-Host "Official: $tag"
                Write-Host "Arch: $arch"
                if ($choice.Asset) {
                    Write-Host "Asset: $($choice.Asset.name)"
                    $digestSha = Get-AssetSha256 $choice.Asset
                    if ($digestSha) {
                        Write-Host "Asset SHA256: $digestSha"
                    } else {
                        Write-Host 'Asset SHA256: missing (fail-closed on a real update)'
                    }
                    if ($choice.Asset.size) {
                        Write-Host "Asset size: $($choice.Asset.size)"
                    }
                }
                Write-Host "Local: $localVersion"
            }

            if (-not $choice.Asset) {
                Write-ErrLine $(if ($choice.Note) { $choice.Note } else { "No official Windows $arch zip." })
                Save-LauncherState -Tag $tag -LocalVersion $localVersion -ETag $api.ETag -Arch $arch -TouchedApi
            } else {
                $officialParts = Get-VersionParts $tag
                $localParts = Get-VersionParts $localVersion
                $exeMissing = -not (Test-Path -LiteralPath $GameExe)

                if ($exeMissing) {
                    $needUpdate = $true
                    $updateReason = 'openmohaa_game.exe is missing'
                } elseif ($wrongArch) {
                    $needUpdate = $true
                    $updateReason = "wrong architecture ($localArch instead of $arch)"
                } elseif ($officialParts -and $localParts) {
                    if ((Compare-VersionParts $officialParts $localParts) -gt 0) {
                        $needUpdate = $true
                        $updateReason = "official $tag is newer than $localVersion"
                    }
                } elseif ($state.Tag -ne $tag) {
                    $needUpdate = $true
                    $updateReason = "local version unreadable; official is $tag"
                }

                if (-not $needUpdate) {
                    Write-Host 'No update.'
                    Save-LauncherState -Tag $tag -LocalVersion $localVersion -ETag $api.ETag -Arch $arch -TouchedApi
                } elseif ($CheckOnly) {
                    Write-Host "Update available ($updateReason)."
                } elseif ($running.Count -gt 0) {
                    Write-WarnLine 'Game already running.'
                    $needUpdate = $false
                } else {
                    Write-Host 'Downloading update...'
                    Install-OfficialZip $choice.Asset
                    $localVersion = Get-LocalVersion $GameExe
                    $localArch = Get-PeArch $GameExe
                    Save-LauncherState -Tag $tag -LocalVersion $localVersion -ETag $api.ETag -Arch $arch -TouchedApi
                    Write-Ok "Installed: $tag"
                }
            }
        }
    } catch {
        if (Test-TimeoutError $_) {
            Write-Host 'Timeout.'
        } else {
            Write-Host 'Check failed.'
            if ($_.Exception.Message) {
                Write-WarnLine $_.Exception.Message
            }
        }
    }
}

if ($CheckOnly) {
    exit 0
}

if (-not $SkipLaunch) {
    $running = Test-GameRunning
    if ($running.Count -gt 0) {
        Write-WarnLine 'Game already running.'
        exit 0
    }

    if (-not (Test-Path -LiteralPath $GameExe)) {
        Write-ErrLine 'openmohaa_game.exe is missing.'
        exit 1
    }

    Write-Host ''
    Start-OpenMohaa
}

exit 0
