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
    qappd=`hg qapp | wc -l`
    if [ "$qappd" -gt "0" ]; then
        changeset=`hg log -r qparent | grep changeset | sed 's/changeset:[ ]*//'`
    else
        changeset=`hg log -r tip | grep changeset | sed 's/changeset:[ ]*//'`
    fi
    output+="$i;$changeset"
    if [ ! -z "$output" ]; then
        output+=";"
    fi
    popd >& /dev/null
done
echo $output
