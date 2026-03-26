#!/bin/bash

# 1. 创建一个 LLDB 指令文件，告诉它如何处理信号和报错
cat <<EOF > .lldb_cmds
process handle SIGSEGV --stop true --pass true --notify true
process handle SIGBUS  --stop true --pass true --notify true
process handle SIGILL  --stop true --pass true --notify true
process handle SIGFPE  --stop true --pass true --notify true
process handle SIGABRT --stop true --pass true --notify true
process handle SIGTERM --stop true --pass true --notify true

run

bt
quit
EOF

COUNT=1
BINARY="build/macosx/arm64/releasedbg/chromatic-test"

while true; do
    echo "--- Attempt $COUNT ---"
    
    # 使用 -b (batch mode) 运行，读取指令文件
    # 如果程序正常退出，lldb 也会退出
    # 如果程序崩溃，bt 会被打印
    lldb -b -s .lldb_cmds -- $BINARY --gtest_filter=-ChromaticTest.SignalGuard_* > output.log 2>&1
    
    # 检查输出日志中是否包含崩溃堆栈常见的关键词
    if grep -q "stop reason =" output.log; then
        echo "[!] Crash detected!"
        cat output.log
        break
    fi
    
    ((COUNT++))
done

# 清理临时文件
rm .lldb_cmds