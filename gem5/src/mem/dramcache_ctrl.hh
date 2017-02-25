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
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <functional>

#include "base/statistics.hh"
#include "enums/AddrMap.hh"
#include "enums/MemSched.hh"
#include "enums/PageManage.hh"
#include "enums/DRAMCacheReplacementScheme.hh"
#include "mem/qport.hh"
#include "params/DRAMCacheCtrl.hh"
#include "sim/eventq.hh"
#include "mem/dram_ctrl.hh"
#include "mem/cache/mshr_queue.hh"
#include "debug/DRAMCache.hh"
#include "base/random.hh"

#define DRAM_PKT_COUNT 2
#define PREDICTION_LATENCY 5
#define MAPI_PREDICTOR

// maximum number of lookups to do to find an alternative line for eviction
// our chain table has 2 bits; 0 no chain, [1-3] chain locations
#define CHAIN_MAX 3

class DRAMCacheCtrl : public DRAMCtrl
{

  public:

    /**
     * Indexes to enumerate the MSHR queues.
     */
    enum MSHRQueueIndex {
        MSHRQueue_MSHRs,
        MSHRQueue_WriteBuffer
    };

    /**
     * Reasons for caches to be blocked.
     */
    enum BlockedCause {
        Blocked_NoMSHRs,
		Blocked_NoWBuffers = MSHRQueue_WriteBuffer,
        Blocked_NoTargets,
        NUM_BLOCKED_CAUSES
    };

    /**
     * Override the default behaviour of sendDeferredPacket to enable
     * the memory-side cache port to also send requests based on the
     * current MSHR status. This queue has a pointer to our specific
     * cache implementation and is used by the MemSidePort.
     */
    class DRAMCacheReqPacketQueue : public ReqPacketQueue
    {

      protected:

        DRAMCacheCtrl &cache;
        SnoopRespPacketQueue &snoopRespQueue;

      public:

        DRAMCacheReqPacketQueue(DRAMCacheCtrl &cache, MasterPort &port,
                            SnoopRespPacketQueue &snoop_resp_queue) :
            ReqPacketQueue(cache, port), cache(cache),
            snoopRespQueue(snoop_resp_queue) { }

        /**
         * Override the normal sendDeferredPacket and do not only
         * consider the transmit list (used for responses), but also MSHR
         * requests.
         */
        virtual void sendDeferredPacket();

    };

	class DRAMCacheMasterPort : public QueuedMasterPort
	{
		DRAMCacheReqPacketQueue reqqueue;
		//dummy snoop queue - required as we are inheriting QueueMasterPort
		SnoopRespPacketQueue snoopdummy;
		DRAMCacheCtrl& dramcache;

	  public:
		DRAMCacheMasterPort(const std::string& name, DRAMCacheCtrl& _dramcache):
			QueuedMasterPort(name, &_dramcache, reqqueue, snoopdummy),
			reqqueue(_dramcache, *this, snoopdummy),
			snoopdummy(_dramcache, *this), dramcache(_dramcache){};

        /**
         * Schedule a send of a request packet (from the MSHR). Note
         * that we could already have a retry outstanding.
         */
        void schedSendEvent(Tick time)
        {
            DPRINTF(DRAMCache, "DRAMCache Scheduling send event at %llu\n", time);
            reqQueue.schedSendEvent(time);
        }

	  protected:
		bool recvTimingResp(PacketPtr pkt);

		//This port never snoops
		virtual bool isSnooping() const { return false; }
	};


	std::deque<DRAMPacket*> dramPktWriteRespQueue;
	void processWriteRespondEvent();
	EventWrapper<DRAMCacheCtrl, &DRAMCacheCtrl::processWriteRespondEvent> respondWriteEvent;

	DRAMCacheMasterPort dramCache_masterport;

	MSHRQueue mshrQueue;
	MSHRQueue writeBuffer;

	/** Pointer to the MSHR that has no targets. */
	MSHR *noTargetMSHR;

	uint64_t dramCache_size;
    uint64_t dramCache_assoc;
	uint64_t dramCache_block_size;
	bool dramCache_write_allocate;
	uint64_t dramCache_access_count;
	uint64_t dramCache_num_sets;
    Enums::DRAMCacheReplacementScheme replacement_scheme;
    uint64_t totalRows;
    uint64_t system_cache_block_size;
    uint64_t num_sub_blocks_per_way;
    uint64_t total_gpu_lines; // moving count of total lines owned by GPU
    uint64_t total_gpu_dirty_lines; // moving count of total dirty lines owned by GPU
    uint64_t total_chained_sets;
    uint64_t order; // mshr needs order for some reason
    const int numTarget;

    // addr, drampkt addr
    std::set<std::pair<Addr, Addr>> isInWriteQueue;

    /** Stores time the cache blocked for statistics. */
    Cycles blockedCycle;

    /**
     * Bit vector of the blocking reasons for the access path.
     * @sa #BlockedCause
     */
    uint8_t blocked;
    // remember if we recvd a request when we were blocked because of MSHR/WB full
    bool cacheMustSendRetry;

    int num_cores; //num of CPU cores in the system, needed for per core predictor

    bool dramCacheTimingMode;

    const uint32_t fillBufferSize;
    const uint32_t fillHighThreshold;

    uint32_t cacheFillsThisTime;
    uint32_t cacheWritesThisTime;

    std::deque<DRAMPacket*> fillQueue;
    // addr, drampkt_addr
    std::set<std::pair<Addr,Addr>> isInFillQueue;

    //  MAP-I PREDICTOR type goes here
	// holds Addr of requests that have been sent as PAM by predictor
	typedef struct pamReq
	{
		// -1 set initially, originalSet lookup not complete
		// 1 hit in originalSet
		// 0 miss in originalSet
		int8_t isHit;

		// -1 initially; 1 if chained, 0 if not chained
		int8_t isChained;
		// -1 initially; 1 hit in chainedSet; 0 miss in chainedSet
		int8_t isChainedHit;
		// set only isChained=true
		bool isChainDirty;
		bool isPamComplete; // true if parallel memory request has returned
		MSHR* mshr;
		PacketPtr pkt;
		pamReq()
		{
			isPamComplete = false;
			isHit = -1;
			isChained = -1;
			isChainedHit = -1;
			isChainDirty = false;
		}
	}pamReq;
	std::map<Addr,pamReq*> pamQueue;

    // alloy cache - memory access counter saturating 3 bit counter
    // takes values between 0 to 7
    // for prediction only MSB bit considered, hence < 3 miss; > 3 hit
    typedef uint8_t mac;

    // predictor; hash of PC to mac - indexed by folded xor hash of PC
    typedef std::map <unsigned int, mac> predictorTable;
    // per core predictor
    static std::map<int, predictorTable> predictor;

    // static prediction accuracy we want our predictor to have
    static int predAccuracy;

    // gpu occupancy threshold above which gpu req are chained
    static int chainingCPUThreshold;


    // Event call back to respond after chainAccess has completed
    // check if PAM returned after first and before second access
    std::deque<DRAMPacket*> chainRespQueue;
	void processChainRespondEvent();
	EventWrapper<DRAMCacheCtrl, &DRAMCacheCtrl::processChainRespondEvent> chainRespondEvent;

	void processChainHelper();
    // bypass tag store for CPU
    class LRUTagStore
    {
        // fully associative victim tag store for DRAMCache
        int size;
        std::vector<Addr> tags;
        DRAMCacheCtrl* dramcache;

      public:
        Stats::Scalar num_hits;
        Stats::Scalar num_read_hits;
        Stats::Scalar num_accesses;
        Stats::Scalar num_read_accesses;
        Stats::Formula hit_rate;
        Stats::Formula read_hit_rate;

        LRUTagStore (DRAMCacheCtrl *dramache, int size)
        {
            this->size = size;
            tags.reserve(size);
            this->dramcache = dramcache;
        }
        void insertIntoBypassTag(PacketPtr pkt)
        {
            Addr blockAddr = dramcache->blockAlign(pkt->getAddr());
            if (tags.size() == size)
                tags.pop_back();
            tags.insert(tags.begin(), blockAddr);

        }
        bool isHit(PacketPtr pkt)
        {
            Addr blockAddr = dramcache->blockAlign(pkt->getAddr());
            auto it = std::find (tags.begin(), tags.end(), blockAddr);
            if (it != tags.end())
            {
                tags.erase(it);
                tags.insert(tags.begin(), blockAddr);
                return true;
            }
            return false;
        }
        void regStats();

    };

    LRUTagStore *cpuBypassTag;
    bool bypassTagEnable;


    Random randomPredictor;

    //since assoc is 1 - set = way
	struct dramCacheSet_t
	{
        // Do we really need this information to be kept per set?
	    //uint64_t num_read_hits;
	    //uint64_t num_write_hits;
	    //uint64_t num_read_misses;
	    //uint64_t num_write_misses;
	    //uint64_t num_hits;
	    //uint64_t num_misses;
	    //uint64_t num_evicts;
	    //uint64_t num_wbs;
	    //uint64_t num_wbs_on_read;
	    //uint64_t num_wbs_on_write;
	    //uint64_t num_writes_to_dirty_lines;
	    //uint64_t num_gpu_occupied; //number of times this way was occupied by GPU data

	    // here are entries for each way! but we assume assoc=1 for now
	    // this should go into lruStackEntry_t if the assoc is increased

        // we moved this out to an independent array for faster access
	    //bool isGPUOwned;
	    uint64_t tag;
	    bool     valid;
	    bool     dirty;
	    bool     chainDirty;
	    // cache sub-block counters
	    //bool     *used; // a bit per each cache block sized chunk in the line to denote usage
	    //uint64_t *written; // counter for each cache block write count
	    //uint64_t *accessed; // counter for each cache block access count
	    //uint64_t *read_after_write; // counter for each cache block that was read after being written
	    //uint64_t *write_after_write; // counter for each cache block that was read after being written
	};

	struct dramCacheSet_t * set;
	std::vector<uint8_t> chainTable;
	std::vector<uint8_t> reverseChainTable;
	std::vector<bool> isRowChained;
	std::vector<bool> isGPUOwned;
	// counter for how often to sample max_gpu_lines
	int max_gpu_lines_sample_counter;

	Stats::Scalar dramCache_read_hits;
	Stats::Scalar dramCache_read_misses;
	Stats::Scalar dramCache_write_hits;
	Stats::Scalar dramCache_write_misses;
	Stats::Scalar dramCache_cpu_read_hits;
	Stats::Scalar dramCache_cpu_read_misses;
	Stats::Scalar dramCache_cpu_write_hits;
	Stats::Scalar dramCache_cpu_write_misses;

	Stats::Scalar dramCache_evicts;
	Stats::Scalar dramCache_write_backs;
	Stats::Scalar dramCache_write_backs_on_read;
	Stats::Scalar dramCache_writes_to_dirty_lines;
	Stats::Scalar dramCache_gpu_replaced_cpu;
	Stats::Scalar dramCache_cpu_replaced_gpu;
	Stats::Scalar dramCache_gpu_replaced_gpu;
	Stats::Scalar switched_to_gpu_line; // CPU lines that became GPU lines in cache
	Stats::Scalar switched_to_cpu_line; // GPU lines that became CPU lines in cache
	Stats::Scalar dramCache_cpu_hits;   // hits for CPU req
	Stats::Scalar dramCache_cpu_misses; // misses for CPU req
	// histogram grouping of set numbers (1000 buckets) and number of times sets were occupied by GPU
	//Stats::Histogram dramCache_gpu_occupancy_per_set;

	Stats::Scalar dramCache_mshr_hits;
	Stats::Scalar dramCache_cpu_mshr_hits;
	Stats::Scalar dramCache_writebuffer_hits;
	Stats::Scalar dramCache_cpu_writebuffer_hits;
	Stats::Scalar dramCache_tot_mshr_used;
	Stats::Scalar dramCache_tot_writebuffer_used;
	Stats::Scalar dramCache_max_mshr_used;
	Stats::Scalar dramCache_max_writebuffer_used;

	Stats::Scalar dramCache_max_gpu_dirty_lines; // we need to find the size of dirty line structure
	// max number of gpu sets in dramcache
	// - chaining kicks in only in cpu lines are lesser than low thresh
	// - meaning gpu should be hungry and occupy a lots of lines in the cache
	Stats::Scalar dramCache_max_gpu_lines;
	Stats::Vector dramCache_mshr_miss_latency; // Total cycle latency of MSHR [0]-cpu [1]-gpu
	Stats::Scalar dramCache_total_pred; // number of predictions made
	Stats::Scalar dramCache_incorrect_pred; // number of miss predictions by predictor

	Stats::Scalar dramCache_pam_requests; // number of times pam request sent
	Stats::Scalar dramCache_pam_returned_before_original_access;
	Stats::Scalar dramCache_pam_returned_before_chain_access;

	Stats::Scalar dramCache_noncpu0_cpu_accesses;
	Stats::Scalar dramCache_noncpu0_cpu_hits;
	Stats::Formula dramCache_noncpu0_cpu_hit_rate;

	Stats::Scalar dramCache_fillBursts; // stats for fill bursts
	Stats::Average dramCache_avgFillQLen; // average fill Q length
	Stats::Histogram fillsPerTurnAround;
	Stats::Scalar dramCache_servicedByFillQ; // reads serviced by fillQ

	Stats::Formula dramCache_max_gpu_occupancy; // % maximum gpu set occupancy in dramcache
	Stats::Formula dramCache_hit_rate;
	Stats::Formula dramCache_rd_hit_rate;
	Stats::Formula dramCache_wr_hit_rate;
	Stats::Formula dramCache_evict_rate;
	Stats::Formula dramCache_cpu_hit_rate;
	Stats::Formula dramCache_gpu_hit_rate;
	// correct predictions = total num predictions - incorrect predictions
	Stats::Formula dramCache_correct_pred;

    /** The total number of cycles blocked for each blocked cause. */
    Stats::Vector blocked_cycles;
    /** The number of times this cache blocked for each blocked cause. */
    Stats::Vector blocked_causes;

    // Latencies for write requests; DRAMCache doesn't reply immediately writes
    // because we have to check tag for hit or miss
    Stats::Scalar totWrQLat;
    Stats::Scalar totWrMemAccLat;
    Stats::Scalar totWrBusLat;
    Stats::Scalar cpuWrQLat;
    Stats::Scalar cpuWrMemAccLat;
    Stats::Scalar cpuWrBusLat;
    Stats::Scalar gpuWrQLat;
    Stats::Scalar gpuWrMemAccLat;
    Stats::Scalar gpuWrBusLat;

    // CHAINING STATS
    // total number of number of times set chaining was done
    Stats::Scalar dramCache_total_chained_sets;
    // max number of chained sets at any point in time in the cache
    Stats::Scalar dramCache_max_chained_sets;
    Stats::Scalar dramCache_avg_chained_sets;
    // assuming chaining is done at row buffer granularity
    // Stats::Scalar dramCache_total_chained_rows;
    Stats::Scalar dramCache_avg_chained_rows;
    Stats::Scalar dramCache_max_chained_rows;
    // number of times the CPU chaining threshold reached, forcing the
    // conflicting gpu request to chain or bypass
    Stats::Scalar chaining_cpu_threshold_reached;

    // max gpu lines at row granularity
    Stats::Vector dramCache_max_gpu_lines_per_row;
    Stats::Vector dramCache_accesses_per_row;
    Stats::Vector dramCache_gpu_accesses_per_row;

    // number of times the max gpu lines was above 20%,50%,80% in each row
    Stats::Vector dramCache_max_gpu_lines_per_row_above_20;
    Stats::Vector dramCache_max_gpu_lines_per_row_above_50;
    Stats::Vector dramCache_max_gpu_lines_per_row_above_70;

    Stats::Scalar dramCache_chain_read_hits;
    Stats::Scalar dramCache_chain_write_hits;
    Stats::Scalar dramCache_chain_read_misses;
    Stats::Scalar dramCache_chain_write_misses;
    Stats::Scalar dramCache_chain_cpu_read_hits;
    Stats::Scalar dramCache_chain_cpu_read_misses;
    Stats::Scalar dramCache_chain_cpu_write_hits;
    Stats::Scalar dramCache_chain_cpu_write_misses;
    Stats::Formula dramCache_chain_cpu_hits;
    Stats::Formula dramCache_chain_cpu_misses;
    Stats::Formula dramCache_chain_gpu_hits;
    Stats::Formula dramCache_chain_gpu_misses;

    Stats::Scalar dramCache_chain_write_backs;
    Stats::Scalar dramCache_chain_writes_to_dirty_lines;


    Stats::Scalar dramCache_chain_unlink_by_cpu;
    Stats::Scalar dramCache_chain_unlink_by_gpu;
    // a different set wanted to chain to this set, so it unlinked the previous chain set
    // e.g. A->C; now B wants to chain to C; A->C unlink
    Stats::Scalar dramCache_chain_unlink_by_chain;
    // gpu bypass occurs when it can't find a GPU line in CHAIN_MAX lookaheads
    Stats::Scalar dramCache_gpu_bypass;

    std::pair<unsigned int, unsigned int> getSetTagFromAddr(Addr addr)
    {
		unsigned int cacheBlock = addr/dramCache_block_size;
		unsigned int cacheSet = cacheBlock % dramCache_num_sets;
		unsigned int cacheTag = cacheBlock / dramCache_num_sets;

		return std::make_pair(cacheSet, cacheTag);
    }

    // CHAINING HELPER FUNCTIONS
    Addr getChainedAddr(unsigned int originalSet, uint8_t chainOffset)
    {
        // returns the first burst addr in the DRAMCache of the chainedSet
        // does the wrap around if needed
        Addr addr;
        uint8_t pktCount = 2;

        unsigned int chainedSet = originalSet + chainOffset;
        if((chainedSet/15) != (originalSet/15))
            chainedSet = ((floor(originalSet/15))*15) + (chainedSet%15);

        uint64_t cacheRow = floor(originalSet/15);
        // packet count is 2; we need to number our sets in multiplies of 2
        addr = chainedSet * pktCount;

        // account for tags for each 15 sets (i.e each row)
        addr += (cacheRow * 2);

        return addr;
    }

    // originalSet is the actual set
    // chainedSet is the set to which originalSet is chained to
    void unlinkChain(unsigned int originalSet)
    {
        assert(chainTable[originalSet]!=0);
        unsigned int chainedSet = originalSet + chainTable[originalSet];
        // if the chainedSet and cacheSet donot lie in same row; then we have
        // to wrap around e.g. 73+3=76; 76/15=5; 73/15=4
        if((chainedSet/15) != (originalSet/15))
        {
            chainedSet = ((floor(originalSet/15))*15) + (chainedSet%15);
        }
        reverseChainTable[chainedSet]=0;
        chainTable[originalSet]=0;
        unsigned int cacheRow = floor(chainedSet / 15);
        auto chainTableItr = chainTable.begin();
        int numChained = count_if(chainTableItr + (cacheRow*15),
                chainTableItr + ((cacheRow+1)*15),
                bind1st(std::less<uint8_t>(), 0));
        if (numChained==0)
            isRowChained[cacheRow] = false;

    }

    void unlinkReverseChain(unsigned int chainedSet)
    {
        assert(reverseChainTable[chainedSet]!=0);
        unsigned int originalSet = chainedSet - reverseChainTable[chainedSet];
        // if the chainedSet and cacheSet donot lie in same row; then we have
        // to wrap around e.g. 60-3=57; 57/15=3; 60/15=4
        if((originalSet/15) != (chainedSet/15))
        {
            originalSet = ((floor(chainedSet/15))*15) + (originalSet%15);
        }
        reverseChainTable[chainedSet]=0;
        chainTable[originalSet]=0;

        unsigned int cacheRow = floor(chainedSet / 15);
        auto chainTableItr = chainTable.begin();
        int numChained = count_if(chainTableItr + (cacheRow*15),
                chainTableItr + ((15*(cacheRow+1))-1),
                bind1st(std::less<uint8_t>(), 0));
        if (numChained==0)
             isRowChained[cacheRow] = false;
    }

    DRAMCacheCtrl(const DRAMCacheCtrlParams* p);

    ~DRAMCacheCtrl() {
        if (set) {
            //for (int i=0;i<num_sub_blocks_per_way;i++){
            //    delete set[i].used;
            //    delete set[i].written;
            //    delete set[i].accessed;
            //}
            delete [] set;
        }
        set=NULL;
    }

    void init() M5_ATTR_OVERRIDE;

    DrainState drain() M5_ATTR_OVERRIDE;

    BaseMasterPort &getMasterPort(const std::string &if_name,
                                  PortID idx = InvalidPortID);

    bool doCacheLookup(PacketPtr pkt);  // check hit/miss; returns true for hit
    bool doChainLookup(DRAMPacket* dram_pkt);  // check chained hit/miss; returns true for hit

    void doWriteBack(Addr evictAddr, int contextId);

    Addr blockAlign(Addr addr) const { return (addr & ~(Addr(dramCache_block_size - 1))); }

    /**
     * Marks the access path of the cache as blocked for the given cause. This
     * also sets the blocked flag in the slave interface.
     * @param cause The reason for the cache blocking.
     */
    void setBlocked(BlockedCause cause)
    {
        uint8_t flag = 1 << cause;
        if (blocked == 0) {
            blocked_causes[cause]++;
            blockedCycle = curCycle();
            port.setBlocked();
        }
        blocked |= flag;
        DPRINTF(DRAMCache,"Blocking for cause %d, mask=%d\n", cause, blocked);
    }

    /**
     * Marks the cache as unblocked for the given cause. This also clears the
     * blocked flags in the appropriate interfaces.
     * @param cause The newly unblocked cause.
     * @warning Calling this function can cause a blocked request on the bus to
     * access the cache. The cache must be in a state to handle that request.
     */
    void clearBlocked(BlockedCause cause)
    {
        uint8_t flag = 1 << cause;
        blocked &= ~flag;
        DPRINTF(DRAMCache,"Unblocking for cause %d, mask=%d\n", cause, blocked);
        if (blocked == 0) {
            blocked_cycles[cause] += curCycle() - blockedCycle;
            port.clearBlocked();
            if (cacheMustSendRetry)
            {
                port.sendRetryReq();
                cacheMustSendRetry = false;
            }
        }

    }

    /**
     * Allocate a buffer, passing the time indicating when schedule an
     * event to the queued port to go and ask the MSHR and write queue
     * if they have packets to send.
     *
     * allocateBufferInternal() function is called in:
     * - MSHR allocateWriteBuffer (unchached write forwarded to WriteBuffer);
     * - MSHR allocateMissBuffer (miss in MSHR queue);
     */
    MSHR *allocateBufferInternal(MSHRQueue *mq, Addr addr, int size,
                                 PacketPtr pkt, Tick time,
                                 bool sched_send)
    {
        // check that the address is block aligned since we rely on
        // this in a number of places when checking for matches and
        // overlap
        assert(addr == blockAlign(addr));

        MSHR *mshr = mq->allocate(addr, size, pkt, time, order++);

        mshr->queue = mq;

        if (mq->isFull()) {
            setBlocked((BlockedCause)mq->index);
        }

        if (sched_send)
        {
            // schedule the send
            schedMemSideSendEvent(time);
        }

        return mshr;
    }

    void markInService(MSHR *mshr, bool pending_dirty_resp)
    {
        MSHRQueue *mq = mshr->queue;
        bool wasFull = mq->isFull();
        mq->markInService(mshr, pending_dirty_resp);
        if (wasFull && !mq->isFull()) {
            clearBlocked((BlockedCause)mq->index);
        }
    }

    MSHR *allocateMissBuffer(PacketPtr pkt, Tick time, bool sched_send = true)
    {
        dramCache_tot_mshr_used++;
        if((mshrQueue.allocated+1)>dramCache_max_mshr_used.value())
            dramCache_max_mshr_used = mshrQueue.allocated+1;

        DPRINTF(DRAMCache,"Allocating MSHR for blkaddr %d size %d\n",
                blockAlign(pkt->getAddr()), dramCache_block_size);
        return allocateBufferInternal(&mshrQueue,
                                      blockAlign(pkt->getAddr()), dramCache_block_size,
                                      pkt, time, sched_send);
    }

    MSHR *allocateWriteBuffer(PacketPtr pkt, Tick time)
    {
        dramCache_tot_writebuffer_used++;
        if((writeBuffer.allocated+1)>dramCache_max_writebuffer_used.value())
            dramCache_max_writebuffer_used = writeBuffer.allocated+1;

        DPRINTF(DRAMCache,"Allocating write buffer for blkaddr %d size %d\n",
                blockAlign(pkt->getAddr()), dramCache_block_size);
        assert(pkt->isWrite() && !pkt->isRead());
        return allocateBufferInternal(&writeBuffer,
                                      blockAlign(pkt->getAddr()), dramCache_block_size,
                                      pkt, time, true);
    }

    /**
     * Return the next MSHR to service, either a pending miss from the
     * mshrQueue, a buffered write from the write buffer, or something
     * from the prefetcher.  This function is responsible for
     * prioritizing among those sources on the fly.
     */
    MSHR *getNextMSHR();

    /**
     * Find next request ready time from among possible sources.
     */
    Tick nextMSHRReadyTime() const;

    /**
     * Selects an outstanding request to service.  Called when the
     * cache gets granted the downstream bus in timing mode.
     * @return The request to service, NULL if none found.
     */
    PacketPtr getTimingPacket();

    void schedMemSideSendEvent(Tick time)
    {
        dramCache_masterport.schedSendEvent(time);
    }

	DRAMPacket* decodeAddr(PacketPtr pkt, Addr dramPktAddr, unsigned int size,
	                               bool isRead);
    void regStats();

    void processNextReqEvent();

    void processRespondEvent();
    // 1) in this function we have some stuff that is common between
    // #define and #undef MAPI_PREDICTOR
    // 2) since processRespondEvent sometimes has to do chainedAccess
    // access is not yet complete when chainedAccess is invoked
    // hence we cannot signal ReadRetry yet
    void processRespondHelper(bool accessComplete);

    Tick recvAtomic(PacketPtr pkt);

    void addToReadQueue(PacketPtr pkt, unsigned int pktCount);
    void addToWriteQueue(PacketPtr pkt, unsigned int pktCount);

    // decode incoming pkt, create dram_pkt and push it to the back of fillQueue
    // FillQueue now understands chaining and updates tags as well
    void addToFillQueue(PacketPtr pkt, unsigned int pktCount);
    // check if fill Queue has room for pktCount number of entries
    // true if full , false otherwise
    bool fillQueueFull(unsigned int neededEntries) const;

    bool recvTimingReq(PacketPtr pkt);

    void recvTimingResp(PacketPtr pkt);

    // doChainedAccess creates 2 bursts and calls doChainedDRAMAccess to do
    // the DRAM related operations. It then enqueues the packet for callback
    // by chainRespondEvent with the appropriate chainLookupDelay
    // input: cacheSet; dram_pkt used for originalSet access
    // return: chainLookupDelay
    uint64_t doChainedAccess(unsigned int cacheSet, DRAMPacket *dram_pkt);
    void doChainedDRAMAccess(DRAMPacket* dram_pkt);

    void doDRAMAccess(DRAMPacket* dram_pkt);

    // predictor functions
    uint64_t hash_pc (Addr pc);
    bool predict(ContextID contextId, Addr pc); // true for hit; false for miss
    // takes static prediction accuracy & predicts true for hit; false for miss
    bool predict_static(Addr blk_addr);
    void incMac(ContextID contextId, Addr pc);
    void decMac(ContextID contextId, Addr pc);

    Addr regenerateBlkAddr(uint64_t set, uint64_t tag);

    void access(PacketPtr ptr);
    void respond(PacketPtr ptr, Tick latency);
};

#endif //__MEM_DRAMCACHE_CTRL_HH__
