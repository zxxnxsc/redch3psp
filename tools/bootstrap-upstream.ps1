$ErrorActionPreference = 'Stop'

$Upstream = 'https://github.com/SugaryHull/re3.git'
$UpstreamCommit = '31dacfe8edb01ca2aae3069e7c777c4849cf5adc'
$Root = Split-Path -Parent $PSScriptRoot
$Work = Join-Path $Root 'work\re3'
$Patcher = Join-Path $Root 'tools\apply_reconstruction.py'

if (Test-Path $Work) {
    Write-Host "Removing old $Work"
    Remove-Item -Recurse -Force $Work
}

New-Item -ItemType Directory -Force (Split-Path -Parent $Work) | Out-Null
Write-Host '[redch3psp] cloning pinned re3 upstream'
git clone --quiet $Upstream $Work
Push-Location $Work
try {
    git checkout --quiet $UpstreamCommit
} finally {
    Pop-Location
}

Write-Host '[redch3psp] applying deterministic reconstruction edits'
if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 $Patcher $Work
} elseif (Get-Command python3 -ErrorAction SilentlyContinue) {
    & python3 $Patcher $Work
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    & python $Patcher $Work
} else {
    throw 'Python 3 is required to run tools/apply_reconstruction.py'
}

if ($LASTEXITCODE -ne 0) {
    throw "Source reconstruction failed with exit code $LASTEXITCODE"
}

Write-Host "[redch3psp] reconstruction tree ready: $Work"
Write-Host "[redch3psp] upstream commit: $UpstreamCommit"
