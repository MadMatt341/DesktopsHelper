$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$expected = (Get-Content vendor/adapter.sha256).Trim()
if ((Get-FileHash vendor/VirtualDesktopAccessor.dll).Hash -ne $expected) { throw 'Adapter SHA256 mismatch' }
New-Item -ItemType Directory -Force build/release | Out-Null
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode -mwindows -s src/main.c src/service.c src/input.c src/diagnostics.c -o build/release/DesktopsHelper.exe -luser32 -lgdi32 -lshell32 -lole32 -luuid
if ($LASTEXITCODE) { throw 'Build failed' }
Copy-Item vendor/VirtualDesktopAccessor.dll build/release/
Copy-Item vendor/LICENSE.VirtualDesktopAccessor.txt build/release/
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/integration.c -o build/release/integration.exe -luser32 -lole32
if ($LASTEXITCODE) { throw 'Test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/shortcuts.c -o build/release/shortcuts.exe -luser32
if ($LASTEXITCODE) { throw 'Shortcut test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/visibility.c -o build/release/visibility.exe -ldwmapi -luser32
if ($LASTEXITCODE) { throw 'Visibility test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/health.c -o build/release/health.exe -luser32
if ($LASTEXITCODE) { throw 'Health test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/topology.c -o build/release/topology.exe -luser32
if ($LASTEXITCODE) { throw 'Topology test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/input-policy.c -o build/release/input-policy.exe -luser32
if ($LASTEXITCODE) { throw 'Input test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/lifecycle.c -o build/release/lifecycle.exe -luser32 -lole32
if ($LASTEXITCODE) { throw 'Lifecycle test build failed' }
Write-Output 'Built build/release/DesktopsHelper.exe'
