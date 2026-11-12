#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 提交前本地自检 —— 一条命令跑完 CI 会做的四件事：
#   1. 配置包 JSON Schema 校验
#   2. 以 CI 同款参数配置（-DPROTOSIM_WERROR=ON，警告当错误）
#   3. 编译
#   4. ctest 全部测试 + ProtoSim --selftest
#
# 目的：把红灯挡在本地，别让 CI 当第一道关。
#
# 用法：
#   tools/check.sh                 # 全跑
#   tools/check.sh --fast          # 跳过 configure（改了 CMakeLists 才需要重配）
#   tools/check.sh --no-tests      # 只编译
#
# 环境变量：
#   PROTOSIM_QT      Qt 前缀，默认 D:/programming/Qt/6.6.3/msvc2019_64
#   PROTOSIM_BUILD   构建目录，默认 <repo>/build
#   PROTOSIM_CONFIG  Release（默认）/ Debug

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT_PREFIX="${PROTOSIM_QT:-D:/programming/Qt/6.6.3/msvc2019_64}"
BUILD_DIR="${PROTOSIM_BUILD:-$REPO_ROOT/build}"
CONFIG="${PROTOSIM_CONFIG:-Release}"
GENERATOR="Visual Studio 17 2022"

DO_CONFIGURE=1
DO_TESTS=1
for arg in "$@"; do
    case "$arg" in
        --fast) DO_CONFIGURE=0 ;;
        --no-tests) DO_TESTS=0 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "未知参数：$arg（用 --help 看用法）"; exit 2 ;;
    esac
done

# MSBuild 在 HTTPS_PROXY 与 https_proxy 并存时会直接崩（Windows 上变量名不区分大小写），
# 这里统一清掉小写那份，免得每次手动 env -u。
unset https_proxy http_proxy all_proxy FTP_PROXY ftp_proxy 2>/dev/null || true

step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }
ok()   { printf '\033[32m✓ %s\033[0m\n' "$1"; }
die()  { printf '\033[31m✗ %s\033[0m\n' "$1"; exit 1; }

cd "$REPO_ROOT" || die "进不了仓库根目录"

# ---------------------------------------------------------------- 1. 配置包

step "配置包 Schema 校验"
if command -v python >/dev/null 2>&1; then
    python tools/validate_config.py --quiet || die "配置包校验失败"
    ok "配置包通过 jsonschema 校验"
else
    printf '跳过：找不到 python\n'
fi

# ---------------------------------------------------------------- 2. 配置

if [ "$DO_CONFIGURE" -eq 1 ]; then
    step "CMake 配置（WERROR=ON，与 CI 一致）"
    cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" -A x64 \
        -DPROTOSIM_WERROR=ON \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        || die "CMake 配置失败（Qt 前缀：$QT_PREFIX）"
    ok "配置完成"
else
    step "跳过 CMake 配置（--fast）"
fi

# ---------------------------------------------------------------- 3. 编译

step "编译（$CONFIG）"
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel || die "编译失败（先看上面的第一条 error）"
ok "编译通过，无 warning-as-error"

if [ "$DO_TESTS" -eq 0 ]; then
    printf '\n\033[32m全部通过（已跳过测试）\033[0m\n'
    exit 0
fi

# ---------------------------------------------------------------- 4. 测试

# ctest 跑的是动态链接的 exe，需要能找 Qt 的 DLL。
# 注意：Git Bash / MSYS 的 PATH 只认 POSIX 形式（/d/xxx），
# 写成 D:/xxx 会让 DLL 找不到、测试全报 0xc0000139（入口点找不到），所以这里转一次。
QT_BIN="$QT_PREFIX/bin"
if command -v cygpath >/dev/null 2>&1; then
    QT_BIN="$(cygpath -u "$QT_PREFIX/bin")"
fi
export PATH="$QT_BIN:$PATH"

step "ctest"
ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure || die "ctest 有失败用例"
ok "ctest 全绿"

step "ProtoSim --selftest"
"$BUILD_DIR/bin/ProtoSim.exe" --selftest || die "自检未通过"
ok "自检 9 项全过"

printf '\n\033[32m\033[1m可以提交了。\033[0m\n'
