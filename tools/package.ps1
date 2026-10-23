[CmdletBinding()]
param(
    # Package label (the git tag for a release). Default: `git describe --tags --always`.
    [string]$Version,
    # Skip configure/build/test (the caller, for example CI, already ran them).
    [switch]$NoBuild,
    # Optional folder of extra art merged into the payload as assets\... (scope masks live
    # outside the source repository). Without it the mod uses its built-in circle window.
    [string]$AssetsLocal,
    # Label written into BUILD-IDENTITY.txt. The launcher shows anything other than
    # PUBLIC_RELEASE as a local candidate.
    [ValidateSet('RELEASE', 'PUBLIC_RELEASE')][string]$Kind = 'RELEASE',
    # Where the zips and SHA256SUMS.txt land. Default: out\packages\<Version>.
    [string]$OutDir,
    # Release notes to ship; default is generated from the git log since the previous tag.
    [string]$NotesFile,
    # Package with uncommitted changes (the manifest records it).
    [switch]$AllowDirty
)

# Packages one build of MjolnirVR from the `release` preset:
#   MjolnirVR-<v>-Build.zip   HaloMCCVRLauncher.exe + ModFiles\ (+ INSTALL-MANIFEST.sha256): extract, run the
#                             launcher, click Install. The launcher verifies every payload file against the manifest.
#   MjolnirVR-<v>-Manual.zip  Halo_MCC_VR\ with the mod and the launcher inside: copy into the MCC folder by hand.
#   MjolnirVR-<v>-Source.zip  the exact source commit (git archive).
#   SHA256SUMS.txt            SHA-256 of the three zips, the launcher and the DLL.
# It never installs anything and never starts MCC. Works in Windows PowerShell 5.1 and PowerShell 7.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Invoke-Tool([scriptblock]$Block) {
    # Native tools write progress to stderr; only the exit code decides.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Block } finally { $ErrorActionPreference = $saved }
}

function New-ZipFromTree([string]$SourceDirectory, [string]$ArchivePath, [string]$Prefix, [string[]]$ExtraFiles) {
    # Entries live under $Prefix/ (for example Halo_MCC_VR/). Forward slashes, no reparse points.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $ArchivePath) { Remove-Item -LiteralPath $ArchivePath -Force }
    $root = [IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\') + '\'
    $archive = [IO.Compression.ZipFile]::Open($ArchivePath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in @(Get-ChildItem -LiteralPath $SourceDirectory -File -Recurse | Sort-Object FullName)) {
            if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point in payload: $($file.FullName)" }
            $relative = $file.FullName.Substring($root.Length).Replace('\', '/')
            $entry = $archive.CreateEntry("$Prefix/$relative", [IO.Compression.CompressionLevel]::Optimal)
            $in = [IO.File]::OpenRead($file.FullName); $out = $entry.Open()
            try { $in.CopyTo($out) } finally { $out.Dispose(); $in.Dispose() }
        }
        foreach ($extra in @($ExtraFiles)) {
            if (-not $extra) { continue }
            $entry = $archive.CreateEntry("$Prefix/$([IO.Path]::GetFileName($extra))", [IO.Compression.CompressionLevel]::Optimal)
            $in = [IO.File]::OpenRead($extra); $out = $entry.Open()
            try { $in.CopyTo($out) } finally { $out.Dispose(); $in.Dispose() }
        }
    } finally { $archive.Dispose() }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    $preset = 'release'
    $buildDir = Join-Path $repoRoot 'out\build\release'

    $status = @(& git status --porcelain --untracked-files=no)
    $dirty = $status.Count -gt 0
    if ($dirty -and -not $AllowDirty) {
        throw ("Refusing to package a dirty worktree (use -AllowDirty to override):`n" + ($status -join "`n"))
    }
    $commit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') { throw 'Could not resolve the source commit.' }
    if (-not $Version) { $Version = (& git describe --tags --always).Trim() }
    if ($Version -notmatch '^[0-9A-Za-z][0-9A-Za-z._-]*$') { throw "Unsafe version label: $Version" }
    if (-not $OutDir) { $OutDir = Join-Path $repoRoot "out\packages\$Version" }
    $stageDir = Join-Path $repoRoot "out\package-stage\$Version"
    foreach ($dir in @($OutDir, $stageDir)) {
        if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }

    # --- build + test ---------------------------------------------------
    if (-not $NoBuild) {
        Invoke-Tool { & cmake --preset $preset }
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for preset $preset." }
        Invoke-Tool { & cmake --build --preset $preset --parallel }
        if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
        Invoke-Tool { & ctest --test-dir $buildDir -C Release --output-on-failure }
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed; not packaging.' }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'Release\HaloMCCVR.dll'))) {
        throw "No release build found under $buildDir (run without -NoBuild)."
    }

    # --- stage ----------------------------------------------------------
    $payloadDir = Join-Path $stageDir 'ModFiles'
    Invoke-Tool { & cmake --install $buildDir --config Release --prefix $payloadDir --component dist }
    if ($LASTEXITCODE -ne 0) { throw 'Payload staging failed.' }
    Invoke-Tool { & cmake --install $buildDir --config Release --prefix $stageDir --component launcher }
    if ($LASTEXITCODE -ne 0) { throw 'Launcher staging failed.' }

    if ($AssetsLocal) {
        if (-not (Test-Path -LiteralPath $AssetsLocal -PathType Container)) { throw "Assets folder not found: $AssetsLocal" }
        $assetsDest = Join-Path $payloadDir 'assets'
        New-Item -ItemType Directory -Force -Path $assetsDest | Out-Null
        $assetsRoot = [IO.Path]::GetFullPath($AssetsLocal).TrimEnd('\') + '\'
        $copied = 0
        foreach ($file in @(Get-ChildItem -LiteralPath $AssetsLocal -File -Recurse)) {
            $relative = $file.FullName.Substring($assetsRoot.Length)
            if ($relative -like '.git\*' -or $relative -like 'cockpits\*' -or $relative -ieq 'README.md') { continue }
            $target = Join-Path $assetsDest $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $target -Force
            $copied++
        }
        Write-Host "Assets merged: $copied files from $AssetsLocal"
    } else {
        Write-Warning 'No -AssetsLocal: the package ships without per-gun scope masks (the mod draws a circle window).'
    }

    $configGenerator = Join-Path $buildDir 'Release\halomccvr-config-defaults.exe'
    $configPath = Join-Path $payloadDir 'halomccvr.cfg'
    if (-not (Test-Path -LiteralPath $configGenerator -PathType Leaf)) { throw "Default-config generator is missing: $configGenerator" }
    Invoke-Tool { & $configGenerator $configPath }
    if ($LASTEXITCODE -ne 0) { throw 'Default config generation failed.' }

    $dllPath = Join-Path $payloadDir 'HaloMCCVR.dll'
    $launcherPath = Join-Path $stageDir 'HaloMCCVRLauncher.exe'
    foreach ($required in @($dllPath, $launcherPath, $configPath,
            (Join-Path $payloadDir 'LICENSE'),
            (Join-Path $payloadDir 'MANUAL-README.txt'),
            (Join-Path $payloadDir 'THIRD-PARTY-LICENSES.txt'),
            (Join-Path $payloadDir 'assets\fonts\Oxanium.ttf'))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Package is missing: $required" }
    }
    if (Test-Path -LiteralPath (Join-Path $payloadDir 'HaloMCCVRLauncher.exe')) { throw 'The payload must not contain the launcher.' }
    $dlssPresent = (Test-Path -LiteralPath (Join-Path $payloadDir 'nvngx_dlss.dll')) -and
        (Test-Path -LiteralPath (Join-Path $payloadDir 'licenses\NVIDIA-DLSS\LICENSE.txt'))
    if (-not $dlssPresent) { Write-Warning 'DLSS runtime or licence not in the payload; the package ships without DLSS.' }

    # --- package-level documents ---------------------------------------
    if ($NotesFile) {
        $notes = [IO.File]::ReadAllText($NotesFile)
    } else {
        $prev = (& git describe --tags --abbrev=0 --exclude "$Version" 2>$null)
        $range = if ($LASTEXITCODE -eq 0 -and $prev) { "$prev..HEAD" } else { '-60' }
        $log = @(& git log --no-merges --format='- %s' $range)
        $notes = "# MjolnirVR $Version`r`n`r`nSource commit: $commit`r`n`r`n## Changes`r`n`r`n" + ($log -join "`r`n") + "`r`n"
    }
    $utf8 = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText((Join-Path $stageDir 'RELEASE-NOTES.md'), $notes, $utf8)
    [IO.File]::WriteAllText((Join-Path $payloadDir 'RELEASE-NOTES.md'), $notes, $utf8)
    Copy-Item -LiteralPath (Join-Path $payloadDir 'MANUAL-README.txt') -Destination (Join-Path $stageDir 'README.txt')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $stageDir 'LICENSE')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'installer\THIRD-PARTY-LICENSES.txt') -Destination (Join-Path $stageDir 'THIRD-PARTY-LICENSES.txt')
    [IO.File]::WriteAllLines((Join-Path $payloadDir 'BUILD-IDENTITY.txt'),
        @("source_commit=$commit", "build_kind=$Kind", "release_tag=$Version"), $utf8)

    # --- install manifest (what the launcher verifies) ------------------
    $payloadPrefix = [IO.Path]::GetFullPath($payloadDir).TrimEnd('\') + '\'
    $installHashes = @()
    $files = [ordered]@{}
    foreach ($item in @(Get-ChildItem -LiteralPath $payloadDir -File -Recurse | Sort-Object FullName)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
                -not $item.FullName.StartsWith($payloadPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe payload path: $($item.FullName)"
        }
        $relative = $item.FullName.Substring($payloadPrefix.Length).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        $installHashes += "$hash  $relative"
        $files[$relative] = [ordered]@{ bytes = $item.Length; sha256 = $hash }
    }
    [IO.File]::WriteAllLines((Join-Path $payloadDir 'INSTALL-MANIFEST.sha256'), $installHashes, $utf8)

    $installerTests = Join-Path $buildDir 'Release\halomccvr_installer_tests.exe'
    if (Test-Path -LiteralPath $installerTests -PathType Leaf) {
        Invoke-Tool { & $installerTests --verify-payload $payloadDir }
        if ($LASTEXITCODE -ne 0) { throw 'Synthetic package install verification failed.' }
    } else {
        Write-Warning 'halomccvr_installer_tests.exe not built; payload verification skipped.'
    }

    # --- archives -------------------------------------------------------
    $zipBuild = Join-Path $OutDir "MjolnirVR-$Version-Build.zip"
    $zipManual = Join-Path $OutDir "MjolnirVR-$Version-Manual.zip"
    $zipSource = Join-Path $OutDir "MjolnirVR-$Version-Source.zip"
    Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $zipBuild -Force
    New-ZipFromTree $payloadDir $zipManual 'Halo_MCC_VR' @($launcherPath)
    Invoke-Tool { & git -c core.autocrlf=false archive --format=zip --prefix=MjolnirVR/ "--output=$zipSource" $commit }
    if ($LASTEXITCODE -ne 0) { throw 'Source archive failed.' }

    $launcherHash = (Get-FileHash -LiteralPath $launcherPath -Algorithm SHA256).Hash
    $dllHash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash
    $sums = @()
    foreach ($z in @($zipBuild, $zipManual, $zipSource)) {
        $sums += "$((Get-FileHash -LiteralPath $z -Algorithm SHA256).Hash)  $([IO.Path]::GetFileName($z))"
    }
    $sums += "$launcherHash  HaloMCCVRLauncher.exe"
    $sums += "$dllHash  ModFiles/HaloMCCVR.dll"
    [IO.File]::WriteAllLines((Join-Path $OutDir 'SHA256SUMS.txt'), $sums, $utf8)
    Copy-Item -LiteralPath $launcherPath -Destination $OutDir
    Copy-Item -LiteralPath $dllPath -Destination $OutDir

    $manifest = [ordered]@{
        schema_version = 2
        version = $Version
        kind = $Kind
        source_commit = $commit
        dirty_worktree = $dirty
        created_utc = [DateTime]::UtcNow.ToString('o')
        dlss_runtime_included = $dlssPresent
        scope_masks_included = [bool]$AssetsLocal
        launcher = [ordered]@{ path = 'HaloMCCVRLauncher.exe'; sha256 = $launcherHash }
        dll = [ordered]@{ path = 'ModFiles/HaloMCCVR.dll'; sha256 = $dllHash }
        files = $files
    }
    [IO.File]::WriteAllText((Join-Path $OutDir 'manifest.json'),
        ($manifest | ConvertTo-Json -Depth 6) + [Environment]::NewLine, $utf8)

    Write-Host "Packaged $Version ($Kind) from $commit into $OutDir"
    Get-Content -LiteralPath (Join-Path $OutDir 'SHA256SUMS.txt') | ForEach-Object { Write-Host "  $_" }
} finally {
    Pop-Location
}
