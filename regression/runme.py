# Author: Adarsh Patil
# University : Indian Institute of Science Licence
# Freely distributable under LGPL Licence!

import os
import subprocess
import shutil
import time
import sys
try:
    import Queue as Queue
except:
    import queue as Queue
import threading

sims = Queue.Queue()

def worker():
    threadid = threading.current_thread().name
    while True:
        run_command = sims.get()
        if run_command is None:
            print ("Nothing in queue, Killing " + threadid)
            return
        print ("Got item, Remaining queue size "+ str(sims.qsize()))
        print ("")
        print ("Running benchmark..." + threadid + "****\n" + run_command + "\n")
        print ("")
        #time.sleep(5)
        sim = subprocess.Popen( run_command , stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=envs, shell=True)
        sim.wait()
        print (threadid + " COMPLETED JOB!")
        
        

## __MAIN__ ##

import argparse
parser = argparse.ArgumentParser(description='Run specified workloads parallely')

parser.add_argument('--run_mix', default="",
                   help='the list of workload mix to run separated by ;')
parser.add_argument('--threads', default=1,
                   help='number of simulations to run in parallel')
parser.add_argument('--memory', default="",
                    help='total system memory format: xG; specify only x')
parser.add_argument('--dramcache', action='store_true', default=False,
                    help='flag to run with predefined dramcache, default false')
parser.add_argument('--run', action='store_true', default=False,
                    help='flag to run, else default to checkpoint')
parser.add_argument('--fast', action='store_true', default=False,
                    help='flag to run in gem.fast')
parser.add_argument('--run_only_cpu', action='store_true', default=False,
                    help='run only cpu cores using the gpu checkpoint')


if not len(sys.argv)>1:
  print parser.print_help()
  sys.exit(1)

args = parser.parse_args()
num_threads=int(args.threads)
run_mix = args.run_mix.split(';')

hsa = '/storage/adarsh/hsa'
main_prefix = hsa + '/gem5gpu'

 
benchmarks = {
        # 4core
        '4g1' : 'bfs;soplex;bzip2;gobmk;hmmer',
        '4g2' : 'needle;cactusADM;gcc;bzip2;sphinx3',
        '4g3' : 'gaussian;mcf;libquantum;gobmk;sphinx3',
        '4g4' : 'hotspot;astar;milc;gcc;leslie3d',
        '4g5' : 'bfs;astar;milc;gobmk;bwaves',
        '4g6' : 'srad;astar;cactusADM;libquantum;sphinx3',
        '4g7' : 'streamcluster;astar;mcf;gobmk;sphinx3',
        '4g8' : 'needle;astar;mcf;gcc;bzip2',
        '4g9' : 'lud;soplex;milc;mcf;bwaves',
        '4g10': 'kmeans;astar;soplex;cactusADM;libquantum',
        '4g11': 'bfs;astar;soplex;mcf;gcc',
        '4g12': 'kmeans;milc;mcf;libquantum;bzip2',
        '4g13': 'heartwall;astar;milc;soplex;cactusADM',
        '4g14': 'gaussian;astar;mcf;soplex;cactusADM',
        '4g15': 'kmeans;bzip2;gobmk;hmmer;sphinx3',
        '4g16': 'needle;gcc;libquantum;leslie3d;bwaves',
        '4g17': 'bfs;bzip2;libquantum;hmmer;bwaves',
        '4g18': 'bfs;gcc;gobmk;leslie3d;sphinx3',
        '4g19': 'lud;astar;soplex;mcf;gcc',
        '4g20': 'lud;astar;milc;gcc;leslie3d',
        '4g21': 'lud;astar;cactusADM;libquantum;sphinx3',
        '4g22': 'lud;mcf;libquantum;bzip2;sphinx3',
        '4g23': 'gaussian;soplex;milc;cactusADM;libquantum',
        '4g24': 'gaussian;astar;mcf;leslie3d;sphinx3',
        '4g25': 'gaussian;soplex;milc;gobmk;sphinx3',
        '4g26': 'gaussian;milc;libquantum;gobmk;leslie3d',
        '4g27': 'needle;astar;milc;hmmer;sphinx3',
        '4g28': 'needle;cactusADM;soplex;bwaves;libquantum',
        '4g29': 'needle;astar;soplex;cactusADM;gcc',
        '4g30': 'heartwall;soplex;cactusADM;sphinx3;libquantum',

        # 8core
        '8g1' : 'hotspot;milc;mcf;bzip2;gcc;gobmk;libquantum;hmmer;sphinx3',
        '8g2' : 'backprop;soplex;cactusADM;libquantum;bzip2;gobmk;leslie3d;hmmer;sphinx3',
        '8g3' : 'streamcluster;astar;milc;mcf;bzip2;gcc;gobmk;libquantum;hmmer',
        '8g4' : 'gaussian;cactusADM;soplex;mcf;bzip2;gcc;leslie3d;libquantum;sphinx3',
        '8g5' : 'bfs;astar;milc;mcf;soplex;bzip2;gcc;hmmer;libquantum',
        '8g6' : 'kmeans;astar;milc;soplex;cactusADM;bzip2;sphinx3;hmmer;leslie3d',

        # 4 core only CPU
        '4c1' : 'soplex;bzip2;gobmk;hmmer',
        '4c2' : 'cactusADM;gcc;bzip2;sphinx3',
        '4c3' : 'mcf;libquantum;gobmk;sphinx3',
        '4c4' : 'astar;milc;gcc;leslie3d',
        '4c5' : 'astar;milc;gobmk;bwaves',
        '4c6' : 'astar;cactusADM;libquantum;sphinx3',
        '4c7' : 'astar;mcf;gobmk;sphinx3',
        '4c8' : 'astar;mcf;gcc;bzip2',
        '4c9' : 'soplex;milc;mcf;bwaves',
        '4c10': 'astar;soplex;cactusADM;libquantum',
        '4c11': 'astar;soplex;mcf;gcc',
        '4c12': 'milc;mcf;libquantum;bzip2',
        '4c13': 'astar;milc;soplex;cactusADM',
        '4c14': 'astar;mcf;soplex;cactusADM',
        '4c15': 'bzip2;gobmk;hmmer;sphinx3',
        '4c16': 'gcc;libquantum;leslie3d;bwaves',
        '4c17': 'bzip2;libquantum;hmmer;bwaves',
        '4c18': 'gcc;gobmk;leslie3d;sphinx3',
        '4c19': 'astar;soplex;mcf;gcc',
        '4c20': 'astar;milc;gcc;leslie3d',
        '4c21': 'astar;cactusADM;libquantum;sphinx3',
        '4c22': 'mcf;libquantum;bzip2;sphinx3',
        '4c23': 'soplex;milc;cactusADM;libquantum',
        '4c24': 'astar;mcf;leslie3d;sphinx3',
        '4c25': 'soplex;milc;gobmk;sphinx3',
        '4c26': 'milc;libquantum;gobmk;leslie3d',
        '4c27': 'astar;milc;hmmer;sphinx3',
        '4c28': 'cactusADM;soplex;bwaves;libquantum',
        '4c29': 'astar;soplex;cactusADM;gcc',
        '4c30': 'soplex;cactusADM;sphinx3;libquantum',

        # 8core only CPU
        '8c1' : 'milc;mcf;bzip2;gcc;gobmk;libquantum;hmmer;sphinx3',
        '8c2' : 'soplex;cactusADM;libquantum;bzip2;gobmk;leslie3d;hmmer;sphinx3',
        '8c3' : 'astar;milc;mcf;bzip2;gcc;gobmk;libquantum;hmmer',
        '8c4' : 'cactusADM;soplex;mcf;bzip2;gcc;leslie3d;libquantum;sphinx3',
        '8c5' : 'astar;milc;mcf;soplex;bzip2;gcc;hmmer;libquantum',
        '8c6' : 'astar;milc;soplex;cactusADM;bzip2;sphinx3;hmmer;leslie3d'

    }

# this array[workload_num] gives max insts to take checkpoint at for a CPU only
# max insts is almost same for 4 core 8G and 16G so we maintain single list
take_cpt_cpu_insts_4c = [ 9889117449,
        2712381995,
        2527811173,
        4028229395,
        9889117449,
        1952615069,
        3769966693,
        2903847752,
        7420173315,
        5960887950,
        9889117449,
        5960887950,
        0,
        2644338902,
        5960887950,
        2712381995,
    ]

# place holder to be filled
take_cpt_cpu_insts_8c = [ 9889117449,
        2712381995,
        2527811173,
        4028229395,
        9889117449,
        1952615069,
    ]

cpt_common_config = '''--work-begin-checkpoint-count=1
--max-checkpoints=1
--caches
--clusters=8
--flush_kernel_end
--access-host-pagetable
--sc_l1_size=64kB
--sc_l1_assoc=4
--sc_l2_size=512kB
--sc_l2_assoc=16
--gpu_tlb_entries=64
--gpu_tlb_assoc=4
--pwc_size=32kB
--sys-clock=2.5GHz
--cpu-clock=2.5GHz
--l1d_size=32kB
--l1d_assoc=8
--l1i_size=32kB
--l1i_assoc=8
--l2_size=256kB
--l2_assoc=8
--mem-type=DDR3_1600_x64
--dir-on
--benchmark_stdout benchstdout
--benchmark_stderr benchstderr'''

run_common_config = '''--checkpoint-restore=1
--restore-with-cpu=timing
--standard-switch=1
--warmup-insts=250000000
--caches
--clusters=8
--flush_kernel_end
--access-host-pagetable
--sc_l1_size=64kB
--sc_l1_assoc=4
--sc_l2_size=512kB
--sc_l2_assoc=16
--gpu_tlb_entries=64
--gpu_tlb_assoc=4
--pwc_size=32kB
--sys-clock=2.5GHz
--cpu-clock=2.5GHz
--l1d_size=32kB
--l1d_assoc=8
--l1i_size=32kB
--l1i_assoc=8
--l2_size=256kB
--l2_assoc=8
--mem-type=DDR3_1600_x64
--dir-on
--benchmark_stdout benchstdout
--benchmark_stderr benchstderr
--gpuapp_in_workload'''

cpt_common_config_cpu = '''--at-instruction
--max-checkpoints=1
--caches
--sys-clock=2.5GHz
--cpu-clock=2.5GHz
--l1d_size=32kB
--l1d_assoc=8
--l1i_size=32kB
--l1i_assoc=8
--l2_size=256kB
--l2_assoc=8
--mem-type=DDR3_1600_x64
--dir-on
--benchmark_stdout benchstdout
--benchmark_stderr benchstderr'''

run_common_config_cpu = '''--at-instruction
--restore-with-cpu=timing
--standard-switch=1
--warmup-insts=250000000
--caches
--sys-clock=2.5GHz
--cpu-clock=2.5GHz
--l1d_size=32kB
--l1d_assoc=8
--l1i_size=32kB
--l1i_assoc=8
--l2_size=256kB
--l2_assoc=8
--mem-type=DDR3_1600_x64
--dir-on
--benchmark_stdout benchstdout
--benchmark_stderr benchstderr'''

'''
DRAM CACHE PREDEFINED CONFIGURATION
4G - 1 cntrl - 128M dram cache
8G - 2 cntrl - 256M dram cache
16G - 4 cntrl - 512M dram cache
32G - 8 cntrl - 1G dram cache
'''

results_dir = main_prefix + '/results'




## Setup environment variables
envs = os.environ.copy()
cudahome = hsa + '/cuda'

envs['CUDAHOME'] = cudahome
envs['PATH'] = envs['PATH'] + ':'+cudahome+'/bin:/opt/gcc4.4.6/bin'
envs['LD_LIBRARY_PATH']= hsa+'/gcc44/gmp-4.1-build/lib:'+hsa+'/gcc44/mpfr-2.3.2-build/lib:'+cudahome+'/lib64:'+cudahome+'/lib'
envs['NVIDIA_CUDA_SDK_LOCATION'] = hsa+'/NVIDIA_GPU_Computing_SDK/C'

# form command to be executed
if args.fast:
    command = main_prefix + '''/gem5/build/X86_VI_hammer_GPU/gem5.fast -r -e --outdir='''
else:
    command = main_prefix + '''/gem5/build/X86_VI_hammer_GPU/gem5.opt -r -e --outdir='''

cpt_run_suffix = ''
final_config = ''
take_cpt_insts = 0

for benchmark in run_mix:
    print ("Got benchmark " + benchmark)
    if benchmark[1] == 'g':
        if benchmark[0] == '4':
            num_cores = 5
            max_insts = 250000000
        else:
            num_cores = 9
            max_insts = 125000000

    elif benchmark[1] == 'c':
        if benchmark[0] == '4':
            num_cores = 4
            max_insts = 250000000
            take_cpt_insts = take_cpt_cpu_insts_4c[int(benchmark[2:])-1]
        else:
            num_cores = 8
            max_insts = 125000000
            take_cpt_insts = take_cpt_cpu_insts_8c[int(benchmark[2:])-1]

    if args.dramcache and (int(args.memory) == 8):
        suite_results_dir = results_dir + '/' + benchmark[0] +'core-8G-256ML3'
    elif args.dramcache and (int(args.memory) == 16):
        suite_results_dir = results_dir + '/' + benchmark[0] + 'core-16G-512ML3'
    else:
        suite_results_dir = results_dir + '/' + benchmark[0] + 'core-' + args.memory + 'G-noL3'

    # create results dir /results/suite_dir/benchmarks_dir/(files)
    if not os.path.exists(suite_results_dir):
        os.makedirs(suite_results_dir)

    ## 4gX benchmarks
    if benchmark[1] == 'g':
        if args.run:
            cpt_run_suffix = '.run'
            final_config = run_common_config + '\n--checkpoint-dir=' + results_dir + '/cpt/' + benchmark[0] + 'core-' + \
             args.memory + 'G/' + benchmark + '.cpt'
            final_config = final_config + '\n--maxinsts=' + str(max_insts)
        elif args.run_only_cpu:
            cpt_run_suffix = '.cpurun'
            final_config = run_common_config + '\n--checkpoint-dir=' + results_dir + '/cpt/' + benchmark[0] + 'core-' + \
             args.memory + 'G/' + benchmark + '.cpt'
            final_config = final_config + '\n--maxinsts=' + str(max_insts)
            final_config = final_config + '\n--run_only_cpu'
        else:
            cpt_run_suffix = '.cpt'
            final_config = cpt_common_config

    ## 4cX benchmarks
    elif benchmark[1] == 'c':
        if args.run:
            cpt_run_suffix = '.run'
            final_config = run_common_config_cpu + '\n--checkpoint-dir=' + results_dir + '/cpt/' + benchmark[0] + 'core-' + \
             args.memory + 'G/' + benchmark + '.cpt'
            final_config = final_config + '\n--maxinsts=' + str(max_insts) + '\n--checkpoint-restore=' + str(take_cpt_insts)
        else:
            cpt_run_suffix = '.cpt'
            final_config = '--take-checkpoint=' + str(take_cpt_insts) + '\n' + cpt_common_config_cpu

    if not os.path.exists(suite_results_dir+'/'+benchmark + cpt_run_suffix):
        os.makedirs(suite_results_dir+'/'+benchmark + cpt_run_suffix)
    else:
        if os.path.exists(suite_results_dir+'/'+benchmark + cpt_run_suffix +'.bkup'):
            shutil.rmtree(suite_results_dir+'/'+benchmark + cpt_run_suffix +'.bkup')
        shutil.move(suite_results_dir+'/'+benchmark + cpt_run_suffix, suite_results_dir+'/'+benchmark + cpt_run_suffix +'.bkup')
        os.makedirs(suite_results_dir+'/'+benchmark + cpt_run_suffix)

    if args.memory == '8':
        final_config = final_config + '\n--total-mem-size=8GB\n--num-dirs=2'

    if args.memory == '16':
        final_config = final_config + '\n--total-mem-size=16GB\n--num-dirs=4'

    if args.dramcache:
        final_config = final_config + '\n--dramcache'
        if (args.run or args.run_only_cpu):
            final_config = final_config + '\n--dramcache_timing'

    final_config = final_config + '\n--num-cpus=' + str(num_cores)
    final_config = final_config + '\n' + '--benchmark ' + benchmarks[benchmark]

    config_file_fp = open(suite_results_dir+'/'+benchmark+cpt_run_suffix+'/myconfig', "a")
    config_file_fp.write(final_config)
    config_file_fp.close()

    run_command = command + suite_results_dir + '/' + benchmark + cpt_run_suffix + \
                ' ' + main_prefix +'/gem5-gpu/configs/se_fusion_mine.py `cat '+ \
                suite_results_dir+'/'+benchmark+cpt_run_suffix+'/myconfig' + '`'

    sims.put(run_command)

print ("intial queue size " + str(sims.qsize()))
threads = [ threading.Thread(target=worker) for _i in range(num_threads) ]
for thread in threads:
    thread.start()
    sims.put(None)  # one EOF marker for each thread
