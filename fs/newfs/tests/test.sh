#!/bin/bash

# Resolve paths from this script, not the caller's working directory.
TEST_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
PROJECT_DIR="$(dirname -- "$TEST_DIR")"

if [[ $# -gt 0 ]]; then
    case "$1" in
        checkpoint|vector|all) SUITE="$1" ;;
        *) echo "用法：bash tests/test.sh [checkpoint|vector|all]" >&2; exit 2 ;;
    esac
    if [[ $# -ne 1 ]]; then
        echo "只需一个测试类型，不需要指定设备或二进制路径。" >&2
        exit 2
    fi
    echo "[准备 1/2] 配置并编译当前文件系统"
    if ! cmake -S "$PROJECT_DIR" -B "$PROJECT_DIR/build" -DCMAKE_BUILD_TYPE=Debug ||
       ! cmake --build "$PROJECT_DIR/build" -j"$(nproc)"; then
        echo "fail: 编译失败，尚未开始功能测试。" >&2
        exit 1
    fi
    echo "pass: 编译完成"
    echo "[准备 2/2] 启动 $SUITE 测评"
    if ! command -v python3 >/dev/null; then
        echo "fail: 缺少 python3，请在实验环境中安装。" >&2
        exit 1
    fi
    exec python3 "$PROJECT_DIR/bonus/tests/run_bonus.py" --suite "$SUITE"
fi

cd "$TEST_DIR" || exit 1

# cd ../../../tests/test_ddriver || exit
# out=$(./compile_and_run.sh)
# if ! echo "$out" | grep "Test Pass :)"; then
#     echo "-- Output:"
#     echo "$out"
#     echo "请先通过实验一 (确保能够理解ddriver)"
#     echo "Score: 0"
#     exit 1
# fi 
# cd - || exit

read -r -p "请输入测试方式[N(基础功能测试) / E(进阶功能测试) / S(分阶段测试)]: " TEST_METHOD

# 编译src
cd ..; mkdir build >/dev/null 2>&1; cd build

cmake .. >/dev/null 2>&1; make >/dev/null 2>&1;
if [ $? -eq 0 ]; then
    echo "" >/dev/null
else
    echo "Test Fail : 编译失败"
    exit 1
fi
cd ../tests

rm mnt -rf
mkdir mnt 2>/dev/null 

if [[ "${TEST_METHOD}" == "E" ]]; then
    ./main.sh "6"
elif [[ "${TEST_METHOD}" == "N" ]]; then
    ./main.sh "4"
else
    echo "----测试阶段1：mount测试"
    echo "----测试阶段2：增加 mkdir 和 touch 测试"
    echo "----测试阶段3：增加 ls 测试"
    echo "----测试阶段4：增加 umount 及 remount 测试"
    echo "----测试阶段5：增加 read 及 write 测试"
    echo "----测试阶段6：增加 copy 测试"
    read -r -p "按照你的进度输入测试等级[数字1-6]: " LEVEL 
    if [[ "${LEVEL}" -ge "1" ]] && [[ "${LEVEL}" -le "6" ]]; then
        ./main.sh "${LEVEL}"
    else
        echo "!! Wrong Test Level! Please input 1 to 6 !!"
    fi
fi
