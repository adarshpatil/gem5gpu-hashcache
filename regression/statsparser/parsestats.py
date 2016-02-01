#!/usr/bin/python

import optparse
import os
import sys

# Binary search to find index (period number)
def getPeriod(intervals, tick):
    if len(intervals) > 0:
        lowerIndex = 0
        upperIndex = len(intervals) - 1
        currIndex = ((upperIndex - lowerIndex) / 2) + lowerIndex
        while lowerIndex != upperIndex:
            if tick >= intervals[currIndex] and tick <= intervals[currIndex+1]:
                return currIndex
            elif tick >= intervals[currIndex]:
                lowerIndex = currIndex
                currIndex = ((upperIndex - lowerIndex) / 2) + lowerIndex
            elif tick <= intervals[currIndex]:
                upperIndex = currIndex
                currIndex = ((upperIndex - lowerIndex) / 2) + lowerIndex
    else:
        return 0

def getValue(string):
    if "%" in string:
        value = float(string.replace("%","")) / 100
    else:
        value = float(string)
    return value

parser = optparse.OptionParser()
parser.add_option("--output-dir", "-o", action="store", help="Specify directory from which to parse stats")
(options, args) = parser.parse_args()

if options.output_dir is not None:
    outputDir = options.output_dir
else:
    print "ERROR: Must specify output directory to -o option"
    sys.exit(-1)

if not os.path.dirname(outputDir):
    outputDir = os.path.join('./', outputDir)
    if not os.path.dirname(outputDir):
        print "ERROR: gem5 output directory \'%s\' doesn't exist" % outputDir
        sys.exit(-1)

# Read appropriate stats files
statsFilename = os.path.join(outputDir, 'stats.txt')
gpuStatsFilename = os.path.join(outputDir, 'gpu_stats.txt')

cache_line_size_bytes = 128

allStatsNames = {}
PERIOD_START_TICK = 'system.period_start_tick'
PERIOD_END_TICK = 'system.period_end_tick'
PERIOD_TICKS = 'system.period_ticks'
SPA_KERNELS_EXECUTED = 'system.stream_proc_array.kernels_executed'
SPA_KERNEL_TICKS = 'system.stream_proc_array.kernel_ticks'
SPA_CTA_COUNT = 'system.stream_proc_array.cta_count'
SPA_CTA_TICKS = 'system.stream_proc_array.cta_ticks'
GEM5_STAT = 1
RUBY_STAT = 2
allStatsNames[PERIOD_START_TICK] = GEM5_STAT
allStatsNames[PERIOD_END_TICK] = GEM5_STAT
allStatsNames[PERIOD_TICKS] = GEM5_STAT
allStatsNames[SPA_KERNELS_EXECUTED] = GEM5_STAT
allStatsNames[SPA_KERNEL_TICKS] = GEM5_STAT
allStatsNames[SPA_CTA_COUNT] = GEM5_STAT
allStatsNames[SPA_CTA_TICKS] = GEM5_STAT
allStats = []
intervals = []
intervals.append(0)
ctaStats = {}


GPU_STATS_START = 0
GPU_STATS_KERNELS = 1
GPU_STATS_CTAS = 2
GPU_STATS_DONE = 3
max_cta_periods = 0
if os.path.exists(gpuStatsFilename):
    statsFile = open(gpuStatsFilename, 'r')
    gpuStatsPart = GPU_STATS_START
    for line in statsFile:
        if 'kernel times (ticks):' in line:
            if gpuStatsPart != GPU_STATS_START:
                print "ERROR: Malformed GPU stats file \'%s\'" % gpuStatsFilename
                sys.exit(-1)
            gpuStatsPart += 1

        elif 'shader CTA times (ticks):' in line:
            if gpuStatsPart != GPU_STATS_KERNELS:
                print "ERROR: Malformed GPU stats file \'%s\'" % gpuStatsFilename
                sys.exit(-1)
            gpuStatsPart += 1

        elif 'total kernel time = ' in line:
            if gpuStatsPart != GPU_STATS_CTAS:
                print "ERROR: Malformed GPU stats file \'%s\'" % gpuStatsFilename
                sys.exit(-1)
            gpuStatsPart += 1

        elif gpuStatsPart == GPU_STATS_KERNELS:
            line = line.rstrip('\n')
            if 'start' not in line and line is not "":
                currentPeriod = 0
                isKernelExecuting = 0
                tokens = line.split(',')
                numKernels = 0
                endTick = 0
                for token in tokens:
                    endTick = int(token)
                    intervals.append(endTick)
                    allStats.append({})
                    if currentPeriod == 0:
                        allStats[currentPeriod][PERIOD_START_TICK] = 0
                    else:
                        allStats[currentPeriod][PERIOD_START_TICK] = allStats[currentPeriod-1][PERIOD_END_TICK]
                    allStats[currentPeriod][SPA_KERNELS_EXECUTED] = isKernelExecuting
                    allStats[currentPeriod][PERIOD_END_TICK] = endTick
                    allStats[currentPeriod][PERIOD_TICKS] = allStats[currentPeriod][PERIOD_END_TICK] - allStats[currentPeriod][PERIOD_START_TICK]
                    if isKernelExecuting:
                        numKernels += 1
                        allStats[currentPeriod][SPA_KERNEL_TICKS] = allStats[currentPeriod][PERIOD_TICKS]
                    isKernelExecuting = 1 - isKernelExecuting
                    currentPeriod += 1

        elif gpuStatsPart == GPU_STATS_CTAS:
            line = line.rstrip('\n')
            if 'start' not in line and line is not "":
                tokens = line.split(',')
                shaderID = int(tokens.pop(0))
                ctaID = int(tokens.pop(0))
                ctaExecuting = False
                ctaStartPeriod = -1
                for (index, token) in enumerate(tokens):
                    tick = int(token)
                    if shaderID not in ctaStats.keys():
                        ctaStats[shaderID] = {}
                    if ctaID not in ctaStats[shaderID].keys():
                        ctaStats[shaderID][ctaID] = []
                    ctaStats[shaderID][ctaID].append(tick)
                    if len(ctaStats[shaderID][ctaID]) > max_cta_periods:
                        max_cta_periods = len(ctaStats[shaderID][ctaID])
                    currentPeriod = getPeriod(intervals, tick)
                    isKernelExecuting = allStats[currentPeriod][SPA_KERNELS_EXECUTED]
                    if not isKernelExecuting and index != len(tokens)-1:
                        print "ERROR: Random CTA start/end @ tick %d while kernel not executing" % tick
                        sys.exit(-1)
                    if ctaExecuting:
                        if ctaStartPeriod != currentPeriod:
                            print "ERROR: [%d:%d] CTA spans periods: CTA start: %d, current period: %d" % (shaderID, ctaID, ctaStartPeriod, currentPeriod)
                            sys.exit(-1)
                        if SPA_CTA_COUNT not in allStats[currentPeriod].keys():
                            allStats[currentPeriod][SPA_CTA_COUNT] = 0
                        allStats[currentPeriod][SPA_CTA_COUNT] += 1
                        if SPA_CTA_TICKS not in allStats[currentPeriod].keys():
                            allStats[currentPeriod][SPA_CTA_TICKS] = 0
                        allStats[currentPeriod][SPA_CTA_TICKS] += (ctaStats[shaderID][ctaID][-1] - ctaStats[shaderID][ctaID][-2])
                        ctaStartPeriod = -1
                    elif index == len(tokens)-1:
                        ctaStartPeriod = -1
                    else:
                        ctaStartPeriod = currentPeriod
                    ctaExecuting = not ctaExecuting
    statsFile.close()
else:
    print "WARNING: Stats file \'%s\' doesn't exist. Skipping..." % gpuStatsFilename

in_gem5_stats = False
stats_period = -1
ruby_mc_stats_time = False
ruby_mc_name = ""
if os.path.exists(statsFilename):
    statsFile = open(statsFilename, 'r')
    for line in statsFile:
        line = line.rstrip('\n')
        if 'Begin Simulation Statistics' in line:
            in_gem5_stats = True
            stats_period += 1
        elif 'End Simulation Statistics' in line:
            in_gem5_stats = False
        elif in_gem5_stats:
            tokens = line.split()
            if len(tokens) > 2:
                allStatsNames[tokens[0]] = GEM5_STAT
                allStats[stats_period][tokens[0]] = getValue(tokens[1])

        elif not in_gem5_stats:
            # Grab desired Ruby stats
            if "Memory controller:" in line or "Memory Controller:" in line:
                ruby_mc_name = line.replace("Memory controller: ","").replace("Memory Controller: ","").rstrip(":")
                if "no stats recorded." in ruby_mc_name:
                    ruby_mc_name = ruby_mc_name.replace(" no stats recorded.","")
                    ruby_mc_stats_time = False
                else:
                    ruby_mc_stats_time = True
            elif ruby_mc_stats_time:
                if line is "":
                    ruby_mc_stats_time = False
                else:
                    tokens = line.split()
                    stat_name = "%s.%s" % (ruby_mc_name, tokens[0].rstrip(":"))
                    allStatsNames[stat_name] = RUBY_STAT
                    allStats[stats_period][stat_name] = getValue(tokens[1])
            elif "system" in line and "Cache Stats:" not in line and "system_time" not in line and "_request_type_" not in line:
                tokens = line.split()
                stat_name = tokens[0].rstrip(":")
                allStatsNames[stat_name] = RUBY_STAT
                allStats[stats_period][stat_name] = getValue(tokens[1])
    statsFile.close()
else:
    print "WARNING: Stats file \'%s\' doesn't exist. Skipping..." % statsFilename

stats_period += 1

###############################################################################
# Ruby stats to periodic instead of cumulative
###############################################################################

for stat_name in allStatsNames.keys():
    if allStatsNames[stat_name] == RUBY_STAT:
        for period in range(stats_period-1,0,-1):
            if stat_name not in allStats[period-1].keys():
                allStats[period-1][stat_name] = 0
            allStats[period][stat_name] = allStats[period][stat_name] - allStats[period-1][stat_name]

###############################################################################
# Calculated stats
###############################################################################

if 'sim_seconds' in allStatsNames.keys():
    for stat_name in allStatsNames.keys():
        if 'memBuffer.memory_total_requests' in stat_name:
            new_stat = stat_name.replace('memory_total_requests', 'memory_bandwidth_demand')
            allStatsNames[new_stat] = GEM5_STAT
            for period in range(stats_period):
                if allStats[period]['sim_seconds'] == 0:
                    allStats[period][new_stat] = 0
                else:
                    allStats[period][new_stat] = cache_line_size_bytes * allStats[period][stat_name] / (allStats[period]['sim_seconds'] * 1024 * 1024 * 1024)

###############################################################################
# Output period statistics
###############################################################################

output_file = open(os.path.join(outputDir, 'stats_periods.txt'), 'w')

sorted_stats_names = sorted(allStatsNames.keys())

for stat_name in sorted_stats_names:
    cumulative_stat = 0
    stat_string = ""
    for period in range(stats_period):
        if stat_name not in allStats[period].keys():
            allStats[period][stat_name] = 0
        cumulative_stat += allStats[period][stat_name]
        stat_string = "%s,%s" % (stat_string, allStats[period][stat_name])
    stat_string = "%s,%s%s\n" % (stat_name, cumulative_stat, stat_string)
    output_file.write(stat_string)

output_file.close()

###############################################################################
# Output kernel and CTA timings
###############################################################################

output_file = open(os.path.join(outputDir, 'stats_ctas.txt'), 'w')

kc_cta_space_string = ""
kc_header_string = "Name"
spa_kernel_string = "SPA"
first_kernel_starts = 0
for period in range(stats_period):
    if period % 2:
        header_part = "Kernel"
    else:
        header_part = "Idle"
    kc_header_string = "%s,%s" % (kc_header_string, header_part)
    kc_cta_space_string = "%s," % kc_cta_space_string
    if period == 0:
        spa_kernel_string = "%s,%s" % (spa_kernel_string, 0)
        first_kernel_starts = allStats[period][PERIOD_END_TICK]
    elif period == stats_period - 1:
        spa_kernel_string = "%s,%s" % (spa_kernel_string, 0)
    else:
        spa_kernel_string = "%s,%s" % (spa_kernel_string, allStats[period][PERIOD_TICKS])

for period in range(max_cta_periods):
    if period % 2:
        header_part = "Active"
    else:
        header_part = "Idle"
    kc_header_string = "%s,%s" % (kc_header_string, header_part)

kc_header_string += "\n"
output_file.write(kc_header_string)
spa_kernel_string += "\n"
output_file.write(spa_kernel_string)

last_kernel_ends = allStats[stats_period-2][PERIOD_END_TICK]
for shader in ctaStats.keys():
    for cta in ctaStats[shader].keys():
        cta_string = "%s;%s%s" % (shader, cta, kc_cta_space_string)
        for period in range(len(ctaStats[shader][cta])):
            if period == 0:
                cta_ticks = ctaStats[shader][cta][period] - first_kernel_starts
            else:
                if ctaStats[shader][cta][period] >= last_kernel_ends:
                    cta_ticks = last_kernel_ends - ctaStats[shader][cta][period-1]
                else:
                    cta_ticks = ctaStats[shader][cta][period] - ctaStats[shader][cta][period-1]
            cta_string = "%s,%s" % (cta_string, cta_ticks)
        cta_string += "\n"
        output_file.write(cta_string)

output_file.close()
