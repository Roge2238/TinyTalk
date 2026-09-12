#!/usr/bin/env bash
# ============================================================
# 一键构建(门禁脚本,见 DESIGN.md M0)
# 用法: ./build.sh
# 输出: build/tinytalkd
# ============================================================
set -e
cd "$(dirname "$0")"

cmake -S . -B build >/dev/null
cmake --build build

echo "构建成功: build/tinytalkd"
