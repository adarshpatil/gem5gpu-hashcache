# HAShCache: Shared DRAM Cache for Integrated CPU+GPU Systems

HAShCache is a shared last-level die-stacked DRAM cache for a integrated heterogeneous CPU-GPU processor.
HAShCache is heterogeneity-aware and can adapt dynamically to address the inherent disparity of demands in such an architecture.
It combines an intelligent DRAM request scheduler (PrIS), a selective temporal bypass scheme (ByE) and an cache-line occupancy controlling mechanism (Chaining).

More details in [TACO '17](https://dl.acm.org/authorize.cfm?key=N42646) paper and at [http://adar.sh/hashcache-acm-taco](http://adar.sh/hashcache-acm-taco)

## This repo adds the following to [gem5-gpu](https://gem5-gpu.cs.wisc.edu/wiki/)
- **3D stacked DRAM as hardware managed cache** <br/>
   adds logic to use a HMC-like stacked DRAM as hardware managed, memory side DRAM cache (gem5/src/mem/DRAMCacheCtrl.py, gem5/src/mem/dramcache_ctrl.cc, gem5/src/mem/dramcache_ctrl.hh)
- **Ability to differentiate between memory requests from CPU and GPU**  <br/>
adds metadata to Ruby's Abstract Controller to differentiate between requests originating from CPU and GPU by modifying the request packet (gem/src/mem/ruby/slicc_interface/AbstractController.cc)
- **DRAM Cache optimization**  <br/>
Implements PrIS, ByE and Chaining mechanisms (gem5/src/mem/dramcache_ctrl.cc)
- **Concurrently executing CPU-GPU workload mixes**  <br/>
Scripts to run workload mixes from the paper (regression/runme.py)
- **Stability fixes**  <br/>
several fixes throughout the code to run the heterogenous SPEC+Rodinia workload


## The original [gem5-gpu](https://gem5-gpu.cs.wisc.edu/wiki/) simulator

  - Merges gem5 and gpgpu-sim simulators
  - Uses the gem5 memory hierarchy to unify CPU/GPU memory
  - Adds hetergenous coherence protocols 
