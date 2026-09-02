$ErrorActionPreference = 'Stop'

$Upstream = 'https://github.com/SugaryHull/re3.git'
$UpstreamCommit = '31dacfe8edb01ca2aae3069e7c777c4849cf5adc'
$Root = Split-Path -Parent $PSScriptRoot
$Work = Join-Path $Root 'work\re3'

if (Test-Path $Work) {
    Write-Host "Removing old $Work"
    Remove-Item -Recurse -Force $Work
}

New-Item -ItemType Directory -Force (Split-Path -Parent $Work) | Out-Null
Write-Host 'Cloning pinned re3 upstream...'
git clone $Upstream $Work
Push-Location $Work
try {
    git checkout $UpstreamCommit

    $patches = Get-ChildItem (Join-Path $Root 'patches\*.patch') | Sort-Object Name
    foreach ($patch in $patches) {
        Write-Host "Checking $($patch.Name)..."
        git apply --check $patch.FullName
        Write-Host "Applying $($patch.Name)..."
        git apply $patch.FullName
    }

    Write-Host ''
    Write-Host 'Upstream reconstruction tree is ready:'
    Write-Host $Work
    Write-Host "Pinned commit: $UpstreamCommit"
} finally {
    Pop-Location
}
