# Author: Adarsh Patil
# University : Indian Institute of Science Licence
# Freely distributable under LGPL Licence!

import os
import subprocess
import shutil
import time
import Queue
import threading

sims = Queue.Queue()

def worker():
    threadid = threading.current_thread().name
    while True:
        run_command = sims.get()
        if run_command is None:
            print "Nothing in queue, Killing " + threadid
            return
        print "Got item, Remaining queue size "+ str(sims.qsize())
        print ""
        print "Running benchmark..." + threadid + "****\n" + run_command + "\n"
        print ""
        time.sleep(5)
        #sim = subprocess.Popen( run_command , stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=envs, shell=True)
        #sim.wait()
        print threadid + " Completed job!"
        
        

## __MAIN__ ##
hsa = '/home/isis/hsa'
main_prefix = hsa + '/gem5gpu'

num_threads = 8
 
benchmarks = {
        '4cg_mix1' : 'milc;mcf;omnetpp;gcc;bfs',
        '4cg_mix2' : 'GemsFDTD;leslie3d;xalancbmk;soplex;lud',
        '4cg_mix3' : 'cactusADM;libquantum;tonto;sphinx3;particlefilter_naive',
        '4cg_mix4' : 'lbm;bwaves;zeusmp;mcf;kmeans',
        '4cg_mix5' : 'soplex;bzip2;gobmk;gaussian;',
        '4cg_mix6' : 'milc;GemsFDTD;cactusADM;lbm;backprop',
        '4cg_mix7' : 'mcf;omnetpp;soplex;leslie3d;hotspot',
        '4cg_mix8' : 'bwaves;gobmk;zeusmp;gcc;needle'
    }

results_dir = main_prefix + '/results'
suite_results_dir = results_dir + '/4core-4GB-2cntrl-noL3'
suite_config = suite_results_dir + '/config' 

# create results dir /results/suite_dir/benchmarks_dir/(files)
if not os.path.exists(suite_results_dir):
    os.makedirs(suite_results_dir)

common_config_file = main_prefix + '/regression/4core-4G-common_config'

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

    
for benchmark in benchmarks.keys():
    if not os.path.exists(suite_results_dir+'/'+benchmark):
        os.makedirs(suite_results_dir+'/'+benchmark)
    else:
        if os.path.exists(suite_results_dir+'/'+benchmark+'.bkup'):
            shutil.rmtree(suite_results_dir+'/'+benchmark+'.bkup')
        shutil.move(suite_results_dir+'/'+benchmark, suite_results_dir+'/'+benchmark+'.bkup')
        os.makedirs(suite_results_dir+'/'+benchmark)
    
    run_command = command + suite_results_dir + '/' + benchmark + ' ' + main_prefix +'/gem5-gpu/configs/se_fusion_mine.py `cat '+ \
                suite_config+'`  --benchmark '+ benchmarks[benchmark] + \
                ' --benchmark_stdout ' + suite_results_dir + '/' + benchmark + '/bench-stdout'
                ' --benchmark_stderr ' + suite_results_dir + '/' + benchmark + '/bench-stderr'
    sims.put(run_command)
    
print "intial queue size " + str(sims.qsize())
threads = [ threading.Thread(target=worker) for _i in range(num_threads) ]
for thread in threads:
    thread.start()
    sims.put(None)  # one EOF marker for each thread
