#!/bin/sh
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cmake_bin="$project_root/.native-tools/cmake/data/bin/cmake"
if [ ! -x "$cmake_bin" ]; then
	cmake_bin=$(command -v cmake) || {
		printf '%s\n' '未找到 CMake，请按 README.md 安装构建依赖。' >&2
		exit 1
	}
fi
cd "$project_root"
"$cmake_bin" --preset release
"$cmake_bin" --build --preset release --parallel
exec "$project_root/build/release/bin/nasti-pcb-designer" "$@"
