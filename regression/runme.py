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
        print "Running benchmark..." + threadid + "****\n" + run_command
        print ""
        #time.sleep(5)
        sim = subprocess.Popen( run_command , stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=envs, shell=True)
        sim.wait()
        print threadid + " Completed job!"
        
        

## __MAIN__ ##
hsa = '/storage/adarsh/hsa'
main_prefix = hsa + '/gem5gpu'

num_threads = 8
 
benchmarks_prefix = main_prefix + '/benchmarks'
spec_prefix = benchmarks_prefix + '/cpu2006'
rodinia_prefix = benchmarks_prefix + '/rodinia-nocopy'

m1 = (spec_prefix + '/milc/milc_base.amd64',
      spec_prefix + '/mcf/mcf_base.amd64',
      spec_prefix + '/omnetpp/omnetpp_base.amd64', 
      spec_prefix + '/gcc/gcc_base.amd64',
      rodinia_prefix + '/bin/gem5_fusion_bfs') + tuple([spec_prefix]*9) + (rodinia_prefix,)

m2 = (spec_prefix + '/GemsFDTD/GemsFDTD_base.amd64',
      spec_prefix + '/leslie3d/leslie3d_base.amd64',
      spec_prefix + '/xalancbmk/Xalan_base.amd64', 
      spec_prefix + '/soplex/soplex_base.amd64',
      rodinia_prefix + '/bin/gem5_fusion_lud') + tuple([spec_prefix]*8) + (rodinia_prefix,)

m4 = (spec_prefix + '/lbm/lbm_base.amd64',
      spec_prefix + '/bwaves/bwaves_base.amd64',
      spec_prefix + '/zeusmp/zeusmp_base.amd64', 
      spec_prefix + '/mcf/mcf_base.amd64',
      rodinia_prefix + '/bin/gem5_fusion_kmeans') + tuple([spec_prefix]*7) + (rodinia_prefix,)

m5 = (spec_prefix + '/soplex/soplex_base.amd64',
      spec_prefix + '/bzip2/bzip2_base.amd64',
      spec_prefix + '/gobmk/gobmk_base.amd64', 
      spec_prefix + '/hmmer/hmmer_base.amd64',
      rodinia_prefix + '/bin/gem5_fusion_streamcluster') + tuple([spec_prefix]*9)
      
m6 = (spec_prefix + '/milc/milc_base.amd64',
      spec_prefix + '/GemsFDTD/GemsFDTD_base.amd64',
      spec_prefix + '/cactusADM/cactusADM_base.amd64', 
      spec_prefix + '/lbm/lbm_base.amd64',
      rodinia_prefix + '/lbm/gem5_fusion_backprop') + tuple([spec_prefix]*7)
      
m7 = (spec_prefix + '/mcf/mcf_base.amd64',
      spec_prefix + '/omnetpp/omnetpp_base.amd64',
      spec_prefix + '/soplex/soplex_base.amd64', 
      spec_prefix + '/leslie3d/leslie3d_base.amd64',
      rodinia_prefix + '/lbm/gem5_fusion_mummer') + tuple([spec_prefix]*8) + tuple([rodinia_prefix]*2)
      
m8 = (spec_prefix + '/bwaves/bwaves_base.amd64',
      spec_prefix + '/gobmk/gobmk_base.amd64',
      spec_prefix + '/zeusmp/zeusmp_base.amd64',
      spec_prefix + '/gcc/gcc_base.amd64',
      rodinia_prefix + '/lbm/gem5_fusion_needle') + tuple([spec_prefix]*7) 
            
benchmarks = {
        '4cg_mix1' :
         '''--cmd "%s;%s;%s;%s;%s" \
         --input "%s/milc/su3imp.in;;;;" \
         --output "%s/milc/speccmds.out;%s/mcf/inp.out;%s/omnetpp/omnetpp.log;%s/gcc/gcc.ref.g23.out;" \
         --options ";%s/mcf/inp.in;%s/omnetpp/omnetpp.ini;%s/gcc/g23.i -o %s/gcc/g23.s;%s/inputs/bfs/graph4096.txt" \
         ''' % m1,
              
        '4cg_mix2' :
         '''--cmd "%s;%s;%s;%s;%s" \
         --input ";%s/leslie3d/leslie3d.in;;;" \
         --output "%s/GemsFDTD/GemsFDTD.ref.out;%s/leslie3d/leslie3d.ref.out;%s/xalancbmk/xalancbmk.ref.out;%s/soplex/soplex.ref.ref.out;" \
         --options ";;-v %s/xalancbmk/t5.xml %s/xalancbmk/xalanc.xsl;-m3500 %s/soplex/ref.mps;%s/inputs/lud/1024.dat" \
         ''' % m2,
              
        '4cg_mix4' :
         '''--cmd "%s;%s;%s;%s;%s" \
         --input ";;;;" \
         --output "%s/lbm/lbm.ref.out;%s/bwaves/bwaves.ref.out;%s/zeusmp/zeusmp.ref.out;%s/mcf/mcf.ref.out;" \
         --options "3000 %s/reference.dat 0 0 %s/100_100_130_ldc.of;;;%s/inp.in;%s/inputs/kmeans/kdd_cup" \
         ''' % m4,
         
         '4cg_mix5' :
         '''--cmd "%s;%s;%s;%s;%s" \
         --input ";;%s/gobmk/score2.tst;;" \
         --output "%s/soplex/soplex.ref.ref.out;%s/bzip2/bzip2.ref.liberty.out;%s/gobmk/gobmk.ref.score2.out;%s/hmmer/hmmer.ref.nph3.out;" \
         --options  "-m3500 %s/soplex/ref.mps;%s/bzip2/liberty.jpg 30;--quiet --mode gtp;%s/hmmer/nph3.hmm %s/hmmer/swiss41;10 20 256 65536 65536 1000 none output.txt 1" \
         ''' % m5,
         
         '4cg_mix6' :
         '''--cmd "%s;%s;%s;%s;%s" \
         --input "%s/milc/su3imp.in;;;;" \
         --output "%s/milc/milc.ref.out;%s/GemsFDTD/GemsFDTD.ref.out;%s/cactusADM/cactusADM.ref.out;;" \
         --options  ";;%s/cactusADM/benchADM.par;3000 %s/lbm/reference.dat 0 0 %s/lbm/100_100_130_ldc.of;65536" \
         ''' % m6,
         
         '4cg_mix7' :
         '''--cmd "%s;%s;%s;%s;%s" \
         --input ";;;%s/leslie3d/leslie3d.in;" \
         --output "%s/mcf/mcf.ref.out;%s/omnetpp/omnetpp.ref.log;%s/soplex/soplex.ref.ref.out;%s/leslie3d/leslie3d.ref.out;" \
         --options  "%s/mcf/inp.in;%s/omnetpp/omnetpp.ini;-m3500 %s/soplex/ref.mps;;-l 100 %s/mummergpu/input/cbrigg_ref.fna %s/mummergpu/input/cbrigg_reads.fasta" \
         ''' % m7,
         
         '4cg_mix8' :
         '''--cmd "%s;%s;%s;%s;%s" \
         --input ";%s/gobmk/score2.tst;;;" \
         --output "%s/bwaves/bwaves.ref.out;%s/gobmk/gobmk.ref.score2.out;%s/zeusmp/zeusmp.ref.out;%s/gcc/gcc.ref.g23.out;" \
         --options  ";--quiet --mode gtp;;%s/gcc/g23.i -o %s/gcc/g23.s;-x 128 -y 128 -z 10 -np 1000" \
         ''' % m8
    }

results_dir = main_prefix + '/results'
suite_results_dir = results_dir + '/4core-4GB-2cntrl-noL3'
suite_config = suite_results_dir + '/config' 

# make results dir results -> suite_dir -> benchmarks_dir -> (files)
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
    
    # write benchmark specific config into benchmark_config file
    #benchmark_config = suite_results_dir+'/'+benchmark+'/benchmark_config'
    #benchmark_config_fp = open(benchmark_config,'w')
    #benchmark_config_fp.write(benchmarks[benchmark])
    #benchmark_config_fp.close()
    
    #print command + suite_results_dir + '/' + benchmark + ' ' + main_prefix \
    #        +'/gem5-gpu/configs/se_fusion_mine.py `cat '+ suite_config+'`  '+ benchmarks[benchmark]
    
    run_command = command + suite_results_dir + '/' + benchmark + ' ' + main_prefix +'/gem5-gpu/configs/se_fusion_mine.py `cat '+ \
                suite_config+'`  '+ benchmarks[benchmark]
    sims.put(run_command)
    
print "intial queue size " + str(sims.qsize())
threads = [ threading.Thread(target=worker) for _i in range(num_threads) ]
for thread in threads:
    thread.start()
    sims.put(None)  # one EOF marker for each thread
