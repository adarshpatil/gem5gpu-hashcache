import os
from Benchmark import *
from dcmbmk import *

def addDcmbmkBenchmarks(gem5FusionRoot, suites, benchmarks):
    suites.append('dcmbmk')
    dcmbmkSEBinDir = os.path.join(gem5FusionRoot, 'benchmarks/dcmbmk-image/bin')
    dcmbmkSEInpDir = os.path.join(gem5FusionRoot, 'benchmarks/dcmbmk-image/inputs')
    dcmbmkFSBinDir = os.path.join('dcmbmk/bin')
    dcmbmkFSInpDir = os.path.join('dcmbmk/inputs')
    # Note: this can/should be a symlink and/or get passed in
    dcmbmkRcSDir = os.path.join(gem5FusionRoot, 'full_system_files/runscripts')
    dcmbmkCmdLines = {}

    benchNames = ['cmem', 'diverge', 'global', 'icache1', 'icache2', 'icache3',
                  'icache4', 'shared', 'sync', 'texture2', 'texture4'];

    for benchName in benchNames:
        bench = Benchmark(suite = 'dcmbmk',
                          name = benchName,
                          executable = 'gem5_fusion_%s' % benchName,
                          seBinDir = dcmbmkSEBinDir,
                          seInpDir = os.path.join(dcmbmkSEInpDir, benchName),
                          fsBinDir = dcmbmkFSBinDir,
                          fsInpDir = os.path.join(dcmbmkFSInpDir, benchName),
                          rcSDir = dcmbmkRcSDir,
                          simSizes = ['default'],
                          cmdLines = dcmbmkCmdLines)
        benchmarks.append(bench)
