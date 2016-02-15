# Copyright (c) 2006-2008 The Regents of The University of Michigan
# Copyright (c) 2012-2015 Mark D. Hill and David A. Wood
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Jason Power, Joel Hestness

import optparse
import os
import sys
from os.path import join as joinpath

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, fatal

addToPath('../../gem5/configs/common')
addToPath('../../gem5/configs/ruby')
addToPath('../../gem5/configs/topologies')
addToPath('gpu_protocol')

import GPUConfig
import GPUMemConfig
import Options
import Ruby
import Simulation
import Mybench

parser = optparse.OptionParser()
GPUConfig.addGPUOptions(parser)
GPUMemConfig.addMemCtrlOptions(parser)
Options.addCommonOptions(parser)
Options.addSEOptions(parser)

#
# Add the ruby specific and protocol specific options
#
Ruby.define_options(parser)

(options, args) = parser.parse_args()

options.ruby = True

if args:
    print "Error: script doesn't take any positional arguments"
    sys.exit(1)

if buildEnv['TARGET_ISA'] not in ["x86", "arm"]:
    fatal("gem5-gpu SE doesn't currently work with non-x86 or non-ARM system!")

#
# CPU type configuration
#
if options.cpu_type != "timing" and options.cpu_type != "TimingSimpleCPU" \
    and options.cpu_type != "detailed" and options.cpu_type != "DerivO3CPU":
    print "Warning: gem5-gpu only known to work with timing and detailed CPUs: Proceed at your own risk!"
(CPUClass, test_mem_mode, FutureClass) = Simulation.setCPUClass(options)

# If fast-forwarding, set the fast-forward CPU and mem mode for
# timing rather than atomic
if options.fast_forward:
    assert(CPUClass == AtomicSimpleCPU)
    assert(test_mem_mode == "atomic")
    CPUClass, test_mem_mode = Simulation.getCPUClass("TimingSimpleCPU")
    
#
# Memory space configuration
#
(cpu_mem_range, gpu_mem_range, total_mem_range) = GPUConfig.configureMemorySpaces(options)

#
# Setup benchmark to be run
#
#process = LiveProcess()
#process.executable = options.cmd
#process.cmd = [options.cmd] + options.options.split()

#if options.input != "":
#    process.input = options.input
#if options.output != "":
#    process.output = options.output
#if options.errout != "":
#    process.errout = options.errout

# Hard code the cache block width to 128B for now
# TODO: Remove this if/when block size can be different than 128B
if options.cacheline_size != 128:
    print "Warning: Only block size currently supported is 128B. Defaulting to 128."
    options.cacheline_size = 128

#
# Instantiate system
#
system = System(cpu = [CPUClass(cpu_id = i)
                       for i in xrange(options.num_cpus)],
                mem_mode = test_mem_mode,
                mem_ranges = [cpu_mem_range],
                cache_line_size = options.cacheline_size)

# Create a top-level voltage domain
system.voltage_domain = VoltageDomain(voltage = options.sys_voltage)

# Create a source clock for the system and set the clock period
system.clk_domain = SrcClockDomain(clock = options.sys_clock,
                                   voltage_domain = system.voltage_domain)

# Create a CPU voltage domain
system.cpu_voltage_domain = VoltageDomain()

# Create a separate clock domain for the CPUs
system.cpu_clk_domain = SrcClockDomain(clock = options.cpu_clock,
                                       voltage_domain =
                                       system.cpu_voltage_domain)

Simulation.setWorkCountOptions(system, options)

#
# Create the GPU
#
system.gpu = GPUConfig.createGPU(options, gpu_mem_range)

#
# Setup Ruby
#
system.ruby_clk_domain = SrcClockDomain(clock = options.ruby_clock,
                                        voltage_domain = system.voltage_domain)
Ruby.create_system(options, False, system)

system.gpu.ruby = system.ruby
system.ruby.clk_domain = system.ruby_clk_domain

if options.split:
    if options.access_backing_store:
        #
        # Reset Ruby's phys_mem to add the device memory range
        #
        system.ruby.phys_mem = SimpleMemory(range=total_mem_range,
                                            in_addr_map=False)

#
# Setup benchmark to be run
#

benchmark = options.benchmark.split(';')
for (i, cpu) in enumerate(system.cpu):
    if benchmark[i]:
        # spec 2006 benchmarks
        if benchmark[i] == 'perlbench':
            print 'CPU ' + `i` +  '--> perlbench'
            process = Mybench.perlbench
        elif benchmark[i] == 'bzip2':
            print 'CPU ' + `i` +  '--> bzip2'
            process = Mybench.bzip2
        elif benchmark[i] == 'gcc':
            print 'CPU ' + `i` +  '--> gcc'
            process = Mybench.gcc
        elif benchmark[i] == 'bwaves':
            print 'CPU ' + `i` +  '--> bwaves'
            process = Mybench.bwaves
        elif benchmark[i] == 'gamess':
            print 'CPU ' + `i` +  '--> gamess'
            process = Mybench.gamess
        elif benchmark[i] == 'mcf':
            print 'CPU ' + `i` + '--> mcf'
            process = Mybench.mcf
        elif benchmark[i] == 'milc':
            print 'CPU ' + `i` +  '--> milc'
            process = Mybench.milc
        elif benchmark[i] == 'zeusmp':
            print '--> zeusmp'
            process = Mybench.zeusmp
        elif benchmark[i] == 'gromacs':
            print 'CPU ' + `i` +  '--> gromacs'
            process = Mybench.gromacs
        elif benchmark[i] == 'cactusADM':
            print 'CPU ' + `i` +  '--> cactusADM'
            process = Mybench.cactusADM
        elif benchmark[i] == 'leslie3d':
            print 'CPU ' + `i` +  '--> leslie3d'
            process = Mybench.leslie3d
        elif benchmark[i] == 'namd':
            print 'CPU ' + `i` +  '--> namd'
            process = Mybench.namd
        elif benchmark[i] == 'gobmk':
            print 'CPU ' + `i` +  '--> gobmk'
            process = Mybench.gobmk
        elif benchmark[i] == 'dealII':
            print 'CPU ' + `i` +  '--> dealII'
            process = Mybench.dealII
        elif benchmark[i] == 'soplex':
            print 'CPU ' + `i` +  '--> soplex'
            process = Mybench.soplex
        elif benchmark[i] == 'povray':
            print 'CPU ' + `i` +  '--> povray'
            process = Mybench.povray
        elif benchmark[i] == 'calculix':
            print 'CPU ' + `i` +  '--> calculix'
            process = Mybench.calculix
        elif benchmark[i] == 'hmmer':
            print 'CPU ' + `i` +  '--> hmmer'
            process = Mybench.hmmer
        elif benchmark[i] == 'sjeng':
            print 'CPU ' + `i` +  '--> sjeng'
            process = Mybench.sjeng
        elif benchmark[i] == 'GemsFDTD':
            print 'CPU ' + `i` +  '--> GemsFDTD'
            process = Mybench.GemsFDTD
        elif benchmark[i] == 'libquantum':
            print 'CPU ' + `i` +  '--> libquantum'
            process = Mybench.libquantum
        elif benchmark[i] == 'h264ref':
            print 'CPU ' + `i` +  '--> h264ref'
            process = Mybench.h264ref
        elif benchmark[i] == 'tonto':
            print 'CPU ' + `i` +  '--> tonto'
            process = Mybench.tonto
        elif benchmark[i] == 'lbm':
            print 'CPU ' + `i` +  '--> lbm'
            process = Mybench.lbm
        elif benchmark[i] == 'omnetpp':
            print 'CPU ' + `i` +  '--> omnetpp'
            process = Mybench.omnetpp
        elif benchmark[i] == 'astar':
            print 'CPU ' + `i` +  '--> astar'
            process = Mybench.astar
        elif benchmark[i] == 'wrf':
            print 'CPU ' + `i` +  '--> wrf'
            process = Mybench.wrf
        elif benchmark[i] == 'sphinx3':
            print 'CPU ' + `i` +  '--> sphinx3'
            process = Mybench.sphinx3
        elif benchmark[i] == 'xalancbmk':
            print 'CPU ' + `i` +  '--> xalancbmk'
            process = Mybench.xalancbmk
        elif benchmark[i] == 'specrand_i':
            print 'CPU ' + `i` +  '--> specrand_i'
            process = Mybench.specrand_i
        elif benchmark[i] == 'specrand_f':
            print 'CPU ' + `i` +  '--> specrand_f'
            process = Mybench.specrand_f

        # rodinia benchmarks
        elif benchmark[i] == 'backprop':
            print 'CPU ' + `i` +  '--> backprop'
            process = Mybench.backprop
        elif benchmark[i] == 'bfs':
            print 'CPU ' + `i` +  '--> bfs'
            process = Mybench.bfs
        elif benchmark[i] == 'hotspot':
            print 'CPU ' + `i` +  '--> hotspot'
            process = Mybench.hotspot
        elif benchmark[i] == 'kmeans':
            print 'CPU ' + `i` +  '--> kmeans '
            process = Mybench.kmeans
        elif benchmark[i] == 'mummer':
            print 'CPU ' + `i` +  '--> mummer'
            process = Mybench.mummer
        elif benchmark[i] == 'needle':
            print 'CPU ' + `i` +  '--> needle'
            process = Mybench.needle
        elif benchmark[i] == 'lud':
            print 'CPU ' + `i` +  '--> lud'
            process = Mybench.lud
        elif benchmark[i] == 'streamcluster':
            print 'CPU ' + `i` +  '--> streamcluster'
            process = Mybench.streamcluster
        elif benchmark[i] == 'gaussian':
            print 'CPU ' + `i` +  '--> gaussian'
            process = Mybench.gaussian
        elif benchmark[i] == 'particlefilter_naive':
            print 'CPU ' + `i` +  '--> particlefilter_naive'
            process = Mybench.particlefilter_naive
        elif benchmark[i] == 'heartwall':
            print 'CPU ' + `i` +  '--> heartwall'
            process = Mybench.heartwall
        else:
            print "No recognized benchmark selected! Exiting."
            sys.exit(1)
    else:
        print >> sys.stderr, "Need --benchmark switch to specify workload. Exiting!\n"
        sys.exit(1)

    # Set process stdout/stderr
    if options.benchmark_stdout:
        process.output = options.benchmark_stdout
        print "Process stdout file: " + process.output
    if options.benchmark_stderr:
        process.errout = options.benchmark_stderr
        print "Process stderr file: " + process.errout
    
    cpu.workload = process
#
# Connect CPU ports
#
for (i, cpu) in enumerate(system.cpu):
    ruby_port = system.ruby._cpu_ports[i]

    cpu.clk_domain = system.cpu_clk_domain
    cpu.createThreads()
    cpu.createInterruptController()
    #
    # Tie the cpu ports to the correct ruby system ports
    #
    cpu.icache_port = system.ruby._cpu_ports[i].slave
    cpu.dcache_port = system.ruby._cpu_ports[i].slave
    cpu.itb.walker.port = system.ruby._cpu_ports[i].slave
    cpu.dtb.walker.port = system.ruby._cpu_ports[i].slave
    if buildEnv['TARGET_ISA'] == "x86":
        cpu.interrupts.pio = ruby_port.master
        cpu.interrupts.int_master = ruby_port.slave
        cpu.interrupts.int_slave = ruby_port.master

#
# Connect GPU ports
#
GPUConfig.connectGPUPorts(system.gpu, system.ruby, options)

if options.mem_type == "RubyMemoryControl":
    GPUMemConfig.setMemoryControlOptions(system, options)

print "***"
print options
print "***"
#
# Finalize setup and run
#

root = Root(full_system = False, system = system)

m5.disableAllListeners()

Simulation.run(options, root, system, FutureClass)
