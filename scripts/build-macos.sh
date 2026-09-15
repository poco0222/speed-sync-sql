#!/bin/zsh
set -eu
cd "$(dirname "$0")/.."
qt_root="${SPEED_SYNC_QT_ROOT:-/Users/PopoY/Documents/DevTools/Qt/6.10.2/macos}"
qt_tools="${SPEED_SYNC_QT_TOOLS:-/Users/PopoY/Documents/DevTools/Qt/Tools}"
cmake_bin="$qt_tools/CMake/CMake.app/Contents/bin/cmake"
plugin="${SPEED_SYNC_MYSQL_PLUGIN:-$PWD/.local/mysql-driver/plugins/sqldrivers/libqsqlmysql.dylib}"
[[ -f "$plugin" ]] || { print -u2 'QMYSQL plugin missing. See README.md before building.'; exit 1; }
npm --prefix frontend ci --no-audit --no-fund
npm --prefix frontend run build
"$cmake_bin" -S . -B build -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$qt_tools/Ninja/ninja" \
  -DCMAKE_PREFIX_PATH="$qt_root" -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_BUILD_TYPE=Debug \
  -DSPEED_SYNC_MYSQL_PLUGIN="$plugin"
"$cmake_bin" --build build --parallel 4
"$qt_tools/CMake/CMake.app/Contents/bin/ctest" --test-dir build --output-on-failure
npm --prefix frontend test
