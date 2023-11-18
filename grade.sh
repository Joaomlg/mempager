#!/bin/bash
set -u

TESTSPEC=tests/tests.spec
# TESTSPEC=tests/test11.spec

make

mkdir -p log

while read -r num frames blocks nodiff ; do
    num=$((num))
    frames=$((frames))
    blocks=$((blocks))
    nodiff=$((nodiff))
    echo "running test$num"
    rm -rf mmu.sock mmu.pmem.img.*
    ./bin/mmu $frames $blocks &> log/test$num.mmu.out &
    sleep 1s
    ./bin/test$num &> log/test$num.out
    kill -SIGINT %1
    wait
    rm -rf mmu.sock mmu.pmem.img.*
    if [ $nodiff -eq 1 ] ; then
        continue
    fi
    if ! diff tests/test$num.mmu.out log/test$num.mmu.out > /dev/null ; then
        echo "test$num.mmu.out differs"
    fi
    if ! diff tests/test$num.out log/test$num.out > /dev/null ; then
        echo "test$num.out differs"
    fi
done < $TESTSPEC
