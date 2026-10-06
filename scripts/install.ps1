<#
.SYNOPSIS
  Bootstrap `vb`, the Voxel Browser developer CLI (Windows).

.DESCRIPTION
  irm https://github.com/VoxelBrowser/voxel_browser/releases/latest/download/install.ps1 | iex

  Downloads the newest vb, verifies its SHA-256 against the release's
  release.toml, puts it in <data>\bin, adds that directory to your *user* PATH
  (no administrator rights, nothing system-wide), then runs `vb install`.

  Environment: VB_HOME, VB_REPO (owner/repo), VB_BASE_URL, VB_RELEASE_DIR (a local
  directory with release.toml + zips), VB_NO_MODIFY_PATH=1, VB_SKIP_INSTALL=1.
#>
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is far faster without it

function Get-Setting($name, $default) {
  $v = [Environment]::GetEnvironmentVariable($name)
  if ([string]::IsNullOrEmpty($v)) { return $default }
  return $v
}

$repo    = Get-Setting 'VB_REPO' 'VoxelBrowser/voxel_browser'
$baseUrl = Get-Setting 'VB_BASE_URL' 'https://github.com'
$relDir  = Get-Setting 'VB_RELEASE_DIR' ''

$arch = $env:PROCESSOR_ARCHITECTURE
switch ($arch) {
  'AMD64' { $platform = 'windows-x86_64' }
  'ARM64' { $platform = 'windows-arm64' }
  default { throw "no published builds for Windows on $arch" }
}

$data = Get-Setting 'VB_HOME' (Join-Path $env:LOCALAPPDATA 'voxel_browser')
$bin  = Join-Path $data 'bin'

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("vb-install-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
  function Fetch($name, $dest) {
    if ($relDir -ne '') {
      $src = Join-Path $relDir $name
      if (-not (Test-Path $src)) { throw "$src not found" }
      Copy-Item $src $dest
    } else {
      Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/$repo/releases/latest/download/$name" -OutFile $dest
    }
  }

  Write-Host 'Looking up the latest release...'
  $manifest = Join-Path $tmp 'release.toml'
  Fetch 'release.toml' $manifest

  # release.toml is flat `key = "value"` lines grouped under [[artifact]].
  $file = $null; $sha = $null
  $kind = $plat = $build = $f = $s = ''
  $check = {
    if ($kind -eq 'cli' -and $plat -eq $platform -and $build -eq 'release' -and $f) { $script:file = $f; $script:sha = $s }
  }
  foreach ($line in Get-Content $manifest) {
    if ($line -match '^\[\[artifact\]\]') {
      & $check
      $kind = $plat = $build = $f = $s = ''
    } elseif ($line -match '^\s*([a-z_0-9]+)\s*=\s*"?([^"]*)"?\s*$') {
      switch ($Matches[1]) {
        'kind'     { $kind = $Matches[2] }
        'platform' { $plat = $Matches[2] }
        'build'    { $build = $Matches[2] }
        'file'     { $f = $Matches[2] }
        'sha256'   { $s = $Matches[2] }
      }
    }
  }
  & $check
  if (-not $file) { throw "the latest release has no vb download for $platform" }
  if ($file -match '[\\/]') { throw 'unexpected archive name in release.toml' }
  if (-not $sha) { throw "release.toml lists no checksum for $file" }

  Write-Host "Downloading $file..."
  $zip = Join-Path $tmp $file
  Fetch $file $zip
  $got = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
  if ($got -ne $sha.ToLowerInvariant()) {
    throw "SHA-256 mismatch for $file (expected $sha, got $got); not installing"
  }

  $extract = Join-Path $tmp 'extract'
  Expand-Archive -Path $zip -DestinationPath $extract
  $exe = Join-Path $extract 'vb.exe'
  if (-not (Test-Path $exe)) { throw 'the archive does not contain vb.exe' }
  & $exe --version *> $null
  if ($LASTEXITCODE -ne 0) { throw 'the downloaded vb does not run on this machine' }

  New-Item -ItemType Directory -Force -Path $bin | Out-Null
  $target = Join-Path $bin 'vb.exe'
  if (Test-Path $target) {
    # A running vb.exe can be renamed but not overwritten (see `vb self update`).
    Move-Item -Force $target "$target.old"
  }
  Move-Item -Force $exe $target
  Write-Host "Installed vb to $target"

  $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
  $onPath = ($userPath -split ';') -contains $bin
  if (-not $onPath) {
    if ((Get-Setting 'VB_NO_MODIFY_PATH' '0') -eq '1') {
      Write-Host "Add this to your PATH:  $bin"
    } else {
      $new = if ([string]::IsNullOrEmpty($userPath)) { $bin } else { "$bin;$userPath" }
      [Environment]::SetEnvironmentVariable('Path', $new, 'User')
      $env:Path = "$bin;$env:Path"
      Write-Host "Added $bin to your user PATH (open a new terminal to pick it up)"
    }
  }

  if ((Get-Setting 'VB_SKIP_INSTALL' '0') -ne '1') {
    Write-Host 'Installing the game...'
    $src = Get-Setting 'VB_SOURCE' ''
    if ($src -eq '') {
      if ($relDir -ne '') { $src = "dir:$relDir" }
      elseif ($baseUrl -ne 'https://github.com') { $src = "$repo@$baseUrl" }
      elseif ($repo -ne 'VoxelBrowser/voxel_browser') { $src = $repo }
    }
    $env:VB_SOURCE = $src
    if ($data -ne (Join-Path $env:LOCALAPPDATA 'voxel_browser')) { $env:VB_HOME = $data }
    & $target install latest
    if ($LASTEXITCODE -ne 0) { throw 'vb install failed (retry with: vb install)' }
    & $target shim install *> $null
  }
  Write-Host 'Done. Try:  vb host     (run a server)    vb launch     (start the game)'
}
finally {
  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
