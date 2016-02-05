from m5.params import *
from AbstractMemory import *
from DRAMCtrl import *

class DRAMCacheCtrl(DRAMCtrl):
    type = 'DRAMCacheCtrl'
    cxx_header = "mem/dramcache_ctrl.hh"
    
    # Master Port to interface with main memory
    memoryport = MasterPort("Downstream master port to memory")

    # DRAMCache Params
    dramcache_size = Param.MemorySize('512MB',"dramcache size")
    dramcache_assoc = Param.Unsigned(1, "dramcache associativity")
    dramcache_block_size = Param.Unsigned(128, "dramcache block size")
    
# A single DDR3-1600 x64 channel (one command and address bus), with
# timings based on a DDR3-1600 4 Gbit datasheet (Micron MT41J512M8) in
# an 8x8 configuration.
class DDR3_1600_x64_Cache(DRAMCacheCtrl):
    # size of device in bytes
    device_size = '512MB'

    # 8x8 configuration, 8 devices each with an 8-bit interface
    device_bus_width = 8

    # DDR3 is a BL8 device
    burst_length = 8

    # Each device has a page (row buffer) size of 1 Kbyte (1K columns x8)
    device_rowbuffer_size = '1kB'

    # 8x8 configuration, so 8 devices
    devices_per_rank = 8

    # Use two ranks
    ranks_per_channel = 2

    # DDR3 has 8 banks in all configurations
    banks_per_rank = 8

    # 800 MHz
    tCK = '1.25ns'

    # 8 beats across an x64 interface translates to 4 clocks @ 800 MHz
    tBURST = '5ns'

    # DDR3-1600 11-11-11
    tRCD = '13.75ns'
    tCL = '13.75ns'
    tRP = '13.75ns'
    tRAS = '35ns'
    tRRD = '6ns'
    tXAW = '30ns'
    activation_limit = 4
    tRFC = '260ns'

    tWR = '15ns'

    # Greater of 4 CK or 7.5 ns
    tWTR = '7.5ns'

    # Greater of 4 CK or 7.5 ns
    tRTP = '7.5ns'

    # Default same rank rd-to-wr bus turnaround to 2 CK, @800 MHz = 2.5 ns
    tRTW = '2.5ns'

    # Default different rank bus delay to 2 CK, @800 MHz = 2.5 ns
    tCS = '2.5ns'

    # <=85C, half for >85C
    tREFI = '7.8us'

    # Current values from datasheet
    IDD0 = '75mA'
    IDD2N = '50mA'
    IDD3N = '57mA'
    IDD4W = '165mA'
    IDD4R = '187mA'
    IDD5 = '220mA'
    VDD = '1.5V'

# A single HMC-2500 x32 model based on:
# [1] DRAMSpec: a high-level DRAM bank modelling tool
# developed at the University of Kaiserslautern. This high level tool
# uses RC (resistance-capacitance) and CV (capacitance-voltage) models to
# estimate the DRAM bank latency and power numbers.
# [2] A Logic-base Interconnect for Supporting Near Memory Computation in the
# Hybrid Memory Cube (E. Azarkhish et. al)
# Assumed for the HMC model is a 30 nm technology node.
# The modelled HMC consists of 4 Gbit layers which sum up to 2GB of memory (4
# layers).
# Each layer has 16 vaults and each vault consists of 2 banks per layer.
# In order to be able to use the same controller used for 2D DRAM generations
# for HMC, the following analogy is done:
# Channel (DDR) => Vault (HMC)
# device_size (DDR) => size of a single layer in a vault
# ranks per channel (DDR) => number of layers
# banks per rank (DDR) => banks per layer
# devices per rank (DDR) => devices per layer ( 1 for HMC).
# The parameters for which no input is available are inherited from the DDR3
# configuration.
# This configuration includes the latencies from the DRAM to the logic layer of
# the HMC
class HMC_2500_x32_Cache(DDR3_1600_x64_Cache):
    # size of device
    # two banks per device with each bank 4MB [2]
    device_size = '8MB'
    
    # 1x32 configuration, 1 device with 32 TSVs [2]
    device_bus_width = 32

    # HMC is a BL8 device [2]
    burst_length = 8

    # Each device has a page (row buffer) size of 256 bytes [2]
    device_rowbuffer_size = '256B'

    # 1x32 configuration, so 1 device [2]
    devices_per_rank = 1

    # 4 layers so 4 ranks [2]
    ranks_per_channel = 4

    # HMC has 2 banks per layer [2]
    # Each layer represents a rank. With 4 layers and 8 banks in total, each
    # layer has 2 banks; thus 2 banks per rank.
    banks_per_rank = 2

    # 1250 MHz [2]
    tCK = '0.8ns'

    # 8 beats across an x32 interface translates to 4 clocks @ 1250 MHz
    tBURST = '3.2ns'

    # Values using DRAMSpec HMC model [1]
    tRCD = '10.2ns'
    tCL = '9.9ns'
    tRP = '7.7ns'
    tRAS = '21.6ns'

    # tRRD depends on the power supply network for each vendor.
    # We assume a tRRD of a double bank approach to be equal to 4 clock
    # cycles (Assumption)
    tRRD = '3.2ns'

    # activation limit is set to 0 since there are only 2 banks per vault layer.
    activation_limit = 0

    # Values using DRAMSpec HMC model [1]
    tRFC = '59ns'
    tWR = '8ns'
    tRTP = '4.9ns'

    # Default different rank bus delay assumed to 1 CK for TSVs, @1250 MHz = 0.8
    # ns (Assumption)
    tCS = '0.8ns'

    # Value using DRAMSpec HMC model [1]
    tREFI = '3.9us'

    # Set default controller parameters
    page_policy = 'close'
    write_buffer_size = 8
    read_buffer_size = 8
    addr_mapping = 'RoCoRaBaCh'
    min_writes_per_switch = 8
