#!/bin/bash
set -u

TESTSPEC=tests/tests.spec
# TESTSPEC=tests/test11.spec

make

while read -r num frames blocks nodiff ; do
    num=$((num))
    frames=$((frames))
    blocks=$((blocks))
    nodiff=$((nodiff))
    echo "running test$num"
    rm -rf mmu.sock mmu.pmem.img.*
    ./bin/mmu $frames $blocks &> test$num.mmu.out &
    sleep 1s
    ./bin/test$num &> test$num.out
    kill -SIGINT %1
    wait
    rm -rf mmu.sock mmu.pmem.img.*
    if [ $nodiff -eq 1 ] ; then
        continue
    fi
    if ! diff tests/test$num.mmu.out test$num.mmu.out > /dev/null ; then
        echo "test$num.mmu.out differs"
    fi
    if ! diff tests/test$num.out test$num.out > /dev/null ; then
        echo "test$num.out differs"
    fi
done < $TESTSPEC
