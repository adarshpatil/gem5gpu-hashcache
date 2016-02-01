import os
from Benchmark import *
from rodinia import *

class RodiniaBenchmark(Benchmark):
    def __init__(self, gem5FusionRoot, name, data):
        Benchmark.__init__(self)
        self.suite = 'rodinia'
        self.name = name
        self.executable = 'gem5_fusion_%s' % data['executable']
        self.seBinDir = os.path.join(gem5FusionRoot, 'benchmarks/rodinia-image/bin')
        self.seBinBuildDir = os.path.join(gem5FusionRoot, 'benchmarks/rodinia')
        self.seInpDir = os.path.join(gem5FusionRoot, 'benchmarks/rodinia-image/inputs', name)
        self.fsBinDir = os.path.join('rodinia/bin')
        self.fsInpDir = os.path.join('rodinia/inputs', name)
        self.rcSDir = os.path.join(gem5FusionRoot, 'full_system_files/runscripts')
        self.simSizes = data['inputs'].keys()
        self.cmdLines = data['inputs']
        try:
            self.buildDir = data['buildDir']
        except KeyError:
            self.buildDir = self.name

class RodiniaNoCopyBenchmark(RodiniaBenchmark):
    def __init__(self, gem5FusionRoot, benchName, cmdLines):
        RodiniaBenchmark.__init__(self, gem5FusionRoot, benchName, cmdLines)
        self.suite = 'rodinia-nocopy'
        self.seBinBuildDir = os.path.join(gem5FusionRoot, 'benchmarks/rodinia-nocopy')
        self.seBinDir = os.path.join(gem5FusionRoot, 'benchmarks/rodinia-nocopy-image/bin')
        self.fsBinDir = os.path.join('rodinia-nocopy/bin')
        self.accessHostPagetable = True

class RodiniaOpenMPBenchmark(RodiniaBenchmark):
    def __init__(self, gem5FusionRoot, benchName, cmdLines):
        RodiniaBenchmark.__init__(self, gem5FusionRoot, benchName, cmdLines)
        self.suite = 'rodinia-omp'
        self.executable = cmdLines['executable']
        self.seBinBuildDir = os.path.join(gem5FusionRoot, 'benchmarks/rodinia-omp')
        self.seBinDir = os.path.join(gem5FusionRoot, 'benchmarks/rodinia-omp-image/bin')
        self.fsBinDir = os.path.join('rodinia-omp/bin')
        if 'export' in cmdLines.keys():
            self.export = True
            self.exportString = cmdLines['export']

def addRodiniaBenchmarks(gem5FusionRoot, suites, benchmarks):
    suites.append('rodinia')

    inputsetsFile = open(os.path.join(os.path.dirname(__file__), 'rodinia_inputsets.json'), 'r')
    import json
    info = json.load(inputsetsFile)

    for name,data in info.items():
        bench = RodiniaBenchmark(gem5FusionRoot, name, data)
        benchmarks.append(bench)


def addRodiniaNoCopyBenchmarks(gem5FusionRoot, suites, benchmarks):
    suites.append('rodinia-nocopy')

    inputsetsFile = open(os.path.join(os.path.dirname(__file__), 'rodinia_inputsets.json'), 'r')
    import json
    info = json.load(inputsetsFile)

    for name,data in info.items():
        bench = RodiniaNoCopyBenchmark(gem5FusionRoot, name, data)
        benchmarks.append(bench)

    inputsetsFile.close()

def addRodiniaOpenMPBenchmarks(gem5FusionRoot, suites, benchmarks):
    suites.append('rodinia-omp')

    inputsetsFile = open(os.path.join(os.path.dirname(__file__), 'rodinia_omp_inputsets.json'), 'r')
    import json
    info = json.load(inputsetsFile)

    for name,data in info.items():
        bench = RodiniaOpenMPBenchmark(gem5FusionRoot, name, data)
        benchmarks.append(bench)
