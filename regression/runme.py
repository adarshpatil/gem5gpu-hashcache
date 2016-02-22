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
hsa = '/storage/adarsh/hsa'
main_prefix = hsa + '/gem5gpu'

num_threads = 8
 
benchmarks = {
        # CPU - GPU workload mix
        '4cg_mix1' : 'milc;mcf;omnetpp;gcc;bfs',
        '4cg_mix2' : 'GemsFDTD;leslie3d;xalancbmk;soplex;lud',
        '4cg_mix3' : 'cactusADM;libquantum;tonto;sphinx3;particlefilter_naive',
        '4cg_mix4' : 'lbm;bwaves;zeusmp;mcf;kmeans',
        '4cg_mix5' : 'soplex;bzip2;gobmk;hmmer;gaussian',
        '4cg_mix6' : 'milc;GemsFDTD;cactusADM;lbm;backprop',
        '4cg_mix7' : 'mcf;omnetpp;soplex;leslie3d;streamcluster',
        '4cg_mix8' : 'bwaves;gobmk;zeusmp;gcc;needle',
        '4cg_mix9' : 'gcc;milc;astar;leslie3d;hotspot',
        '4cg_mix10': 'astar;bzip2;sphinx3;xalancbmk;heartwall',

        # Only CPU workloads mixes
        '4c_mix1' : 'milc;mcf;omnetpp;gcc',
        '4c_mix2' : 'GemsFDTD;leslie3d;xalancbmk;soplex',
        '4c_mix3' : 'cactusADM;libquantum;tonto;sphinx3',
        '4c_mix4' : 'lbm;bwaves;zeusmp;mcf',
        '4c_mix5' : 'soplex;bzip2;gobmk;hmmer',
        '4c_mix6' : 'milc;GemsFDTD;cactusADM;lbm',
        '4c_mix7' : 'mcf;omnetpp;soplex;leslie3d',
        '4c_mix8' : 'bwaves;gobmk;zeusmp;gcc',
        '4c_mix9' : 'gcc;milc;astar;leslie3d',
        '4c_mix10': 'astar;bzip2;sphinx3;xalancbmk',
        'n1' : '462;459;470;433',
        'n2' : '429;462;471;473',
        'n3' : '410;473;445;433',
        'n4' : '462;459;445;410',
        'n5' : '429;456;450;459',
        'n6' : '403;445;459;462',
        'n7' : '403;433;470;410'
    }

results_dir = main_prefix + '/results'
suite_results_dir = results_dir + '/4core-8GB-2cntrl-noL3'
suite_config = suite_results_dir + '/config' 

# create results dir /results/suite_dir/benchmarks_dir/(files)
if not os.path.exists(suite_results_dir):
    os.makedirs(suite_results_dir)

common_config_file = main_prefix + '/regression/4core-8G-common_config'

suite_options = '''--l3_size=4096kB --l3_assoc=16 --num-dirs=2 '''

# create run config from common and suite options and write to  config
shutil.copyfile(common_config_file, suite_config)

run_config_fp = open(suite_config,"a")
run_config_fp.write(suite_options)
run_config_fp.close()


## Setup environment variables
envs = os.environ.copy()
cudahome = hsa + '/cuda'

envs['CUDAHOME'] = cudahome
envs['PATH'] = envs['PATH'] + ':'+cudahome+'/bin:/opt/gcc4.4.6/bin'
envs['LD_LIBRARY_PATH']= hsa+'/gcc44/gmp-4.1-build/lib:'+hsa+'/gcc44/mpfr-2.3.2-build/lib:'+cudahome+'/lib64:'+cudahome+'/lib'
envs['NVIDIA_CUDA_SDK_LOCATION'] = hsa+'/NVIDIA_GPU_Computing_SDK/C'

# form command to be executed
command = '''build/X86_VI_hammer_GPU/gem5.opt -r --stdout-file=stdout -e --stderr-file=stderr --outdir '''

import argparse
parser = argparse.ArgumentParser(description='Run specified workloads parallely')

parser.add_argument('--run_mix', default="",
                   help='the list of workload mix to run separated by ;')
parser.add_argument('--threads', default=1,
                   help='number of simulations to run in parallel')

args = parser.parse_args()
num_threads=int(args.threads)
run_mix = args.run_mix.split(';')

for benchmark in run_mix:
    print ("Got benchmark " + benchmark)
    if not os.path.exists(suite_results_dir+'/'+benchmark):
        os.makedirs(suite_results_dir+'/'+benchmark)
    else:
        if os.path.exists(suite_results_dir+'/'+benchmark+'.bkup'):
            shutil.rmtree(suite_results_dir+'/'+benchmark+'.bkup')
        shutil.move(suite_results_dir+'/'+benchmark, suite_results_dir+'/'+benchmark+'.bkup')
        os.makedirs(suite_results_dir+'/'+benchmark)
    
    run_command = command + suite_results_dir + '/' + benchmark + ' ' + main_prefix +'/gem5-gpu/configs/se_fusion_mine.py `cat '+ \
                suite_config+'`  --benchmark "'+ benchmarks[benchmark] + '"' + \
                ' --benchmark_stdout ' + suite_results_dir + '/' + benchmark + '/bench-stdout' + \
                ' --benchmark_stderr ' + suite_results_dir + '/' + benchmark + '/bench-stderr'
    sims.put(run_command)
    
print ("intial queue size " + str(sims.qsize()))
threads = [ threading.Thread(target=worker) for _i in range(num_threads) ]
for thread in threads:
    thread.start()
    sims.put(None)  # one EOF marker for each thread
