param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][string]$ScratchRoot,
    [string]$ApplicationId = '{BAE3DE00-B354-46A9-BD28-99AC2A89FAE0}',
    [string]$ExeName = '',
    [string]$DisplayName = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$brand = Get-Content -LiteralPath (Join-Path $repo 'branding.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$version = (Get-Content -LiteralPath (Join-Path $repo 'VERSION') -Raw -Encoding UTF8).Trim()
if (-not $ExeName) { $ExeName = "$($brand.name)_pp.exe" }
if (-not $DisplayName) { $DisplayName = $brand.display_name }
if ($ExeName -notmatch '^[A-Za-z0-9_.-]+\.exe$' -or $ApplicationId -notmatch '^[A-Za-z0-9{}-]+$') { throw 'Invalid test identifiers.' }
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$setupInfo = (Get-Item -LiteralPath $installerPath).VersionInfo
if ($setupInfo.ProductName.Trim() -ne $DisplayName -or $setupInfo.ProductVersion.Trim() -ne $version -or
    $setupInfo.OriginalFilename.Trim() -ine (Split-Path -Leaf $installerPath)) {
    throw 'Installer product or version metadata is incorrect.'
}
$root = (Resolve-Path -LiteralPath $ScratchRoot).Path
$key = "Software\Microsoft\Windows\CurrentVersion\Uninstall\${ApplicationId}_is1"
foreach ($hive in @('HKCU:', 'HKLM:')) {
    if (Test-Path -LiteralPath (Join-Path $hive $key)) { throw "Refusing to test over an existing installation: $hive\$key" }
}
$testDir = Join-Path $root ("installer-smoke-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$install = Join-Path $testDir 'installed app'
$env:TEMP = $testDir
$env:TMP = $testDir
function Run-Checked([string]$Program, [string[]]$Arguments, [bool]$ShouldSucceed) {
    $process = Start-Process -FilePath $Program -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(180000)) {
        $live = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
        if ($live -and $live.Path -ieq $Program) { Stop-Process -Id $live.Id }
        throw 'Installer process timed out.'
    }
    if (($process.ExitCode -eq 0) -ne $ShouldSucceed) { throw "Unexpected exit code $($process.ExitCode): $Program" }
}
$installArgs = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS', "/DIR=`"$install`"", "/LOG=`"$testDir\install.log`"")
Run-Checked $installerPath $installArgs $true
$exe = Join-Path $install $ExeName
$uninstaller = Join-Path $install 'unins000.exe'
$registry = Join-Path 'HKCU:' $key
if (-not (Test-Path -LiteralPath $exe -PathType Leaf) -or -not (Test-Path -LiteralPath $uninstaller -PathType Leaf) -or
    -not (Test-Path -LiteralPath $registry)) { throw 'Install files or uninstall registration are missing.' }
$record = Get-ItemProperty -LiteralPath $registry
if ($record.DisplayVersion -ne $version -or $record.UninstallString -notlike '*unins000.exe*') { throw 'Invalid uninstall registration.' }
Set-Content -LiteralPath (Join-Path $install 'settings.ini') -Value 'test settings'
Set-Content -LiteralPath (Join-Path $install 'models/user-custom.dat') -Value 'test model'
Set-Content -LiteralPath (Join-Path $install 'models/PP-OCRv6_det_small.onnx') -Value 'modified model'
# A renamed Windows utility simulates this exact installed executable without launching the user's app.
Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/ping.exe') -Destination $exe -Force
$fixture = Start-Process -FilePath $exe -ArgumentList '-t', '127.0.0.1' -WorkingDirectory $install -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Seconds 2
    if (-not (Get-Process -Id $fixture.Id -ErrorAction SilentlyContinue)) { throw 'Process fixture exited early.' }
    Run-Checked $installerPath $installArgs $false
    Run-Checked $uninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$testDir\uninstall-running.log`"") $false
    if (-not (Get-Process -Id $fixture.Id -ErrorAction SilentlyContinue) -or -not (Test-Path -LiteralPath $uninstaller)) {
        throw 'Running application was terminated or uninstalled.'
    }
} finally {
    $live = Get-Process -Id $fixture.Id -ErrorAction SilentlyContinue
    if ($live -and $live.Path -ieq $exe) { Stop-Process -Id $live.Id; $live.WaitForExit() }
}
Run-Checked $installerPath $installArgs $true
if ((Get-Content -LiteralPath (Join-Path $install 'settings.ini') -Raw).Trim() -ne 'test settings' -or
    (Get-Content -LiteralPath (Join-Path $install 'models/PP-OCRv6_det_small.onnx') -Raw).Trim() -ne 'modified model') {
    throw 'Upgrade did not preserve user files.'
}
Run-Checked $uninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$testDir\uninstall.log`"") $true
for ($i = 0; $i -lt 60 -and (Test-Path -LiteralPath $uninstaller); $i++) { Start-Sleep -Milliseconds 250 }
if ((Test-Path -LiteralPath $exe) -or (Test-Path -LiteralPath $registry) -or
    -not (Test-Path -LiteralPath (Join-Path $install 'settings.ini')) -or
    -not (Test-Path -LiteralPath (Join-Path $install 'models/user-custom.dat')) -or
    -not (Test-Path -LiteralPath (Join-Path $install 'models/PP-OCRv6_det_small.onnx'))) {
    throw 'Uninstall did not remove the program or preserve user data.'
}
Write-Host "Installer smoke passed in $testDir"
