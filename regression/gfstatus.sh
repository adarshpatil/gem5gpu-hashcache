#!/bin/bash

if [ ! -z "$GEM5FUSION" ]; then
    baseDir=$GEM5FUSION/..
else
    baseDir=..
fi

cd $baseDir
for i in gem5 gem5/.hg/patches gpgpu-sim gpgpu-sim/.hg/patches gem5-fusion benchmarks; do
    pushd . >& /dev/null
    cd $i
    echo "$i:"
    hg st
    echo ""
    popd >& /dev/null
done
