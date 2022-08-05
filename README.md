# HAShCache: Shared DRAM Cache for Integrated CPU+GPU Systems

HAShCache is a shared last-level die-stacked DRAM cache for a integrated heterogeneous CPU-GPU processor.\
HAShCache can adapt dynamically to address the inherent disparity of demands in integrated CPU-GPU processors (heterogeneity-aware). \
HAShCache proposes an intelligent DRAM cache controller which employs an 
1. intelligent DRAM request scheduler (PrIS) 
2. a selective temporal bypass scheme (ByE) 
3. cache-line occupancy controlling mechanism (Chaining).

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

## Referencing our work
If you are using Dvé for your work, please cite:**

```
@article{hashcache-taco17,
   author = {Patil, Adarsh and Govindarajan, Ramaswamy},
   title = {HAShCache: Heterogeneity-Aware Shared DRAMCache for Integrated Heterogeneous Systems},
   year = {2017},
   issue_date = {December 2017},
   publisher = {Association for Computing Machinery},
   address = {New York, NY, USA},
   volume = {14},
   number = {4},
   issn = {1544-3566},
   url = {https://doi.org/10.1145/3158641},
   doi = {10.1145/3158641},
   journal = {ACM Trans. Archit. Code Optim.},
   month = {dec},
   articleno = {51},
   numpages = {26},
   keywords = {3D-stacked memory, Integrated CPU-GPU processors, cache sharing, DRAM cache}
}
```


## The original [gem5-gpu](https://gem5-gpu.cs.wisc.edu/wiki/) simulator

  - Merges gem5 and gpgpu-sim simulators
  - Uses the gem5 memory hierarchy to unify CPU/GPU memory
  - Adds hetergenous coherence protocols 
