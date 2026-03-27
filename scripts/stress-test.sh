#!/bin/bash

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
    
    lldb -b -s .lldb_cmds -- $BINARY --gtest_filter=-ChromaticTest.SignalGuard_* > output.log 2>&1

    if grep -q "stop reason =" output.log; then
        echo "[!] Crash detected!"
        cat output.log
        break
    fi
    
    ((COUNT++))
done

rm .lldb_cmds