param(
  [Parameter(Mandatory=$true)][string]$QtRoot,
  [Parameter(Mandatory=$true)][string]$MySqlHome,
  [Parameter(Mandatory=$true)][string]$MySqlPlugin
)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$env:PATH = "$QtRoot/bin;$MySqlHome/bin;$MySqlHome/lib;$env:PATH"
function Run-Step([scriptblock]$Command) {
  & $Command
  if ($LASTEXITCODE -ne 0) { throw "Build step failed: $LASTEXITCODE" }
}
if (!(Test-Path $MySqlPlugin)) { throw 'QMYSQL plugin missing; build Qt 6.10.2 QMYSQL with MSVC 2022 x64 first.' }
Run-Step { npm --prefix frontend ci --no-audit --no-fund }
Run-Step { npm --prefix frontend run build }
Run-Step { cmake -S . -B build -G Ninja "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release "-DSPEED_SYNC_MYSQL_PLUGIN=$MySqlPlugin" }
Run-Step { cmake --build build --parallel 4 }
Run-Step { & "$QtRoot/bin/windeployqt.exe" --release --no-translations build/speed-sync-sql.exe }
Copy-Item "$MySqlHome/lib/libmysql.dll" build/ -ErrorAction Stop
Run-Step { ctest --test-dir build --output-on-failure }
Run-Step { npm --prefix frontend test }
Write-Host 'Build finished. Run build/speed-sync-sql.exe on Windows 10 and record the actual runtime result.'
