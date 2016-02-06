import os
import optparse
import sys
from os.path import join as joinpath

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, fatal

base_dir = '/storage/adarsh/hsa/gem5gpu/benchmarks/' 
binary_dir = base_dir + 'cpu2006/'
my_suffix = '_base.amd64'
data_dir= base_dir + 'cpu2006/'
#binary_dir = '/proj/sdsdata/sds_c6x_data/workArea/nagendra/spec2006/alpha_binaries/spec2006/'
#data_dir = '/proj/sdsdata/sds_c6x_data/workArea/nagendra/spec2006/alpha_binaries/spec2006/'
#binary_dir_suffix = '/'

#401.bzip2
bzip2 = LiveProcess()
bzip2.executable = binary_dir+'401.bzip2/bzip2'
bzip2.cwd = binary_dir+'401.bzip2'
data='liberty.jpg'
#data=bzip2.cwd+'input.program'
bzip2.cmd = [bzip2.executable] + [data, '30']

#410.bwaves
bwaves = LiveProcess()
bwaves.executable =  binary_dir+'410.bwaves/bwaves'
bwaves.cmd = [bwaves.executable]
bwaves.cwd = binary_dir+'410.bwaves/'

#416.gamess
gamess = LiveProcess()
gamess.executable =  binary_dir+'416.gamess/gamess'
gamess.cwd = binary_dir+'416.gamess'
gamess.cmd = [gamess.executable]
gamess.input=gamess.cwd+'cytosine.2.config'
#gamess.input=gamess.cwd+'exam29.config

#429.mcf
mcf = LiveProcess()
mcf.executable =  binary_dir+'429.mcf/mcf'
data=data_dir+'429.mcf/inp.in'
mcf.cmd = [mcf.executable] + [data]
mcf.cwd = binary_dir+'429.mcf'

#433.milc
milc=LiveProcess()
milc.executable = binary_dir+'433.milc/milc'
milc.cwd=binary_dir+'433.milc'
stdin=data_dir+'433.milc/su3imp.in'
milc.cmd = [milc.executable]
milc.input=stdin

#434.zeusmp
zeusmp=LiveProcess()
zeusmp.executable =  binary_dir+'434.zeusmp/zeusmp'
zeusmp.cmd = [zeusmp.executable]
zeusmp.output = 'zeusmp.stdout'
zeusmp.cwd =  binary_dir+'434.zeusmp'

#435.gromacs
gromacs = LiveProcess()
gromacs.executable = binary_dir+'435.gromacs/gromacs'
gromacs.cwd = binary_dir+'435.gromacs'
gromacs.cmd = [gromacs.executable] + ['-silent','-deffnm','gromacs','-nice','0']

#436.cactusADM
cactusADM = LiveProcess()
cactusADM.executable = binary_dir +'436.cactusADM/cactusADM'
cactusADM.cwd = binary_dir+'436.cactusADM'
data='benchADM.par'
#data=cactusADM.cwd+'benchADM.par'
cactusADM.cmd = [cactusADM.executable] + [data]

#437.leslie3d
leslie3d=LiveProcess()
leslie3d.executable =  binary_dir+'437.leslie3d/leslie3d'
stdin=data_dir+'437.leslie3d/leslie3d.in'
leslie3d.cmd = [leslie3d.executable]
leslie3d.input=stdin
leslie3d.output='leslie3d.stdout'
leslie3d.cwd =  binary_dir+'437.leslie3d'

#444.namd
namd = LiveProcess()
namd.executable =  binary_dir+'444.namd/namd'
namd.cwd =  binary_dir+'444.namd'
input=data_dir+'444.namd/namd.input'
namd.cmd = [namd.executable] + ['--input',input,'--iterations','38','--output','namd.out']
namd.output='namd.stdout'

#445.gobmk
gobmk=LiveProcess()
gobmk.executable =  binary_dir+'445.gobmk/gobmk'
gobmk.cwd =  binary_dir+'445.gobmk'
stdin=data_dir+'445.gobmk/score2.tst'
gobmk.cmd = [gobmk.executable]+['--quiet','--mode','gtp']
gobmk.input=stdin
gobmk.output='score2.out'

#447.dealII
dealII=LiveProcess()
dealII.executable =  binary_dir+'447.dealII/dealII'
dealII.cwd = binary_dir+'447.dealII'
dealII.cmd = [dealII.executable]+['23']

#450.soplex
soplex=LiveProcess()
soplex.executable =  binary_dir+'450.soplex/soplex'
soplex.cwd =  binary_dir+'450.soplex'
data=data_dir+'450.soplex/ref.mps'
soplex.cmd = [soplex.executable]+['-m3500',data]
soplex.output = 'ref.out'

#453.povray
povray=LiveProcess()
povray.executable =  binary_dir+'453.povray/povray'
povray.cwd =  binary_dir+'453.povray'
data=data_dir+'453.povray/SPEC-benchmark-ref.ini'
#povray.cmd = [povray.executable]+['SPEC-benchmark-ref.ini']
povray.cmd = [povray.executable]+[data]
povray.output = 'SPEC-benchmark-test.stdout'

#454.calculix
calculix=LiveProcess()
calculix.executable =  binary_dir+'454.calculix/calculix'
calculix.cwd = binary_dir+'454.calculix'
data='hyperviscoplastic'
calculix.cmd = [calculix.executable]+['-i',data]

#456.hmmer
hmmer=LiveProcess()
hmmer.executable =  binary_dir+'456.hmmer/hmmer'
hmmer.cwd =  binary_dir+'456.hmmer'
data=data_dir+'456.hmmer/nph3.hmm'
hmmer.cmd = [hmmer.executable]+[data,'swiss41']
hmmer.output = 'nph3.out'

#458.sjeng
sjeng=LiveProcess()
sjeng.executable =  binary_dir+'458.sjeng/sjeng'
sjeng.cwd =  binary_dir+'458.sjeng'
data=data_dir+'458.sjeng/ref.txt'
sjeng.cmd = [sjeng.executable]+[data]
sjeng.output = 'sjeng.out'

#459.GemsFDTD
GemsFDTD=LiveProcess()
GemsFDTD.executable =  binary_dir+'459.GemsFDTD/GemsFDTD'
GemsFDTD.cwd = binary_dir+'459.GemsFDTD'
GemsFDTD.cmd = [GemsFDTD.executable]
GemsFDTD.output = 'Gems.log'

#462.libquantum
libquantum=LiveProcess()
libquantum.executable =  binary_dir+'462.libquantum/libquantum'
libquantum.cwd=binary_dir+'462.libquantum'
libquantum.cmd = [libquantum.executable],'1397','8'
libquantum.output = 'libquantum.out'
# libquantum had 1 2 3 4

#464.h264ref
h264ref=LiveProcess()
h264ref.executable =  binary_dir+'464.h264ref/h264ref'
h264ref.cwd=binary_dir+'464.h264ref'
data=data_dir+'464.h264ref/sss_encoder_main.cfg'
h264ref.cmd = [h264ref.executable]+['-d',data]
h264ref.output = 'sss_encoder_main.out'

#465.tonto
tonto=LiveProcess()
tonto.executable = binary_dir+'465.tonto/tonto'
tonto.cwd = binary_dir+'465.tonto'
tonto.cmd = [tonto.executable]

#470.lbm
lbm=LiveProcess()
lbm.executable =  binary_dir+'470.lbm/lbm'
lbm.cwd=binary_dir+'470.lbm'
data=data_dir+'470.lbm/100_100_130_ldc.of'
lbm.cmd = [lbm.executable]+['3000', 'reference.dat', '0', '0' ,data]
lbm.output = 'lbm.out'

#471.omnetpp
omnetpp=LiveProcess()
omnetpp.executable =  binary_dir+'471.omnetpp/omnetpp'
omnetpp.cwd =  binary_dir+'471.omnetpp'
data=data_dir+'471.omnetpp/omnetpp.ini'
#data='omnetpp.ini'
omnetpp.cmd = [omnetpp.executable]+[data]
omnetpp.output = 'omnetpp.log'

#473.astar
astar=LiveProcess()
astar.cwd =  binary_dir+'473.astar'
astar.executable =  binary_dir+'473.astar/astar'
#data = [data_dir]+['473.astar/rivers.cfg']
data = ['BigLakes2048.cfg']
astar.cmd = [astar.executable]+['BigLakes2048.cfg']
astar.output = 'BigLakes2048.cfg'

#481.wrf
wrf=LiveProcess()
wrf.executable =  binary_dir+'481.wrf'+'wrf'
wrf.cwd = binary_dir+'481.wrf'
wrf.cmd = [wrf.executable]

#482.sphinx
sphinx3=LiveProcess()
sphinx3.executable =  binary_dir+'482.sphinx3/sphinx_livepretend'
sphinx3.cwd = binary_dir+'482.sphinx3'
sphinx3.cmd = [sphinx3.executable]+['ctlfile', '.', 'args.an4']

#403.gcc
gcc=LiveProcess()
gcc.executable = binary_dir + '403.gcc/gcc'
gcc.cwd = binary_dir + '403.gcc'
gcc.cmd = [gcc.executable] + ['g23.i', '-o', 'g23.s']

#400.perlbench
perlbench=LiveProcess()
perlbench.executable = binary_dir + '400.perlbench/perlbench'
perlbench.cwd = binary_dir + '400.perlbench'
perlbench.cmd = [perlbench.executable] + ['-I./lib', 'splitmail.pl', '1600', '12', '26', '16', '4500']

#483.xalancbmk
xalancbmk=LiveProcess()
xalancbmk.executable = binary_dir + '483.xalancbmk/Xalan'
xalancbmk.cwd = binary_dir + '483.xalancbmk'
xalancbmk.cmd = [xalancbmk.executable] + ['-v','t5.xml','xalanc.xsl']


############## Rodinia benchmarks ##############

binary_dir = base_dir + 'rodinia-nocopy/bin/'
data_dir = base_dir + 'rodinia-nocopy/inputs/'

# backprop
backprop = LiveProcess()
backprop.executable = binary_dir + 'gem5_fusion_backprop'
backprop.cmd = [backprop.executable] + ['65536']

# bfs
bfs = LiveProcess()
bfs.executable = binary_dir + 'gem5_fusion_bfs'
data = data_dir + 'bfs/graph1MW_6.txt'
bfs.cmd = [bfs.executable] + [data]

# hotspot
hotspot = LiveProcess()
hotspot.executable = binary_dir + 'gem5_fusion_hotspot'
data = data_dir + 'hotspot/temp_1024'
data1 = data_dir + 'hotspot/power_1024'
hotspot.cmd = [hotspot.executable] + ['1024', '2', '2'] + [data] + [data1]

# kmeans
kmeans = LiveProcess()
kmeans.executable = binary_dir + 'gem5_fusion_kmeans'
data = data_dir + 'kmeans/kdd_cup'
kmeans.cmd = [kmeans.executable] + ['-o', '-i'] + [data]

# mummergpu - not working
mummer = LiveProcess()
mummer.executable = binary_dir + 'gem5_fusion_mummer'
data = data_dir + 'mummergpu/lmono'

# needle
needle = LiveProcess()
needle.executable = binary_dir + 'gem5_fusion_needle'
needle.cmd = [needle.executable] + ['2048', '10']

# streamcluster
streamcluster = LiveProcess()
streamcluster.executable = binary_dir + 'gem5_fusion_streamcluster'
streamcluster.cmd = [streamcluster.executable] + ['10', '20', '256', '65536', '65536', '1000', 'none', 'output.txt', '1']

# lud
lud = LiveProcess()
lud.executable = binary_dir + 'gem5_fusion_lud'
data = data_dir + 'lud/2048.dat'
lud.cmd = [lud.executable] + ['-i'] + [data]


# gaussian
gaussian = LiveProcess()
gaussian.executable = binary_dir + 'gem5_fusion_gaussian'
data = data_dir + 'gaussian/matrix1024.txt'
gaussian.cmd = [gaussian.executable] + [data]

# particlefilter_naive
particlefilter_naive = LiveProcess()
particlefilter_naive.executable = binary_dir + 'gem5_fusion_particlefilter_naive'
particlefilter_naive.cmd = [particlefilter_naive.executable] + ['-x', '128', '-y', '128', '-z', '10', '-np', '1000']

# particlefilter_float
particlefilter_float = LiveProcess()
particlefilter_float.executable = binary_dir + 'gem5_fusion_particlefilter_float'
particlefilter_float.cmd = [particlefilter_float.executable] + ['-x', '128', '-y', '128', '-z', '10', '-np', '1000']

# heartwall
hearwall = LiveProcess()
heartwall.executable = binary_dir + 'gem5_fusion_heartwall'
data = data_dir + 'test.avi'
heartwall.cnd = [heartwall.executable] + [data] + ['10']

