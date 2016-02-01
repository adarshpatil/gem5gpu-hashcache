import os

class Benchmark:
    configScript = None
    debug = False
    quiet = False
    useBuildDir = False
    runCondor = False
    takeCheckpoint = False
    restoreCheckpoint = False
    accessHostPagetable = False
    baseCheckpointDir = None
    checkpointDir = None
    export = False
    exportString = ""
    seOutputFiles = ['system.out', 'config.ini', 'gem5.out', 'stats.txt', 'ruby.stats', 'gpu_stats.txt', 'ce_stats.txt']
    fsOutputFiles = ['system.pc.com_1.terminal', 'config.ini', 'gem5.out', 'stats.txt', 'ruby.stats', 'gpu_stats.txt', 'ce_stats.txt']

    def __init__(self, suite = None, name = None, executable = None, seBinDir = None, seBinBuildDir=None, seInpDir = None, fsBinDir = None, fsInpDir = None, rcSDir = None, simSizes = None, cmdLines = None, buildDir = None):
        self.suite = suite
        self.name = name
        self.executable = executable
        self.seBinDir = seBinDir
        self.seBinBuildDir = seBinBuildDir
        self.seInpDir = seInpDir
        self.fsBinDir = fsBinDir
        self.fsInpDir = fsInpDir
        self.rcSDir = rcSDir
        self.simSizes = simSizes
        self.cmdLines = cmdLines
        self.buildDir = buildDir

    def __str__(self):
        return '%s: %s' % (self.suite, self.name)

    def runCommand(self, command):
        if self.debug:
            print command
            return True
        else:
            ret = os.system(command)
            if ret == 0:
                return True
            else:
                return False

    def setConfigScript(self, configScript):
        self.configScript = configScript

    def setDebug(self):
        self.debug = True

    def setQuiet(self):
        self.quiet = True

    def setUseBuildDir(self):
        self.useBuildDir = True

    def setUseCondor(self):
        self.runCondor = True

    def setTakeCheckpoint(self):
        self.takeCheckpoint = True

    def setRestoreCheckpoint(self):
        self.restoreCheckpoint = True

    def generateRcSScript(self, rcSFilename, simsize, threads):
        if simsize is not None:
            commandline = self.cmdLines[simsize]
            commandline = commandline.replace('<inputdir>', self.fsInpDir)
            if threads:
                commandline = commandline.replace('<nthreads>', threads)
        benchbin = os.path.join(self.fsBinDir, self.executable)
        runscript_file = open(rcSFilename, 'w')
        runscript_file.write('#!/bin/sh\n\n# File to run the %s benchmark\n\n' % self.name)
        runscript_file.write('mkdir benchmarks\n')
        runscript_file.write('mount /dev/hdb1 benchmarks\n')
        runscript_file.write('cd benchmarks\n')
        if self.export and threads:
            exportValue = self.exportString.replace('<nthreads>', threads)
            runscript_file.write('export %s\n' % exportValue)
        runscript_file.write('/sbin/m5 dumpresetstats\n')
        if simsize is not None:
            runscript_file.write('%s %s\n' % (benchbin, commandline))
        else:
            runscript_file.write('%s\n' % benchbin)
        runscript_file.write('/sbin/m5 dumpresetstats\n')
        runscript_file.write('echo \"Done :D\"\n')
        runscript_file.write('/sbin/m5 exit\n')
        runscript_file.close()

    def runBenchmark(self, gem5FusionRoot, gem5Bin, fullsystem = False, simsize = None, gem5Params = '', configParams = '', threads = None, runscript = None):
        gem5BinPath = os.path.join(gem5FusionRoot, gem5Bin)
        if not os.path.exists(gem5BinPath):
            print "ERROR: gem5 binary \'%s\' does not exist" % gem5BinPath
            return

        simsize = self.getSimsize(simsize)
        if not simsize:
            return False

        # Setup the system configuration script
        if self.configScript is None:
            if fullsystem:
                configScript = os.path.join(gem5FusionRoot, 'gem5-gpu/configs/fs_fusion.py')
            else:
                configScript = os.path.join(gem5FusionRoot, 'gem5-gpu/configs/se_fusion.py')
        else:
            configScript = self.configScript

        outputDir = self.getOutputDir(fullsystem, simsize)
        outFile = os.path.join(outputDir, 'system.out')

        # Setup FS and SE specific run commands
        if fullsystem:
            if self.rcSDir is None:
                print 'ERROR: rcSDir not specified for benchmark %s' % self.name
                return
            if runscript is None:
                if simsize != 'default':
                    if threads:
                        rcSScript = os.path.join(self.rcSDir, '%s_%st_%s.rcS' % (self.name, threads, simsize))
                    else:
                        rcSScript = os.path.join(self.rcSDir, '%s_%s.rcS' % (self.name, simsize))
                else:
                    rcSScript = os.path.join(self.rcSDir, '%s.rcS' % self.name)
                if not os.path.exists(rcSScript):
                    self.generateRcSScript(rcSScript, simsize, threads)
            else:
                rcSScript = os.path.join(self.rcSDir, runscript)
                if not os.path.exists(rcSScript):
                    print 'ERROR: Specified runscript \'%s\' does not exist' % rcSScript
                    return

            # Setup the command to run
            command = '%s --outdir=%s %s %s `cat %s` --script=%s' % (gem5BinPath, outputDir, gem5Params, configScript, configParams, rcSScript)

        # Handle syscall emulation mode
        else:
            # Setup the binary to be executed
            if self.useBuildDir:
                print "buildDir is", self.buildDir
                benchmarkBinPath = os.path.join(self.seBinBuildDir, self.buildDir, self.executable)
            else:
                benchmarkBinPath = os.path.join(self.seBinDir, self.executable)

            # Setup the benchmark command line
            if simsize != 'default':
                benchmarkCmdLine = self.cmdLines[simsize].replace('<inputdir>', self.seInpDir)
            else:
                benchmarkCmdLine = ''

            # Setup the command to run
            command = '%s --outdir=%s %s %s %s -c %s -o "%s" --num-cpus=5 --output=%s ' % (gem5BinPath, outputDir, gem5Params, configScript, configParams, benchmarkBinPath, benchmarkCmdLine, outFile)

        outputRoot = os.getcwd()
        relOutput = outputDir[len(outputRoot)+1:]
        if self.restoreCheckpoint or self.takeCheckpoint:
            if self.baseCheckpointDir:
                checkpointDir = os.path.join(self.baseCheckpointDir, relOutput)
            elif self.checkpointDir:
                checkpointDir = self.checkpointDir
            else:
                print "ERROR: Checkpoint directory not specified"
                return False

        if self.accessHostPagetable:
            command += " --access-host-pagetable "

        if self.takeCheckpoint:
            command += " --checkpoint-dir=%s --work-begin-exit-count=1 --checkpoint-at-end " % (checkpointDir)

        if self.restoreCheckpoint:
            command += " -r1 --checkpoint-dir=%s --restore-with-cpu=timing " % (checkpointDir)

        if self.quiet and not self.runCondor:
            command += '  > %s ' % ('%s/gem5.out' % outputDir)

        if self.runCondor:
            condor_script = os.path.join(os.path.dirname(os.path.realpath(__file__)), 'condor/condor_shell.py')
            command = '%s -c \'%s\' --output=%s' % (condor_script, command, '%s/gem5.out' % outputDir)

        # Run the benchmark
        return self.runCommand(command)

    def runHW(self, fullsystem = None, simsize = None):
        assert(self.seBinBuildDir)

        benchmarkBinPath = os.path.join(self.seBinBuildDir, self.name, self.executable)

        simsize = self.getSimsize(simsize)

        if simsize != 'default':
            benchmarkCmdLine = self.cmdLines[simsize].replace('<inputdir>', self.seInpDir)
        else:
            benchmarkCmdLine = ''

        outputFile = os.path.join(self.getOutputDir(fullsystem, simsize), "system.out")

        command = "%s %s > %s" % (benchmarkBinPath, benchmarkCmdLine, outputFile)

        return self.runCommand(command)

    def regress(self, gem5FusionRoot, fullsystem = False, simsize = None):
        print 'Regressing files for benchmark: %s' % self.name

        simsize = self.getSimsize(simsize)

        outputDir = self.getOutputDir(fullsystem, simsize, False)

        if fullsystem:
            filesToRegress = self.fsOutputFiles
        else:
            filesToRegress = self.seOutputFiles

        returnValue = True
        if os.path.exists(outputDir):
            # Simple file diffs for now:
            regressOutputFile = os.path.join(outputDir, 'regress.out')
            if os.path.exists(regressOutputFile):
                rmCommand = 'rm -f %s' % regressOutputFile
                self.runCommand(rmCommand)
            for file in filesToRegress:
                filePath = os.path.join(outputDir, file)
                regressFilePath = os.path.join(outputDir, '%s.reg' % file)
                if not os.path.exists(filePath):
                    print 'WARNING: File to regress does not exist \'%s\', skipping...' % file
                    regressCommand = 'echo "FILE: %s:\n\n<<<Missing>>>\n" >> %s' % (file, regressOutputFile)
                elif not os.path.exists(regressFilePath):
                    print 'WARNING: Regress file does not exist \'%s\', skipping...' % ("%s.reg" % file)
                    regressCommand = 'echo "FILE: %s:\n\n<<<Missing regress file>>>\n" >> %s' % (file, regressOutputFile)
                else:
                    regressCommand = 'echo "FILE: %s:\n\n`diff -w %s %s`\n" >> %s' % (file, filePath, regressFilePath, regressOutputFile)
                self.runCommand(regressCommand)
        else:
            returnValue = False

        print 'Done regressing files for benchmark: %s' % self.name
        return returnValue

    def createRegression(self, gem5FusionRoot, fullsystem = False, simsize = None):
        print 'Creating regression files for benchmark: %s...' % self.name

        simsize = self.getSimsize(simsize)

        outputDir = self.getOutputDir(fullsystem, simsize, False)

        returnValue = True
        if os.path.exists(outputDir):
            if fullsystem:
                filesToSave = self.fsOutputFiles
            else:
                filesToSave = self.seOutputFiles
            for file in filesToSave:
                filePath = os.path.join(outputDir, file)
                saveFilePath = os.path.join(outputDir, '%s.reg' % file)
                if not os.path.exists(filePath):
                    print 'WARNING: File to save \'%s\' does not exist, skipping...' % filePath
                    returnValue = False
                else:
                    backupCommand = 'cp %s %s' % (filePath, saveFilePath)
                    self.runCommand(backupCommand)
        else:
            returnValue = False

        print 'Done creating regression files for benchmark: %s' % self.name
        return returnValue

    def __repr__(self):
        return str(self.suite) + ': ' + str(self.name)

    def getOutputDir(self, fullsystem, simsizeName, create=True):

        # Setup the output directory and file
        outputRoot = os.getcwd()
        if fullsystem:
            outputDir = os.path.join(outputRoot, self.suite, simsizeName, 'fs_%s' % self.name)
        else:
            outputDir = os.path.join(outputRoot, self.suite, simsizeName, self.name)
        if not os.path.exists(outputDir):
            if create:
                mkdircmd = 'mkdir -p %s' % outputDir
                self.runCommand(mkdircmd)
            else:
                print 'WARNING: output directory \'%s\' does not exist' % outputDir

        return outputDir

    def getSimsize(self, simsize):
        # Setup the input set size
        if simsize is not None:
            if simsize not in self.simSizes:
                print 'ERROR: For benchmark %s, %s is not valid simsize. Choose from %s' % (self.name, simsize, self.simSizes)
                return None
        elif self.simSizes is not None:
            simsize = self.simSizes[0]
        else:
            simsize = 'default'

        return simsize

