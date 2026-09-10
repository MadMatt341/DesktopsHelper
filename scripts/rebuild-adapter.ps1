param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$archiveHash = '6DB729B361C07C718E198390EF0AB1BD7D51E2AA810F30D19B3F3E2BDD022058'
if ((Get-FileHash vendor/adapter-source.zip).Hash -ne $archiveHash) { throw 'Source archive SHA256 mismatch' }
Expand-Archive vendor/adapter-source.zip build/adapter-source -Force
$source = 'build/adapter-source/Ciantic-VirtualDesktopAccessor-8172097'
& $Python scripts/patch-adapter.py $source
if ($LASTEXITCODE) { throw 'Adapter patch failed' }
$previousRustFlags = $env:RUSTFLAGS
try {
    $env:RUSTFLAGS = '-C target-feature=+crt-static'
    cargo build --release --locked --target x86_64-pc-windows-msvc --manifest-path "$source/dll/Cargo.toml"
    if ($LASTEXITCODE) { throw 'Adapter build failed' }
} finally { $env:RUSTFLAGS = $previousRustFlags }
Copy-Item "$source/target/x86_64-pc-windows-msvc/release/VirtualDesktopAccessor.dll" vendor/VirtualDesktopAccessor.dll
(Get-FileHash vendor/VirtualDesktopAccessor.dll).Hash | Set-Content vendor/adapter.sha256
Write-Output 'Adapter rebuilt. Review its hash, rebuild the helper, and rerun compatibility and performance tests.'
