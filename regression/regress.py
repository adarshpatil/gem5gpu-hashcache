#!/usr/bin/env python

import optparse
import os
import sys

parser = optparse.OptionParser()
parser.add_option("--benchmark", "-b", action="store", help="Specify particular benchmark")
parser.add_option("--create-regress", "-c", action="store_true", default=False, help="Create regressions from output directories")
parser.add_option("--take-checkpoint", '-k', action="store_true", default=False, help="Create checkpoints for the benchmarks. Execution stops after the checkpoint is created")
parser.add_option("--checkpoint-dir", default=None, help="The directory containing checkpoints to restore from")
parser.add_option("--checkpoint-base-dir", default=None, help="Base directory to store and find checkpoints")
parser.add_option("--restore-from-checkpoint", action="store_true", default=False, help="Use checkpoint to run benchmarks")
parser.add_option("--debug", "-d", action="store_true", default=False, help="Print run commands, but don't execute")
parser.add_option("--fullsys", "-f", action="store_true", default=False, help="Run full-system (FS) mode (default: SE mode)")
parser.add_option("--gem5-bin", "-g", type="choice", default="opt",
                  choices = ["debug", "opt", "fast", "prof"],
                  help = "Which gem5 binary to run")
parser.add_option("--gem5-build-dir", "-e", action="store", default=None, help="Specify the gem5 build dir: build/<name>/gem5.*")
parser.add_option("--condor", "-l", action="store_true", default=False, help="Run benchmark in Condor")
parser.add_option("--simsize", "-m", action="store", default=None, help="Specify the sim input set (default None)")
parser.add_option("--norun", "-n", action="store_true", default=False, help="Don't run the benchmark. Use with -c or -r")
parser.add_option("--gem5-params", "-p", action="store", default='', help="Specify parameters to gem5 binary")
parser.add_option("--config-params", "-q", action="store", default='', help="Specify parameters to config script")
parser.add_option("--regress", "-r", action="store_true", default=False, help="Regress the output against saved output")
parser.add_option("--gem5-fusion-root", "-t", default=None, help="The path which the benchmarks/ and gem5/ directories can be found")
parser.add_option("--benchmark-threads", "-T", default=None, help="The number of threads to execute in the benchmark")
parser.add_option("--suite", "-u", action="store", default=None,
                  help = "Specify the benchmark suite to run (default all)")
parser.add_option("--silent", "-s", action="store_true", default=False, help="When enabled, stdout and stderr are suppressed from printing to the screen (disabled by default)")
parser.add_option("--runscript", "-z", action="store", default=None, help="Set the runscript to execute in full-system")
parser.add_option("--no-simulator", action="store_true", default=False, help="Run the benchmarks on real hardware")
parser.add_option("--use-bin-build-dir", action="store_true", default=False, help="Get benchmark executables out of build directory, not image directory (only useful for SE mode)")
(options, args) = parser.parse_args()

if not options.gem5_fusion_root:
    print "ERROR: No root specified. Use --gem5-fusion-root (-t) option"
    sys.exit(-1)

gem5FusionRoot = options.gem5_fusion_root

buildName = 'X86'
if options.gem5_build_dir is not None:
    buildName = options.gem5_build_dir

if 'opt' in options.gem5_bin:
    gem5BinToRun = 'gem5/build/%s/gem5.opt' % buildName
elif 'debug' in options.gem5_bin:
    gem5BinToRun = 'gem5/build/%s/gem5.debug' % buildName
elif 'fast' in options.gem5_bin:
    gem5BinToRun = 'gem5/build/%s/gem5.fast' % buildName
elif 'prof' in options.gem5_bin:
    gem5BinToRun = 'gem5/build/%s/gem5.prof' % buildName

allSuites = []
allBenchmarks = []

# Add benchmarks from suites
from rodinia.RodiniaBenchmarks import *
addRodiniaBenchmarks(gem5FusionRoot, allSuites, allBenchmarks)
addRodiniaNoCopyBenchmarks(gem5FusionRoot, allSuites, allBenchmarks)
addRodiniaOpenMPBenchmarks(gem5FusionRoot, allSuites, allBenchmarks)
from dcmbmk.DcmbmkBenchmarks import *
addDcmbmkBenchmarks(gem5FusionRoot, allSuites, allBenchmarks)
from parsec.ParsecBenchmarks import *
addParsecBenchmarks(gem5FusionRoot, allSuites, allBenchmarks)
from mevbench.MEVBenchBenchmarks import *
addMEVBenchBenchmarks(gem5FusionRoot, allSuites, allBenchmarks)

# Setup the benchmarks to run
benchmarks = []

if options.benchmark is not None:
    for bench in allBenchmarks:
        if options.benchmark in bench.name and (not options.suite or options.suite == bench.suite):
            benchmarks.append(bench)
    if len(benchmarks) == 0:
        print "ERROR: Didn't find benchmark \'%s\'" % options.benchmark
        print "Benchmarks:"
        for bench in allBenchmarks:
            print "  %s" % bench
        sys.exit(-1)
elif options.suite is not None:
    if options.suite not in allSuites:
        print "ERROR: Suite \'%s\' not in suites list" % options.suite
        print "Suites: %s" % allSuites
        sys.exit(-1)
    for bench in allBenchmarks:
        if options.suite == bench.suite:
            benchmarks.append(bench)
    if len(benchmarks) == 0:
        print "ERROR: Suite \'%s\' has no benchmarks" % options.suite
        sys.exit(-1)
else:
    benchmarks = allBenchmarks

################################################
# Run tests
################################################

# TODO: Use this file to handle running benchmarks with specified parallelism
passedBenchmarks = []
failedBenchmarks = []
for bench in benchmarks:
    print "*** Running %s ***" % bench
    if options.debug:
        bench.setDebug()
    if options.silent:
        bench.setQuiet()
    if options.use_bin_build_dir:
        bench.setUseBuildDir()
    if options.condor:
        bench.setUseCondor()
    if options.take_checkpoint:
        bench.setTakeCheckpoint()
        bench.baseCheckpointDir = options.checkpoint_base_dir
    if options.restore_from_checkpoint:
        assert(not options.take_checkpoint)
        bench.setRestoreCheckpoint()
        if options.checkpoint_base_dir:
            bench.baseCheckpointDir = options.checkpoint_base_dir
        elif options.checkpoint_dir:
            bench.checkpointDir = options.checkpoint_dir
        else:
            bench.baseCheckpointDir = '.'
    if not options.norun:
        if options.no_simulator:
            passed = bench.runHW(options.simsize)
        else:
            passed = bench.runBenchmark(gem5FusionRoot, gem5BinToRun, options.fullsys,
                                        options.simsize, options.gem5_params,
                                        options.config_params, options.benchmark_threads,
                                        options.runscript)
    else:
        # assume the benchmark passed previously
        passed = True
    if options.regress and passed:
        passed = bench.regress(gem5FusionRoot, options.fullsys, options.simsize)
    if options.create_regress and passed:
        passed = bench.createRegression(gem5FusionRoot, options.fullsys, options.simsize)

    if passed:
        passedBenchmarks.append(str(bench))
        print bench, "PASSED"
    else:
        failedBenchmarks.append(str(bench))
        print bench, "FAILED"
    print

if failedBenchmarks:
    print "SOME BENCHMARKS FAILED:"
    for bench in failedBenchmarks:
        print bench,
    print
