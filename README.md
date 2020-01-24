# gem5gpu source for [HAShCache](https://dl.acm.org/authorize.cfm?key=N42646) [1]

The original [gem5-gpu](https://gem5-gpu.cs.wisc.edu/wiki/) simulator

  - Merges gem5 and gpgpu-sim simulators
  - Uses the gem5 memory hierarchy to unify CPU/GPU memory
  - Adds hetergenous coherence protocols [2]

# This repo adds
- **3D stacked DRAM as hardware managed cache**
   adds logic to use a HMC-like stacked DRAM as hardware managed, memory side DRAM cache (gem5/src/mem/DRAMCacheCtrl.py, gem5/src/mem/dramcache_ctrl.cc, gem5/src/mem/dramcache_ctrl.hh)
- **Ability to differentiate between memory requests from CPU and GPU** 
adds metadata to Ruby's Abstract Controller to differentiate between requests originating from CPU and GPU by modifying the request packet (gem/src/mem/ruby/slicc_interface/AbstractController.cc)
- **DRAM Cache optimization**
- Implements PrIS, ByE and Chaining mechanisms (gem5/src/mem/dramcache_ctrl.cc)
- **Concurrently executing CPU-GPU workload mixes**
Scripts to run workload mixes from the paper (regression/runme.py)
- **Stability fixes** 
several fixes throughout the code to run the heterogenous SPEC+Rodinia workload

[1] Adarsh Patil and Ramaswamy Govindarajan, HAShCache: Heterogeneity-Aware Shared DRAMCache for Integrated Heterogeneous Systems, ACM Trans. Archit. Code Optim. (TACO) 14, 4, Article 51, December 2017 <br/>
[2] Jason  Power et. al.,  Heterogeneous system co-herence for integrated cpu-gpu systems, MICRO-46, 2013
