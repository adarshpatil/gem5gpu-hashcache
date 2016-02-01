import os
from Benchmark import *
from rodinia import *

class ParsecBenchmark(Benchmark):
    def __init__(self, gem5FusionRoot, name, data):
        Benchmark.__init__(self)
        self.suite = 'parsec'
        self.name = name
        self.executable = data['executable']
        self.fsBinDir = os.path.join('parsec/install/bin')
        self.fsInpDir = os.path.join('parsec/install/inputs', name)
        self.rcSDir = os.path.join(gem5FusionRoot, 'full_system_files/runscripts')
        self.simSizes = data['inputs'].keys()
        self.cmdLines = data['inputs']

def addParsecBenchmarks(gem5FusionRoot, suites, benchmarks):
    suites.append('parsec')

    inputsetsFile = open(os.path.join(os.path.dirname(__file__), 'parsec_inputsets.json'), 'r')
    import json
    info = json.load(inputsetsFile)

    for name,data in info.items():
        bench = ParsecBenchmark(gem5FusionRoot, name, data)
        benchmarks.append(bench)
