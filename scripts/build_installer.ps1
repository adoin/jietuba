param(
    [Parameter(Mandatory)][string]$SourceDir,
    [Parameter(Mandatory)][string]$OutputDir,
    [Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Arch,
    [string]$Compiler = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$brand = Get-Content -LiteralPath (Join-Path $repo 'branding.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$version = (Get-Content -LiteralPath (Join-Path $repo 'VERSION') -Raw -Encoding UTF8).Trim()
if ($brand.name -cnotmatch '^[a-z][a-z0-9]*$' -or $brand.display_name -cnotmatch '^[A-Za-z][A-Za-z0-9 _-]*$' -or
    $brand.display_name_zh -cnotmatch '^[\p{L}\p{N} _-]+$' -or
    $brand.repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $version -cnotmatch '^\d+\.\d+\.\d+$') {
    throw 'Invalid branding.json or VERSION.'
}
$source = (Resolve-Path -LiteralPath $SourceDir).Path
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw 'SourceDir must be a directory.' }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDir).Path
$exeName = "$($brand.name)_pp.exe"
$exe = Join-Path $source $exeName
foreach ($relative in @($exeName, 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'models/PP-OCRv6_det_small.onnx', 'models/PP-OCRv6_rec_small.onnx')) {
    $item = Get-Item -LiteralPath (Join-Path $source $relative) -ErrorAction Stop
    if ($item.PSIsContainer -or $item.Length -eq 0 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Invalid release file: $relative" }
}
$licenseDir = Get-Item -LiteralPath (Join-Path $source 'licenses') -ErrorAction Stop
if (-not $licenseDir.PSIsContainer -or ($licenseDir.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
    -not (Get-ChildItem -LiteralPath $licenseDir.FullName -File -Recurse | Select-Object -First 1)) { throw 'Release licenses are missing.' }
$exeInfo = (Get-Item -LiteralPath $exe).VersionInfo
if ($exeInfo.ProductVersion -ne $version -or $exeInfo.OriginalFilename -ine $exeName -or $exeInfo.ProductName -ne $brand.display_name) {
    throw 'Executable version, product name or original filename does not match branding.json and VERSION.'
}
$stream = [IO.File]::OpenRead($exe)
try {
    $reader = [IO.BinaryReader]::new($stream)
    if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'Invalid PE file.' }
    $stream.Position = 0x3c
    $offset = $reader.ReadUInt32()
    if ($offset -gt $stream.Length - 6) { throw 'Invalid PE offset.' }
    $stream.Position = $offset
    if ($reader.ReadUInt32() -ne 0x00004550) { throw 'Invalid PE signature.' }
    $machine = $reader.ReadUInt16()
    $expected = if ($Arch -eq 'arm64') { 0xaa64 } else { 0x8664 }
    if ($machine -ne $expected) { throw "Executable architecture does not match $Arch." }
} finally { $stream.Dispose() }
if (-not $Compiler) {
    $candidate = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($candidate) { $Compiler = $candidate.Source }
    else { $Compiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
}
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Inno Setup compiler (ISCC.exe) is missing.' }
$owner = $brand.repository.Split('/')[0]
$arguments = @((Join-Path $repo 'installer/windows.iss'), "/DAppName=$($brand.display_name)", "/DAppNameZh=$($brand.display_name_zh)", "/DAppPrefix=$($brand.name)",
    "/DAppVersion=$version", "/DAppRepository=$($brand.repository)", "/DAppPublisher=$owner", "/DSourceDir=$source", "/DOutputDir=$output", "/DTargetArch=$Arch")
& $Compiler @arguments
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE." }
$result = Join-Path $output "$($brand.name)_setup-release-$version-$Arch.exe"
if (-not (Test-Path -LiteralPath $result -PathType Leaf)) { throw 'Installer output was not created.' }
Write-Host "Installer: $result"
Write-Host "SHA256: $((Get-FileHash -LiteralPath $result -Algorithm SHA256).Hash.ToLowerInvariant())"
