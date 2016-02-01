import os
import sys
from Benchmark import *
from mevbench import *

class MEVBenchBenchmark(Benchmark):
    def __init__(self, gem5FusionRoot, name, data):
        Benchmark.__init__(self)
        self.suite = 'mevbench'
        self.name = name
        self.executable = data['executable']
        self.seBinDir = os.path.join(gem5FusionRoot, 'benchmarks/mevbench-image/bin')
        self.seBinBuildDir = 'mevbench-junk-string'
        self.seInpDir = os.path.join(gem5FusionRoot, 'benchmarks/mevbench-image')
        self.fsBinDir = os.path.join('mevbench/bin')
        self.fsInpDir = os.path.join('mevbench')
        self.rcSDir = os.path.join(gem5FusionRoot, 'full_system_files/runscripts')
        self.simSizes = data['inputs'].keys()
        self.cmdLines = data['inputs']
        try:
            self.buildDir = data['buildDir']
        except KeyError:
            self.buildDir = self.name

def addMEVBenchBenchmarks(gem5FusionRoot, suites, benchmarks):
    suites.append('mevbench')

    inputsetsFile = open(os.path.join(os.path.dirname(__file__), 'mevbench_inputsets.json'), 'r')
    import json
    info = json.load(inputsetsFile)

    for name,data in info.items():
        bench = MEVBenchBenchmark(gem5FusionRoot, name, data)
        benchmarks.append(bench)

    inputsetsFile.close()
