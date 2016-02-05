/*
 * Copyright (c) Indian Institute of Science
 * All rights reserved
 *
 * Authors: Adarsh Patil
 */

/**
 * @file
 * DRAMCacheCtrl declaration
 */

#ifndef __MEM_DRAMCACHE_CTRL_HH__
#define __MEM_DRAMCACHE_CTRL_HH__

#include <deque>
#include <string>
#include <unordered_set>

#include "base/statistics.hh"
#include "enums/AddrMap.hh"
#include "enums/MemSched.hh"
#include "enums/PageManage.hh"
#include "mem/qport.hh"
#include "params/DRAMCacheCtrl.hh"
#include "sim/eventq.hh"
#include "mem/dram_ctrl.hh"

class DRAMCacheCtrl : public DRAMCtrl
{
  private:

	class MemoryMasterPort : public QueuedMasterPort
	{

	    ReqPacketQueue reqqueue;
	    // dummy snoop queue
	    SnoopRespPacketQueue snoopdummy;
	    DRAMCacheCtrl& dramcache;

	   public:

	     MemoryMasterPort(const std::string& name, DRAMCacheCtrl& _dramcache);

	   protected:
	     bool recvTimingResp(PacketPtr pkt) { return true; }

	};

    // MemoryMasterPort to send requests to main memory
	MemoryMasterPort memoryport;

	uint64_t      dramCache_size;
    uint64_t      dramCache_assoc;
	uint64_t      dramCache_block_size;
	uint64_t      dramCache_access_count;

	Stats::Scalar dramCache_read_hits;
	Stats::Scalar dramCache_read_misses;
	Stats::Scalar dramCache_write_hits;
	Stats::Scalar dramCache_write_misses;
	Stats::Scalar dramCache_evicts;
	Stats::Scalar dramCache_write_backs;
	Stats::Scalar dramCache_writes_to_dirty_lines;
	//Stats::Scalar dramCache_num_epochs;

	Stats::Formula dramCache_hit_rate;
	Stats::Formula dramCache_rd_hit_rate;
	Stats::Formula dramCache_wr_hit_rate;
	Stats::Formula dramCache_evict_rate;

	//mshr

  public:

    DRAMCacheCtrl(const DRAMCacheCtrlParams* p);
    void init() M5_ATTR_OVERRIDE;
    virtual BaseMasterPort &getMasterPort(const std::string &if_name,
                                          PortID idx = InvalidPortID);
    void regStats();
};

#endif //__MEM_DRAMCACHE_CTRL_HH__
