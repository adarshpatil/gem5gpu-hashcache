# Author: Adarsh Patil
# University : Indian Institute of Science Licence
# Freely distributable under LGPL Licence!

import os
import subprocess
import shutil
import time
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

        # 8core
        '8g1' : 'hotspot;milc;mcf;bzip2;gcc;gobmk;libquantum;hmmer;sphinx3',
        '8g2' : 'backprop;soplex;cactusADM;libquantum;bzip2;gobmk;leslie3d;hmmer;sphinx3',
        '8g3' : 'streamcluster;astar;milc;mcf;bzip2;gcc;gobmk;libquantum;hmmer',
        '8g4' : 'gaussian;cactusADM;soplex;mcf;bzip2;gcc;leslie3d;libquantum;sphinx3',
        '8g5' : 'bfs;astar;milc;mcf;soplex;bzip2;gcc;hmmer;libquantum',
        '8g6' : 'kmeans;astar;milc;soplex;cactusADM;bzip2;sphinx3;hmmer;leslie3d'
    }

cpt_common_config = '''--work-begin-checkpoint-count=1
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
--warmup-insts=500000
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
command = main_prefix + '''/gem5/build/X86_VI_hammer_GPU/gem5.opt -r -e --outdir='''

cpt_run_suffix = ''
final_config = ''

for benchmark in run_mix:
    print ("Got benchmark " + benchmark)
    if benchmark[0] == '4':
        num_cores = 5
        max_insts = 250000000
    else:
        num_cores = 9
        max_insts = 125000000

    if args.dramcache and int(args.memory) == '8':
        suite_results_dir = results_dir + '/' + benchmark[0] +'core-8G-256ML3'
    if args.dramcache and int(args.memory) == '16':
        suite_results_dir = results_dir + '/' + benchmark[0] + 'core-16G-512ML3'
    else:
        suite_results_dir = results_dir + '/4core-' + args.memory + 'G-noL3'

    # create results dir /results/suite_dir/benchmarks_dir/(files)
    if not os.path.exists(suite_results_dir):
        os.makedirs(suite_results_dir)

    if args.run:
        cpt_run_suffix = '.run'
        final_config = run_common_config + '\n--checkpoint-dir=' + suite_results_dir + '/'+ benchmark + '.cpt'
    else:
        cpt_run_suffix = '.cpt'
        final_config = cpt_common_config


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

    final_config = final_config + '\n--num-cpus=' + str(num_cores)
    final_config = final_config + '\n--maxinsts=' + std(max_insts)
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
