/*
 * Copyright (c) Indian Institute of Science
 * All rights reserved
 *
 * Authors: Adarsh Patil
 */

#include "mem/dramcache_ctrl.hh"
#include "debug/DRAMCache.hh"
#include "debug/Drain.hh"
#include "mem/ruby/system/RubyPort.hh"
#include "sim/system.hh"

#include <algorithm>

using namespace std;
using namespace Data;


map <int, DRAMCacheCtrl::predictorTable>  DRAMCacheCtrl::predictor;

DRAMCacheCtrl::DRAMCacheCtrl (const DRAMCacheCtrlParams* p) :
    DRAMCtrl (p), respondWriteEvent(this),
	dramCache_masterport (name () + ".dramcache_masterport",*this),
	mshrQueue ("MSHRs", p->mshrs, 4, p->demand_mshr_reserve, MSHRQueue_MSHRs),
	writeBuffer ("write buffer", p->write_buffers, p->mshrs + 1000, 0,
	MSHRQueue_WriteBuffer), dramCache_size (p->dramcache_size),
	dramCache_assoc (p->dramcache_assoc),
	dramCache_block_size (p->dramcache_block_size),
	dramCache_write_allocate(p->dramcache_write_allocate),
	dramCache_access_count (0), dramCache_num_sets (0),
	replacement_scheme (p->dramcache_replacement_scheme), totalRows (0),
	system_cache_block_size (128), // hardcoded to 128
    num_sub_blocks_per_way (0), total_gpu_lines (0), total_gpu_dirty_lines (0),
	order (0), numTarget (p->tgts_per_mshr), blocked (0), num_cores(p->num_cores),
	dramCacheTimingMode(p->dramcache_timing),
	fillHighThreshold(p->fill_high_thresh_perc),
	fillBufferSize(p->fill_buffer_size),
	cacheFillsThisTime(0), cacheWritesThisTime(0)

{
	DPRINTF(DRAMCache, "DRAMCacheCtrl constructor\n");
	// determine the dram actual capacity from the DRAM config in Mbytes
	uint64_t deviceCapacity = deviceSize / (1024 * 1024) * devicesPerRank
			* ranksPerChannel;

	rowsPerBank = (1024 * 1024 * deviceCapacity)
            / (rowBufferSize * banksPerRank * ranksPerChannel);

	totalRows = (1024 * 1024 * deviceCapacity) / rowBufferSize;

	// Hardcoded for Alloy Cache
	// Row buffer size=2kB,TAD size=128B data + 8B tag = 136B
	// 15 TADs = 2040B + 8 unused
	dramCache_num_sets = totalRows * 15;

	num_sub_blocks_per_way = dramCache_block_size / system_cache_block_size;

	DPRINTF(DRAMCache,
			"DRAMCache totalRows:%d columnsPerStripe:%d, channels:%d, "
			"banksPerRank:%d, ranksPerChannel:%d, columnsPerRowBuffer:%d, "
			"rowsPerBank:%d, burstSize:%d\n",
			totalRows, columnsPerStripe, channels, banksPerRank, ranksPerChannel,
			columnsPerRowBuffer, rowsPerBank, burstSize);

	DPRINTF(DRAMCache,"dramCacheTimingMode %d\n", dramCacheTimingMode);
	// columsPerRowBuffer is actually rowBufferSize/burstSize => each column is 1 burst
	// rowBufferSize = devicesPerRank * deviceRowBufferSize
	//    for DRAM Cache devicesPerRank = 1 => rowBufferSize = deviceRowBufferSize = 2kB
	//    for DDR3 Mem   devicesPerRank = 8 => 8kB
	// DRAMCache totalRows = 64k
	// DRAMCache sets = 960k
	// Assoc is hard to increase; increasing dramCache_block_size maybe tricky
}

void
DRAMCacheCtrl::init ()
{
	DRAMCtrl::init ();
	set = new dramCacheSet_t[dramCache_num_sets];
	DPRINTF(DRAMCache, "dramCache_num_sets: %d\n", dramCache_num_sets);

	// initialize sub block counter for each set - num_cache_blocks_per_way
	for (int i = 0; i < dramCache_num_sets; i++)
	{
		set[i].tag = 0;
		set[i].valid = false;
		set[i].dirty = false;
		/*set[i].used = new bool[num_sub_blocks_per_way];
		set[i].accessed = new uint64_t[num_sub_blocks_per_way];
		set[i].written = new uint64_t[num_sub_blocks_per_way];
		for (int j = 0; j < num_sub_blocks_per_way; j++)
		{
			set[i].used[j] = false;
			set[i].accessed[j] = 0;
			set[i].written[j] = 0;
		}*/
	}
}

DRAMCacheCtrl*
DRAMCacheCtrlParams::create ()
{
	inform("Create DRAMCacheCtrl\n");
	return new DRAMCacheCtrl (this);
}

bool
DRAMCacheCtrl::doCacheLookup (PacketPtr pkt)
{
	// doing DRAMCache hit miss eviction etc
	// find the cache block, set id and tag
	unsigned int cacheBlock = pkt->getAddr () / dramCache_block_size;
	unsigned int cacheSet = cacheBlock % dramCache_num_sets;
	unsigned int cacheTag = cacheBlock / dramCache_num_sets;
	DPRINTF(DRAMCache, "%s pktAddr:%d, blockid:%d set:%d tag:%d\n", __func__,
			pkt->getAddr(), cacheBlock, cacheSet, cacheTag);
	assert(cacheSet >= 0);
	assert(cacheSet < dramCache_num_sets);

	// check if the tag matches and line is valid
	if ((set[cacheSet].tag == cacheTag) && set[cacheSet].valid)
	{
		// HIT
		if (pkt->req->contextId () == 31) // it is a GPU req
		{
			DPRINTF(DRAMCache, "GPU request %d is a %s hit\n",
					pkt->getAddr(), pkt->cmd==MemCmd::WriteReq?"write":"read");

			if (!set[cacheSet].isGPUOwned) // was CPU owned
			{
				set[cacheSet].isGPUOwned = true;
				total_gpu_lines++;
				if(total_gpu_lines > dramCache_max_gpu_lines.value())
					dramCache_max_gpu_lines = total_gpu_lines;

				switched_to_gpu_line++;
			}
		}
		else // not a GPU req - it is a CPU req
		{
			DPRINTF(DRAMCache, "CPU request %d is a %s hit\n",
					pkt->getAddr(), pkt->cmd==MemCmd::WriteReq?"write":"read");

			if (set[cacheSet].isGPUOwned) // was a GPU line before
			{
				set[cacheSet].isGPUOwned = false;
				switched_to_cpu_line++;
				total_gpu_lines--;
			}
			dramCache_cpu_hits++;
		}

		if (pkt->isRead ())
			dramCache_read_hits++;
		else
		{
			dramCache_write_hits++;

			if (!set[cacheSet].dirty && set[cacheSet].isGPUOwned) // write to a clean line & GPU owned
			{
				total_gpu_dirty_lines++;
				if (total_gpu_dirty_lines
						> dramCache_max_gpu_dirty_lines.value ())
					dramCache_max_gpu_dirty_lines = total_gpu_dirty_lines;
			}

			if (set[cacheSet].dirty)
				dramCache_writes_to_dirty_lines++;
			else
				set[cacheSet].dirty = true;

		}

#ifdef PASS_PC
		// Knowing its a hit; inc the prediction and do stats
		Addr alignedAddr = pkt->getAddr() & ~(Addr(128 - 1));
		int cid = pkt->req->contextId();
		Addr pc = RubyPort::pcTable[cid][alignedAddr];
		if(predict(cid, alignedAddr) == false) // predicted miss but was hit
			dramCache_incorrect_pred++;
		incMac(cid, pc);
#endif

		// TODO do the sub block thing here NOT YET IMPLEMENTED
		return true;

	}
	else
	{
		// MISS
		if (pkt->req->contextId () == 31) // it is a GPU req
		{
			DPRINTF(DRAMCache, "GPU request %d is a %s miss\n",
					pkt->getAddr(), pkt->cmd==MemCmd::WriteReq?"write":"read");

			dramCache_gpu_occupancy_per_set.sample(cacheSet);

			if (set[cacheSet].valid) // valid line - causes eviction
			{
				dramCache_evicts++;
				if (set[cacheSet].dirty)
				{
					dramCache_write_backs++;
					// sent pkt for write back of evicted line once fill req returns
					if (pkt->isRead ())
						dramCache_write_backs_on_read++;
				}
				if (set[cacheSet].isGPUOwned) //set occupied by GPU line
				{
					// gpu req replacing gpu line
					if (set[cacheSet].dirty)  // GPU dirty line will be evicted
						total_gpu_dirty_lines--;
				}
				else // set occupied by CPU line
				{
					dramCache_gpu_replaced_cpu++;
					total_gpu_lines++;
				}
			}
		}
		else // it is a CPU req
		{
			DPRINTF(DRAMCache, "CPU request %d is a %s miss\n",
					pkt->getAddr(), pkt->cmd==MemCmd::WriteReq?"write":"read");

			dramCache_cpu_misses++;
			if (set[cacheSet].valid)
			{
				dramCache_evicts++;
				if (set[cacheSet].dirty)
				{
					dramCache_write_backs++;
					// send pkt for write back of evicted line once fill req returns
					if (pkt->isRead ())
						dramCache_write_backs_on_read++;
				}
				if (set[cacheSet].isGPUOwned) // set occupied by GPU line
				{
					dramCache_cpu_replaced_gpu++;
					total_gpu_lines--;
				}
				else // set occupied by CPU line
				{
					// cpu req replacing cpu line
				}
			}
		}

		// adding to MSHR will be done elsewhere

		if (pkt->isRead ())
			dramCache_read_misses++;
		else
		{
			dramCache_write_misses++;
			if (pkt->req->contextId () == 31) // gpu requesting a write
			{
				total_gpu_dirty_lines++;
				if (total_gpu_dirty_lines
						> dramCache_max_gpu_dirty_lines.value ())
					dramCache_max_gpu_dirty_lines = total_gpu_dirty_lines;
			}
		}

#ifdef PASS_PC
		// Knowing its a miss; dec the prediction and do stats
		Addr alignedAddr = pkt->getAddr() & ~(Addr(128 - 1));
		int cid = pkt->req->contextId();
		Addr pc = RubyPort::pcTable[cid][alignedAddr];
		if(predict(cid, alignedAddr) == true) // predicted hit but was miss
			dramCache_incorrect_pred++;
		decMac(cid, pc);
#endif

		return false;
	}
	// don't worry about sending a fill request here, that will done elsewhere
	assert(false);
	return false;
}

void
DRAMCacheCtrl::doWriteBack(Addr evictAddr)
{
	// write back needed as this line is dirty
	// allocate writeBuffer for this block
	// we create a dummy read req and send to access() to get data
	Request *req = new Request(evictAddr, dramCache_block_size, 0,
					Request::wbMasterId);
	PacketPtr dummyRdPkt = new Packet(req, MemCmd::ReadReq);
	PacketPtr wbPkt = new Packet(req, MemCmd::WriteReq);
	dummyRdPkt->allocate();
	wbPkt->allocate();

	access(dummyRdPkt);
	assert(dummyRdPkt->isResponse());

	// copy data returned from dummyRdPkt to pkt
	memcpy(wbPkt->getPtr<uint8_t>(), dummyRdPkt->getPtr<uint8_t>(),
			dramCache_block_size);
	delete dummyRdPkt;

	allocateWriteBuffer(wbPkt,1);

}

DRAMCtrl::DRAMPacket*
DRAMCacheCtrl::decodeAddr (PacketPtr pkt, Addr dramPktAddr, unsigned int size,
		bool isRead)
{

	// decode the address based on the address mapping scheme, with
	// Ro, Ra, Co, Ba and Ch denoting row, rank, column, bank and
	// channel, respectively
	uint8_t rank;
	uint8_t bank;
	// use a 64-bit unsigned during the computations as the row is
	// always the top bits, and check before creating the DRAMPacket
	uint64_t row;

	//	ADARSH here the address should be the address of a set
	//	cuz DRAMCache is addressed by set
	//	This is not there because we have already done it at addtoRead/WriteQ
	//	dramPktAddr = dramPktAddr / dramCache_block_size;
	//	dramPktAddr = dramPktAddr % dramCache_num_sets;

	// truncate the address to a DRAM burst, which makes it unique to
	// a specific column, row, bank, rank and channel
	// Addr addr = dramPktAddr / burstSize;
	// DPRINTF(DRAMCache, "decode addr:%ld, dramPktAddr:%ld\n", addr, dramPktAddr);

	// we have removed the lowest order address bits that denote the
	// position within the column
	// Using addrMapping == Enums::RoCoRaBaCh)
	// optimise for closed page mode and utilise maximum
	// parallelism of the DRAM (at the cost of power)

	Addr addr = dramPktAddr;

	// take out the lower-order column bits
	addr = addr / columnsPerStripe; // div by 1

	// take out the channel part of the address, not that this has
	// to match with how accesses are interleaved between the
	// controllers in the address mapping
	addr = addr / channels; // div by 1

	// start with the bank bits, as this provides the maximum
	// opportunity for parallelism between requests
	bank = addr % banksPerRank;
	addr = addr / banksPerRank;

	// next get the rank bits
	rank = addr % ranksPerChannel;
	addr = addr / ranksPerChannel;

	// next, the higher-order column bites
	addr = addr / (columnsPerRowBuffer / columnsPerStripe);

	// lastly, get the row bits
	row = addr % rowsPerBank;
	addr = addr / rowsPerBank;

	assert(rank < ranksPerChannel);
	assert(bank < banksPerRank);
	assert(row < rowsPerBank);
	assert(row < Bank::NO_ROW);

	DPRINTF(DRAMCache, "Address: %lld Rank %d Bank %d Row %d\n", dramPktAddr,
			rank, bank, row);

	// create the corresponding DRAM packet with the entry time and
	// ready time set to the current tick, the latter will be updated
	// later
	uint16_t bank_id = banksPerRank * rank + bank;
	return new DRAMPacket (pkt, isRead, rank, bank, row, bank_id, dramPktAddr,
			size, ranks[rank]->banks[bank], *ranks[rank]);
}

void
DRAMCacheCtrl::processNextReqEvent()
{
	// 1) We remove burstAlign for isInwriteQueue
	// 2) we add logic to service fillQueue requests - fillQueue is serviced
	//    when bus state is in writes

    int busyRanks = 0;
    for (auto r : ranks) {
        if (!r->isAvailable()) {
            // rank is busy refreshing
            busyRanks++;

            // let the rank know that if it was waiting to drain, it
            // is now done and ready to proceed
            r->checkDrainDone();
        }
    }

    if (busyRanks == ranksPerChannel) {
        // if all ranks are refreshing wait for them to finish
        // and stall this state machine without taking any further
        // action, and do not schedule a new nextReqEvent
        return;
    }

    // pre-emptively set to false.  Overwrite if in READ_TO_WRITE
    // or WRITE_TO_READ state
    bool switched_cmd_type = false;
    if (busState == READ_TO_WRITE) {
        DPRINTF(DRAMCache, "Switching to writes after %d reads with %d reads "
                "waiting\n", readsThisTime, readQueue.size());

        // sample and reset the read-related stats as we are now
        // transitioning to writes, and all reads are done
        rdPerTurnAround.sample(readsThisTime);
        readsThisTime = 0;

        // now proceed to do the actual writes
        busState = WRITE;
        switched_cmd_type = true;
    }
    else if (busState == WRITE_TO_READ)
    {
        DPRINTF(DRAMCache, "Switching to reads after %d writes %d fills "
                "with %d writes %d fills waiting\n", cacheWritesThisTime,
				cacheFillsThisTime, writeQueue.size(), fillQueue.size());

        wrPerTurnAround.sample(cacheWritesThisTime);
        cacheWritesThisTime = 0;
        fillsPerTurnAround.sample(cacheFillsThisTime);
        cacheFillsThisTime = 0;

        writesThisTime = 0;

        busState = READ;
        switched_cmd_type = true;
    }

    // when we get here it is either a read or a write
    if (busState == READ)
    {

        // track if we should switch or not
        bool switch_to_writes = false;

        if (readQueue.empty()) {
            // In the case there is no read request to go next,
            // trigger writes if we have passed the low threshold (or
            // if we are draining)
            if ((!writeQueue.empty() || !fillQueue.empty()) &&
                (drainState() == DrainState::Draining ||
                (writeQueue.size() + fillQueue.size())> writeLowThreshold)) {

                switch_to_writes = true;
            } else {
                // check if we are drained
                // ADARSH also check if dramPktWriteRespQueue is empty
                if (drainState() == DrainState::Draining &&
                    respQueue.empty() && dramPktWriteRespQueue.empty()) {

                    DPRINTF(Drain, "%s DRAM Cache controller done draining\n", __func__);
                    signalDrainDone();
                }

                // nothing to do, not even any point in scheduling an
                // event for the next request
                return;
            }
        } else {
            // bool to check if there is a read to a free rank
            bool found_read = false;

            // Figure out which read request goes next, and move it to the
            // front of the read queue
            // If we are changing command type, incorporate the minimum
            // bus turnaround delay which will be tCS (different rank) case
            found_read = chooseNext(readQueue,
                             switched_cmd_type ? tCS : 0);

            // if no read to an available rank is found then return
            // at this point. There could be writes to the available ranks
            // which are above the required threshold. However, to
            // avoid adding more complexity to the code, return and wait
            // for a refresh event to kick things into action again.
            if (!found_read)
                return;

            DRAMPacket* dram_pkt = readQueue.front();
            assert(dram_pkt->rankRef.isAvailable());
            // here we get a bit creative and shift the bus busy time not
            // just the tWTR, but also a CAS latency to capture the fact
            // that we are allowed to prepare a new bank, but not issue a
            // read command until after tWTR, in essence we capture a
            // bubble on the data bus that is tWTR + tCL
            if (switched_cmd_type && dram_pkt->rank == activeRank) {
                busBusyUntil += tWTR + tCL;
            }

            doDRAMAccess(dram_pkt);

            // At this point we're done dealing with the request
            readQueue.pop_front();

            // sanity check
            assert(dram_pkt->size <= burstSize);
            assert(dram_pkt->readyTime >= curTick());

            // Insert into response queue. It will be sent back to the
            // requestor at its readyTime
            if (respQueue.empty()) {
                assert(!respondEvent.scheduled());
                schedule(respondEvent, dram_pkt->readyTime);
            } else {
                assert(respQueue.back()->readyTime <= dram_pkt->readyTime);
                assert(respondEvent.scheduled());
            }

            respQueue.push_back(dram_pkt);

            // we have so many writes that we have to transition
            if ( (writeQueue.size() + fillQueue.size()) > writeHighThreshold) {
                switch_to_writes = true;
            }
        }

        // switching to writes, either because the read queue is empty
        // and the writes have passed the low threshold (or we are
        // draining), or because the writes hit the high threshold
        if (switch_to_writes) {
            // transition to writing
            busState = READ_TO_WRITE;
        }
    } else {

        // decide here if do we want to do fills or writes

        if (!writeQueue.empty() &&  (fillQueue.size() < fillHighThreshold
                || writeQueue.size() > min(writeHighThreshold, fillHighThreshold)))
		{
			// if writeQ is not empty and fillQ is lesser than high thresh or
			// writeQ size is gerater than some high thresh (we use min of
			// thresh as hysteresis) =>service writes

			// bool to check if write to free rank is found
			bool found_write = false;

			// If we are changing command type, incorporate the minimum
			// bus turnaround delay
			found_write = chooseNext(writeQueue,
									 switched_cmd_type ? std::min(tRTW, tCS) : 0);

			// if no writes to an available rank are found then return.
			// There could be reads to the available ranks. However, to avoid
			// adding more complexity to the code, return at this point and wait
			// for a refresh event to kick things into action again.
			if (!found_write)
				return;

			DRAMPacket* dram_pkt = writeQueue.front();
			assert(dram_pkt->rankRef.isAvailable());
			// sanity check
			assert(dram_pkt->size <= burstSize);

			// add a bubble to the data bus, as defined by the
			// tRTW when access is to the same rank as previous burst
			// Different rank timing is handled with tCS, which is
			// applied to colAllowedAt
			if (switched_cmd_type && dram_pkt->rank == activeRank) {
				busBusyUntil += tRTW;
			}

			doDRAMAccess(dram_pkt);

			DPRINTF(DRAMCache, "writeQueue front being popped %d\n", dram_pkt->addr);
			DPRINTF(DRAMCache, "before size writeQueue:%d isInWriteQueue:%d\n", writeQueue.size(), isInWriteQueue.size());
			writeQueue.pop_front();
			isInWriteQueue.erase(make_pair(dram_pkt->pkt->getAddr(),dram_pkt->addr));
			DPRINTF(DRAMCache, "after size writeQueue:%d isInWriteQueue:%d\n", writeQueue.size(), isInWriteQueue.size());
			// delete dram_pkt; ADARSH we will delete this later in the call back

			// Insert into write response queue. It will be sent back to the
			// requestor at its readyTime
			if (dramPktWriteRespQueue.empty()) {
				assert(!respondWriteEvent.scheduled());
				schedule(respondWriteEvent, dram_pkt->readyTime);
			} else {
				assert(dramPktWriteRespQueue.back()->readyTime <= dram_pkt->readyTime);
				assert(respondWriteEvent.scheduled());
			}

			dramPktWriteRespQueue.push_back(dram_pkt);
			++cacheWritesThisTime;
		}
        else if(writeQueue.empty() ||
                (!writeQueue.empty() && fillQueue.size() > fillHighThreshold))
        {
            // write queue empty => service fills OR
            // if fills have gone above high thesh => service fills

            bool found_fill = false;

            found_fill = chooseNext(fillQueue,
                            switched_cmd_type ? std::min(tRTW, tCS) : 0);

            // refer comment in dram_ctrl.cc line 414
            if(!found_fill)
                return;

            DRAMPacket * dram_pkt = fillQueue.front();
            assert(dram_pkt->rankRef.isAvailable());
            assert(dram_pkt->size <= burstSize);

            if (switched_cmd_type && dram_pkt->rank == activeRank) {
                 busBusyUntil += tRTW;
            }

            DPRINTF(DRAMCache, "servicing fill Req addr:%d dram_pkt addr %d\n",
                    dram_pkt->requestAddr, dram_pkt->addr);
            doDRAMAccess(dram_pkt);

            ++cacheFillsThisTime;

            fillQueue.pop_front();
            isInFillQueue.erase(make_pair(dram_pkt->requestAddr,dram_pkt->addr));
            assert(fillQueue.size() == isInFillQueue.size());
            delete dram_pkt;
        }
        else
        {
            // we should never reach here
            fatal("busState write; unable determine service fills or writes "
                    "fillQ size %d writeQ size %d", fillQueue.size(), writeQueue.size());
         }

        // If we emptied the write or fill queue, or got sufficiently below the
        // threshold (using the minWritesPerSwitch as the hysteresis) and
        // are not draining, or we have reads waiting and have done enough
        // writes, then switch to reads.
        if ( (writeQueue.empty() && fillQueue.empty()) ||
            (writeQueue.size() + fillQueue.size() + minWritesPerSwitch < writeLowThreshold &&
             drainState() != DrainState::Draining) ||
            (!readQueue.empty() && writesThisTime >= minWritesPerSwitch)) {
            // turn the bus back around for reads again
            busState = WRITE_TO_READ;

            // note that the we switch back to reads also in the idle
            // case, which eventually will check for any draining and
            // also pause any further scheduling if there is really
            // nothing to do
        }
    }

    // It is possible that a refresh to another rank kicks things back into
    // action before reaching this point.
    if (!nextReqEvent.scheduled())
        schedule(nextReqEvent, std::max(nextReqTime, curTick()));

    // If there is space available and we have writes waiting then let
    // them retry. This is done here to ensure that the retry does not
    // cause a nextReqEvent to be scheduled before we do so as part of
    // the next request processing
    if (retryWrReq && writeQueue.size() < writeBufferSize) {
        retryWrReq = false;
        port.sendRetryReq();
    }
}

void
DRAMCacheCtrl::processWriteRespondEvent()
{
	DPRINTF(DRAMCache,
			"%s: Some req has reached its readyTime\n", __func__);

	DRAMPacket* dram_pkt = dramPktWriteRespQueue.front ();

	DPRINTF(DRAMCache, "%s for address %d\n", __func__, dram_pkt->pkt->getAddr());

	if (dram_pkt->burstHelper)
	{
		// it is a split packet
		dram_pkt->burstHelper->burstsServiced++;
		if (dram_pkt->burstHelper->burstsServiced
				== dram_pkt->burstHelper->burstCount)
		{
			// nothing with regard to PAM as writes are serial - no prediction

			// we have now serviced all children packets of a system packet
			delete dram_pkt->burstHelper;
			dram_pkt->burstHelper = NULL;

			// Now access is complete, TAD unit available => doCacheLookup
			bool isHit = doCacheLookup (dram_pkt->pkt);

			if (!isHit)
			{
				// write miss we have the entire cache line
				// if writeAllocate check the conflicting cache line
			    //     if clean evict, change tag and make this line as dirty
				//     if dirty put evicted line into writeBuffer, change tag and mark dirty
				// if not writeAllocate
				//     put entry for this line into writeBuffer
				if (dramCache_write_allocate)
				{
					DPRINTF(DRAMCache, "write miss, write allocate\n");
					// check conflicting line for clean or dirty
					unsigned int cacheBlock = dram_pkt->pkt->getAddr()/dramCache_block_size;
					unsigned int cacheSet = cacheBlock % dramCache_num_sets;
					unsigned int cacheTag = cacheBlock / dramCache_num_sets;

					if (set[cacheSet].dirty)
					{
						Addr evictAddr = regenerateBlkAddr(cacheSet, set[cacheSet].tag);
						doWriteBack(evictAddr);
					}
					else
					{
						// evicted cache line clean
						set[cacheSet].tag = cacheTag;
						set[cacheSet].dirty = true;
					}
				}
				else
				{
					DPRINTF(DRAMCache, "write miss, write no allocate\n");
					// we should copy the data into new packet
					// the data set in the packet is dynamic data
					// the constructor has allocated space but not copied data
					PacketPtr pkt = new Packet(dram_pkt->pkt, false, true);
					memcpy(pkt->getPtr<uint8_t>(), dram_pkt->pkt->getPtr<uint8_t>(),
								dramCache_block_size);
					allocateWriteBuffer(pkt,1);
				}
			}

			// access has already happened in addtowriteQueue with clone_pkt
			// we now respond irrespective of whether its a hit of a miss
			PacketPtr respPkt = dram_pkt->pkt;
			DPRINTF(DRAMCache,"Responding to address %d\n", respPkt->getAddr());
			// packet should not be a response yet
			assert(!respPkt->isResponse());
			respPkt->makeResponse();

			respond(respPkt, frontendLatency);
		}
	}
	else
	{
		DPRINTF(DRAMCache,"Responding to single burst address %d\n",
				dram_pkt->pkt->getAddr());

		fatal("DRAMCache ctrl doesnt support single burst for write");
	}


	DRAMPacket * tmp = dramPktWriteRespQueue.front ();
	dramPktWriteRespQueue.pop_front ();
	delete tmp;

	if (!dramPktWriteRespQueue.empty ())
	{
		assert(dramPktWriteRespQueue.front ()->readyTime >= curTick ());
		assert(!respondWriteEvent.scheduled ());
		schedule (respondWriteEvent, dramPktWriteRespQueue.front ()->readyTime);
	}
	else
	{
		// if there is nothing left in any queue, signal a drain
		if (drainState () == DrainState::Draining && writeQueue.empty ()
				&& readQueue.empty () && respQueue.empty())
		{

			DPRINTF(Drain, "%s DRAM Cache controller done draining\n", __func__);
			signalDrainDone ();
		}
	}

}

void
DRAMCacheCtrl::processRespondEvent ()
{
	DPRINTF(DRAMCache,
			"processRespondEvent(): Some req has reached its readyTime\n");

	DRAMPacket* dram_pkt = respQueue.front ();

	DPRINTF(DRAMCache, "%s for address %d\n", __func__, dram_pkt->pkt->getAddr());

	if (dram_pkt->burstHelper)
	{
		// it is a split packet
		dram_pkt->burstHelper->burstsServiced++;
		if (dram_pkt->burstHelper->burstsServiced
				== dram_pkt->burstHelper->burstCount)
		{
			// we are done with burst helper
			delete dram_pkt->burstHelper;
			dram_pkt->burstHelper = NULL;

			// we have now serviced all children packets of a system packet
			// so we can now respond to the requester

			// Now access is complete, TAD unit available => doCacheLookup
			bool isHit = doCacheLookup (dram_pkt->pkt);

#ifdef MAPI_PREDICTOR
			auto pamQueueItr =  pamQueue.find(dram_pkt->pkt->getAddr());
			bool isInPamQueue = (pamQueueItr != pamQueue.end());
			if (isInPamQueue)
			{
				pamQueueItr->second->isHit = isHit;
				pamReq *pr = pamQueueItr->second;
				// this address has a PAM request
				if (pr->isPamComplete)
				{
					// status: memory request has returned
					if (isHit)
					{
						// status: hit in dramcache
						// access and respond to current packet
						access (dram_pkt->pkt);
						respond (dram_pkt->pkt,frontendLatency+backendLatency);

						// throw away first target as this was the clone_pkt
						// created to access the DRAM and also delete clone_pkt
						delete pr->mshr->getTarget()->pkt;
						pr->mshr->popTarget();

						// access and repond to all packets in MSHR
						while (pr->mshr->hasTargets())
						{
							MSHR::Target *target = pr->mshr->getTarget ();
							Packet *tgt_pkt = target->pkt;
							access (tgt_pkt);
							respond (tgt_pkt, frontendLatency+backendLatency);
							pr->mshr->popTarget();
						}
					}
					else
					{
						// status: miss in dramcache
						// first target packet contains data (clone_pkt) as per
						// our mechanism in recvTimingResp
						PacketPtr tgt_pkt, first_tgt_pkt;
						first_tgt_pkt = pr->mshr->getTarget()->pkt;

						Tick miss_latency = curTick () - pr->mshr->getTarget()->recvTime;
						if (first_tgt_pkt->req->contextId () == 31)
							dramCache_mshr_miss_latency[1] += miss_latency;
						else
							dramCache_mshr_miss_latency[0] += miss_latency;

						pr->mshr->popTarget();

						// copy data and respond to current packet
						dram_pkt->pkt->allocate();
						memcpy(dram_pkt->pkt->getPtr<uint8_t>(),
								first_tgt_pkt->getPtr<uint8_t>(),
								dramCache_block_size);
						dram_pkt->pkt->makeResponse();
						respond(dram_pkt->pkt, frontendLatency+backendLatency);

						// copy data and respond to all packets in MSHR
						while (pr->mshr->hasTargets())
						{
							tgt_pkt = pr->mshr->getTarget ()->pkt;
							tgt_pkt->allocate();
							memcpy(tgt_pkt->getPtr<uint8_t>(),
									first_tgt_pkt->getPtr<uint8_t>(),
									dramCache_block_size);
							tgt_pkt->makeResponse();
							respond(tgt_pkt, frontendLatency+backendLatency);
							pr->mshr->popTarget ();

						}

						// put the data into dramcache
						// fill the dramcache and update tags
						unsigned int cacheBlock = dram_pkt->pkt->getAddr()/dramCache_block_size;
						unsigned int cacheSet = cacheBlock % dramCache_num_sets;
						unsigned int cacheTag = cacheBlock / dramCache_num_sets;

						Addr evictAddr = regenerateBlkAddr(cacheSet, set[cacheSet].tag);
						DPRINTF(DRAMCache, "%s Evicting addr %d in cacheSet %d isClean %d\n",
								__func__, evictAddr ,cacheSet, set[cacheSet].dirty);

						// this block needs to be evicted
						if (set[cacheSet].dirty)
							doWriteBack(evictAddr);

						// change the tag directory
						set[cacheSet].tag = cacheTag;
						set[cacheSet].dirty = false;

						// add to fillQueue since we encountered a miss
						// create a packet and add to fillQueue
						// we can delete the packet as we never derefence it
						RequestPtr req = new Request(first_tgt_pkt->getAddr(),
								dramCache_block_size, 0, Request::wbMasterId);
						PacketPtr clone_pkt = new Packet(req, MemCmd::WriteReq);
						addToFillQueue(clone_pkt,DRAM_PKT_COUNT);

						delete clone_pkt;

						delete first_tgt_pkt;

					}

					// deallocate MSHR and pamQueue entry
					MSHRQueue *mq = pr->mshr->queue;
					mq->deallocate(pr->mshr);
					delete pr;
					pamQueue.erase(pamQueueItr);
					if (mq->isFull ())
						clearBlocked ((BlockedCause) mq->index);
				}
				else
				{
					// status: PAM request not yet returned
					if (isHit)
					{
						// status: hit in dramcache
						// access and respond to current packet
						access(dram_pkt->pkt);
						respond(dram_pkt->pkt, frontendLatency+backendLatency);

						assert(pr->mshr->getNumTargets()>=1);

						// throw away first target as that was the clone_pkt
						// created to send to DRAM
						delete pr->mshr->getTarget()->pkt;
						pr->mshr->popTarget();

						// iterate through MSHR and access and respond to all
						while (pr->mshr->hasTargets())
						{
							PacketPtr tgt_pkt = pr->mshr->getTarget ()->pkt;
							access(tgt_pkt);
							respond(tgt_pkt,frontendLatency+backendLatency);
							pr->mshr->popTarget();
						}

						// deallocate MSHR
						MSHRQueue *mq = pr->mshr->queue;
						mq->deallocate(pr->mshr);
						if (mq->isFull())
							clearBlocked ((BlockedCause) mq->index);

					}
					else
					{
						// status: miss in dramcache
						// add current packet to MSHR
						pr->mshr->allocateTarget(dram_pkt->pkt,
								dram_pkt->pkt->headerDelay, order++);

						// mshr will continue coalescing req
						// response will be sent to all when memory req returns

					}

				}
				return;

			}
#endif

			if (!isHit)  // miss send cache fill request
			{
				// send cache fill to master port - master port sends from MSHR
				allocateMissBuffer (dram_pkt->pkt, 1, true);
			}
			else
			{
				// continue to accessAndRespond if it is a hit

				// @todo we probably want to have a different front end and back
				// end latency for split packets
				access (dram_pkt->pkt);
				respond (dram_pkt->pkt, frontendLatency + backendLatency);
			}

		}
	}
	else
	{
        // ADARSH we should not reach here, no single burst
		DPRINTF(DRAMCache,"single read packet dram_pkt addr: %d addr: %d\n",
				dram_pkt->addr, dram_pkt->pkt->getAddr());
		fatal("DRAMCache ctrl doesnt support single burst for read");
	}

	DRAMPacket* tmp = respQueue.front ();
	respQueue.pop_front ();
	delete tmp;

	if (!respQueue.empty ())
	{
		assert(respQueue.front ()->readyTime >= curTick ());
		assert(!respondEvent.scheduled ());
		schedule (respondEvent, respQueue.front ()->readyTime);
	}
	else
	{
		// if there is nothing left in any queue, signal a drain
		if (drainState () == DrainState::Draining && writeQueue.empty ()
				&& readQueue.empty () && dramPktWriteRespQueue.empty())
		{

			DPRINTF(Drain, "%s DRAM Cache controller done draining\n", __func__);
			signalDrainDone ();
		}
	}

	// We have made a location in the queue available at this point,
	// so if there is a read that was forced to wait, retry now
	if (retryRdReq)
	{
		retryRdReq = false;
		port.sendRetryReq ();
	}
}

void
DRAMCacheCtrl::addToReadQueue(PacketPtr pkt, unsigned int pktCount)
{
	// we dont do read coalescing because there could a write request
	// between the 2 reads and may lead to read after write dependencies

    // only add to the read queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(!pkt->isWrite());

    assert(pktCount != 0);

    // if the request size is larger than burst size, the pkt is split into
    // multiple DRAM packets
    // Note if the pkt starting address is not aligened to burst size, the
    // address of first DRAM packet is kept unaliged. Subsequent DRAM packets
    // are aligned to burst size boundaries. This is to ensure we accurately
    // check read packets against packets in write queue.
    Addr addr = pkt->getAddr();

	addr = addr / dramCache_block_size;
	addr = addr % dramCache_num_sets;
	// ADARSH packet count is 2; we need to number our sets in multiplies of 2
	addr = addr * pktCount;

    unsigned pktsServicedByWrQFillQ = 0;
    BurstHelper* burst_helper = NULL;

    for (int cnt = 0; cnt < pktCount; ++cnt) {
        //unsigned size = std::min((addr | (burstSize - 1)) + 1,
        //                pkt->getAddr() + pkt->getSize()) - addr;

    	// ADARSH for us the size is always burstSize
    	unsigned size = burstSize;

        readPktSize[ceilLog2(size)]++;
        readBursts++;

        // First check write and fill buffer to see if the data is already at
        // the controller
        bool foundInWrQ = false, foundInFillQ = false;
        Addr burst_addr = addr;
        // if the burst address is not present then there is no need
        // looking any further
        if (isInWriteQueue.find(make_pair(pkt->getAddr(),burst_addr))
                != isInWriteQueue.end()) {
            for (const auto& p : writeQueue) {
                // check if the read is subsumed in the write queue
                // packet we are looking at
                if (p->addr <= addr && (addr + size) <= (p->addr + p->size)) {
                    foundInWrQ = true;
                    servicedByWrQ++;
                    pktsServicedByWrQFillQ++;
                    DPRINTF(DRAMCache, "Read to addr %lld with size %d serviced by "
                            "write queue\n", addr, size);
                    bytesReadWrQ += burstSize;
                    break;
                }
            }
        }

        // If not found in the write q, make a DRAM packet and
        // push it onto the read queue
        if (!foundInWrQ) {

            // search in fillQueue
            if (isInFillQueue.find(make_pair(pkt->getAddr(),burst_addr))
                    != isInFillQueue.end()) {
                for (const auto& p : fillQueue) {
                    // check if the read is subsumed in the write queue
                    // packet we are looking at
                    if (p->addr <= addr && (addr + size) <= (p->addr + p->size)) {
                        foundInFillQ = true;
                        dramCache_servicedByFillQ++;
                        pktsServicedByWrQFillQ++;
                        DPRINTF(DRAMCache, "Read to addr %lld with size %d "
                                "serviced by fill queue\n", addr, size);
                        bytesReadWrQ += burstSize;
                        break;
                    }
                }
            }
        }

        if(!foundInWrQ && !foundInFillQ) {

            // Make the burst helper for split packets
            if (pktCount > 1 && burst_helper == NULL) {
                DPRINTF(DRAMCache, "Read to addr %lld translates to %d "
                        "dram requests\n", pkt->getAddr(), pktCount);
                burst_helper = new BurstHelper(pktCount);
            }

            DRAMPacket* dram_pkt = decodeAddr(pkt, addr, size, true);
            dram_pkt->burstHelper = burst_helper;

            assert(!readQueueFull(1));
            rdQLenPdf[readQueue.size() + respQueue.size()]++;
            // ADARSH Increment cpu read Q len Pdf
            if (pkt->req->contextId() != 31)
                cpurdQLenPdf[readQueue.size() + respQueue.size()]++;

            DPRINTF(DRAMCache, "Adding to read queue addr\n");

            readQueue.push_back(dram_pkt);

            // Update stats
            avgRdQLen = readQueue.size() + respQueue.size();
        }

        // Starting address of next dram pkt (aligend to burstSize boundary)
        // addr = (addr | (burstSize - 1)) + 1;
        addr = addr + 1;
    }

    // If all packets are serviced by write queue, we send the repsonse back
    if (pktsServicedByWrQFillQ == pktCount) {
        access(pkt);
        respond(pkt, frontendLatency);
        return;
    }

    // Update how many split packets are serviced by write queue
    if (burst_helper != NULL)
        burst_helper->burstsServiced = pktsServicedByWrQFillQ;

    // If we are not already scheduled to get a request out of the
    // queue, do so now
    if (!nextReqEvent.scheduled()) {
        DPRINTF(DRAMCache, "Request scheduled immediately\n");
        schedule(nextReqEvent, curTick());
    }
}

void
DRAMCacheCtrl::addToWriteQueue(PacketPtr pkt, unsigned int pktCount)
{
    // only add to the write queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(pkt->isWrite());

    // if the request size is larger than burst size, the pkt is split into
    // multiple DRAM packets
    Addr addr = pkt->getAddr();

    // ADARSH calcuating DRAM cache address here
	addr = addr / dramCache_block_size;
	addr = addr % dramCache_num_sets;
	// ADARSH packet count is 2; we need to number our sets in multiplies of 2
	addr = addr * pktCount;

	BurstHelper* burst_helper = NULL;

    for (int cnt = 0; cnt < pktCount; ++cnt) {
        unsigned size = burstSize;

        writePktSize[ceilLog2(size)]++;
        writeBursts++;

        // see if we can merge with an existing item in the write
        // queue and keep track of whether we have merged or not
        // UPDATE We are removing merging in write queue because both requests have
        // to be serviced access here and respond later


		// Make the burst helper for split packets
		if (pktCount > 1 && burst_helper == NULL) {
			DPRINTF(DRAMCache, "Write to addr %lld translates to %d "
					"dram requests\n", pkt->getAddr(), pktCount);
			burst_helper = new BurstHelper(pktCount);
		}

		DRAMPacket* dram_pkt = decodeAddr(pkt, addr, size, false);
		dram_pkt->burstHelper = burst_helper;

		assert(writeQueue.size() < writeBufferSize);
		wrQLenPdf[writeQueue.size()]++;
		// ADARSH Increment cpuwrite queue len
		if (pkt->req->contextId() != 31)
			cpuwrQLenPdf[writeQueue.size()]++;

		DPRINTF(DRAMCache, "Adding to write queue\n");

		writeQueue.push_back(dram_pkt);
		isInWriteQueue.insert(make_pair(pkt->getAddr(),addr));
		//DPRINTF(DRAMCache, "writeQueue size - %d\n", writeQueue.size());
		//DPRINTF(DRAMCache, "isInWriteQueue size - %d\n", isInWriteQueue.size());
		assert(writeQueue.size() == isInWriteQueue.size());

		// Update stats
		avgWrQLen = writeQueue.size();

        // Starting address of next dram pkt (aligend to burstSize boundary)
        addr = addr + 1;
    }

    // we do not wait for the writes to be send to the actual memory,
    // but instead take responsibility for the consistency here and
    // snoop the write queue for any upcoming reads
    // @todo, if a pkt size is larger than burst size, we might need a
    // different front end latency

    // TODO WE NEED TO THINK ABOUT THIS! IS THIS THE RIGHT PLACE TO WRITE
    // TO BACKING STORE
    // my thought: write arrives at D$, successfully writes to backing store immediately
    // but we dont know if it will be a hit or a miss since we have not read tag
    // if miss, read req goes to memory and returns; don't perform any action then
    // if hit , all good
    // accessAndRespond(pkt, frontendLatency);

    // We decided to do access here which updates the backing store
    // response will be sent later after we know hit/miss in processWriteResponseEvent
    // we clone packet and access because access() turns packet into a response
    // Our dram_pkt has a pointer to the original packet that is needed to be kept
    // we will turn the packet into a response in processWriteResponseEvent
    PacketPtr clone_pkt = new Packet (pkt, false, true);
    access(clone_pkt);
    delete clone_pkt;

    // If we are not already scheduled to get a request out of the
    // queue, do so now
    if (!nextReqEvent.scheduled()) {
        DPRINTF(DRAMCache, "Request scheduled immediately\n");
        schedule(nextReqEvent, curTick());
    }
}

void
DRAMCacheCtrl::addToFillQueue(PacketPtr pkt, unsigned int pktCount)
{
	assert(pkt->isWrite());

	Addr addr = pkt->getAddr();

	// ADARSH calcuating DRAM cache address here
	addr = addr / dramCache_block_size;
	addr = addr % dramCache_num_sets;
	// ADARSH packet count is 2; we need to number our sets in multiplies of 2
	addr = addr * pktCount;

	if (fillQueueFull(pktCount))
	{
		// fillQueue is full we just drop the requests since we don't want to
		// add complications by doing a retry etc.
		// no correctness issues - fillQueue is just to model DRAM latencies
		warn("DRAMCache fillQueue full, dropping req addr %d", pkt->getAddr());
		return;
	}

	for (int cnt = 0; cnt < pktCount; ++cnt) {
		dramCache_fillBursts++;

		DRAMPacket* dram_pkt = decodeAddr(pkt, addr, burstSize, false);
		dram_pkt->requestAddr = pkt->getAddr();
		dram_pkt->isFill = true;

		assert(fillQueue.size() < fillBufferSize);

		DPRINTF(DRAMCache, "Adding to fill queue addr:%d\n", pkt->getAddr());

		fillQueue.push_back(dram_pkt);
		isInFillQueue.insert(make_pair(pkt->getAddr(),addr));

		dramCache_avgFillQLen = fillQueue.size();

		assert(fillQueue.size() == isInFillQueue.size());

		addr = addr + 1;
	}

    // If we are not already scheduled to get a request out of the
    // queue, do so now
    if (!nextReqEvent.scheduled()) {
        DPRINTF(DRAMCache, "Request scheduled immediately\n");
        schedule(nextReqEvent, curTick());
    }

}

bool
DRAMCacheCtrl::fillQueueFull(unsigned int neededEntries) const
{
	return ((fillQueue.size() + neededEntries) > fillBufferSize);
}

bool
DRAMCacheCtrl::recvTimingReq (PacketPtr pkt)
{
	if(!dramCacheTimingMode)
	{
		access(pkt);
		respond(pkt,frontendLatency);
		return true;
	}

	/// @todo temporary hack to deal with memory corruption issues until
	/// 4-phase transactions are complete
	for (int x = 0; x < pendingDelete.size (); x++)
		delete pendingDelete[x];
	pendingDelete.clear ();

	// This is where we enter from the outside world
	DPRINTF(DRAMCache,
			"%s: request %s addr %lld size %d, ContextId: %d Threadid: %d\n",
			__func__, pkt->cmdString(), pkt->getAddr(), pkt->getSize(),
			pkt->req->contextId(), pkt->req->threadId());

	// simply drop inhibited packets and clean evictions
	if (pkt->memInhibitAsserted () || pkt->cmd == MemCmd::CleanEvict)
	{
		DPRINTF(DRAMCache,
				"Inhibited packet or clean evict -- Dropping it now\n");
		pendingDelete.push_back (pkt);
		return true;
	}

	// cache is blocked due to MSHRs being full
	if (blocked != 0)
	{
		DPRINTF(DRAMCache,"%s cache blocked %d", __func__, pkt->getAddr());
		return false;
	}

	// Calc avg gap between requests
	if (prevArrival != 0)
	{
		totGap += curTick () - prevArrival;
	}
	prevArrival = curTick ();

	// operation is as follows: check MSHR/writeBuffer for hit or miss
	// write cannot occur as a coalesce with MSHR/writeBuffer
	// for read do prediction using predictorTable to perform SAM or PAM
	// start DRAMCache access; verify with tag if hit or miss
	// if read miss send fill request, add to MSHR
	// don't remove cache conflict line till fill req returns
	// once fill req returns, remove from MSHR, alloc cache block
	// if conflict line is dirty - allocate writeBuffer and send for WB
	// once wb ack is received, remove from writeBuffer
	// this cache is non inclusive, so there could be write miss events
	// if write miss check writeAlloc policy? evict line : alloc writeBuffer

	// ADARSH check MSHR if there is outstanding request for this address
	Addr blk_addr = blockAlign(pkt->getAddr());
	MSHR *mshr = mshrQueue.findMatch (blk_addr, false);
	if (mshr)
	{
		if(pkt->cmd == MemCmd::WriteReq) {
			// a write request can never coalesce with read req. Here is why
			// write req means that some cache evicted the line out (writeback)
			// the read request from another cache could have got the data from
			// this cache via coherence not should not send the data to DRAMCache
			fatal("DRAMCache got a write request coalescing with read");
		}

		DPRINTF(DRAMCache, "%s coalescing MSHR for %s addr %#d\n", __func__,
				pkt->cmdString(), pkt->getAddr());
		dramCache_mshr_hits++;
		if (pkt->req->contextId () != 31)
			dramCache_cpu_mshr_hits++;

		mshr->allocateTarget (pkt, pkt->headerDelay, order++);
		if (mshr->getNumTargets () == numTarget)
		{
			noTargetMSHR = mshr;
			setBlocked (Blocked_NoTargets);
		}
		return true;
	}

	// ADARSH check writeBuffer if there is outstanding write back
	// service from writeBuffer only if the request is a read (accessAndRespond)
	// if the request was a write it should go as a read req to memory
	// we assume that memory will coalesce read and write in its queues
	mshr = writeBuffer.findMatch (blk_addr, false);
	if (mshr && pkt->isRead())
	{
		DPRINTF(DRAMCache, "%s servicing read using WriteBuffer for %s addr %#d\n",
				__func__, pkt->cmdString(), pkt->getAddr());
		dramCache_writebuffer_hits++;
		if (pkt->req->contextId () != 31)
			dramCache_cpu_writebuffer_hits++;

		access(pkt);
		respond(pkt, frontendLatency);
		return true;
	}

#ifdef PASS_PC
	// perform prediction using cache address; lookup RubyPort::predictorTable
	int cid = pkt->req->contextId();
	DPRINTF(DRAMCache,"PC %d for addr: %d\n", RubyPort::pcTable[cid][blk_addr],
			blk_addr);

	// adjust mruPcAddrList to keep it in MRU
	RubyPort::mruPcAddrList[cid].remove(blk_addr);
	RubyPort::mruPcAddrList[cid].push_front(blk_addr);
#endif

#ifdef MAPI_PREDICTOR
	// do prediction only for readReq, decide if this access should be SAM/PAM
	// prediction is done only for CPU requests, GPU requests are serial
	if ( (pkt->cmd == MemCmd::ReadReq) && (pkt->req->contextId () != 31))
	{
		if (predict(RubyPort::pcTable[cid][blk_addr], cid) == false)
		{
			// predicted as miss do PAM
			// allocate in PAMQueue
			// create an entry in MSHR, which will send a request to memory

			// we use pamQueue to track PAM requests
			pamQueue[blk_addr] = new pamReq();

			// create MSHR entry
			// 1) we are sure that MSHR entry doesn't exist for this address since if
			//   there was an MSHR it would have been coalesced above
			// 2) we create a new read packet as PAM request can return later than
			//    actual DRAMCache access
			PacketPtr clone_pkt = new Packet (pkt, false, true);
			pamQueue[blk_addr]->mshr =
					allocateMissBuffer(clone_pkt, PREDICTION_LATENCY, true);
		}
	}
#endif

	// Find out how many dram packets a pkt translates to
	// If the burst size is equal or larger than the pkt size, then a pkt
	// translates to only one dram packet. Otherwise, a pkt translates to
	// multiple dram packets
	unsigned size = pkt->getSize ();
	//unsigned offset = pkt->getAddr () & (burstSize - 1);
	//unsigned int dram_pkt_count = divCeil (offset + size, burstSize);
	// we know that we need 5 burts for TAD; 4 for data and 1 for tag
	// UPDATE we modified this to 2 bursts including tag since we assume the
	// tags also burst out using an odd burst size of slightly greater than 64B

	// check local buffers and do not accept if full
	if (pkt->isRead ())
	{
		assert(size != 0);
		if (readQueueFull (DRAM_PKT_COUNT))
		{
			DPRINTF(DRAMCache, "Read queue full, not accepting\n");
			// remember that we have to retry this port
			retryRdReq = true;
			numRdRetry++;
			return false;
		}
		else
		{
			addToReadQueue (pkt, DRAM_PKT_COUNT);
			readReqs++;
			if (pkt->req->contextId () == 31)
				gpuReadReqs++;
			else
				cpuReadReqs++;
			bytesReadSys += size;
		}
	}
	else if (pkt->isWrite ())
	{
		assert(size != 0);
		if (writeQueueFull (DRAM_PKT_COUNT))
		{
			DPRINTF(DRAMCache, "Write queue full, not accepting\n");
			// remember that we have to retry this port
			retryWrReq = true;
			numWrRetry++;
			return false;
		}
		else
		{
			addToWriteQueue (pkt, DRAM_PKT_COUNT);
			writeReqs++;
			if (pkt->req->contextId () == 31)
				gpuWriteReqs++;
			else
				cpuWriteReqs++;
			bytesWrittenSys += size;
		}
	}
	else
	{
		DPRINTF(DRAMCache, "Neither read nor write, ignore timing\n");
		fatal("neither read not write\n");
		neitherReadNorWrite++;
		access(pkt);
		respond(pkt, 1);
	}

	return true;
}

Tick
DRAMCacheCtrl::recvAtomic (PacketPtr pkt)
{
	DPRINTF(DRAMCache, "recvAtomic: %s 0x%x\n", pkt->cmdString(), pkt->getAddr());

	// do the actual memory access and turn the packet into a response
	access (pkt);

	Tick latency = 0;
	if (!pkt->memInhibitAsserted () && pkt->hasData ())
	{
		// this value is not supposed to be accurate, just enough to
		// keep things going, mimic a closed page
		latency = tRP + tRCD + tCL;
	}
	return latency;
}

bool
DRAMCacheCtrl::DRAMCacheMasterPort::recvTimingResp (PacketPtr pkt)
{
	dramcache.recvTimingResp (pkt);
	return true;
}

void
DRAMCacheCtrl::recvTimingResp (PacketPtr pkt)
{
	assert(pkt->isResponse ());

#ifdef MAPI_PREDICTOR
	auto pamQueueItr =  pamQueue.find(pkt->getAddr());
	bool isInPamQueue = (pamQueueItr != pamQueue.end());
	if (isInPamQueue)
	{
		pamQueueItr->second->isPamComplete = true;
		pamReq *pr = pamQueueItr->second;
		// found in PAM Queue
		if (pr->isHit == -1)
		{
			// DRAMCache hasn't done access so we dont know hit or miss
			// we put the data returned from this packet, that came from DRAM
			// into the first target of the MSHR for use incase of a miss
			// (which is the clone_pkt we created for PAM requests)
			PacketPtr tgt_pkt = pamQueueItr->second->mshr->getTarget()->pkt;
			tgt_pkt->allocate();
			memcpy(tgt_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(),
					dramCache_block_size);
			delete pkt;
			return;
		}
		else if (pr->isHit == true)
		{
			// do nothing here , delete packet and pamQueue done later
			// as they are common between isHit true and false

		}
		else if (pr->isHit == false)
		{
			// iterate through MSHR and make response and pop target
			MSHR *mshr = dynamic_cast<MSHR*> (pkt->senderState);

			assert(mshr);

			// the mshr in the recieved packet should be same as PAMQueue mshr
			assert(mshr==pr->mshr);

			MSHRQueue *mq = mshr->queue;
			bool wasFull = mq->isFull ();

			if (mshr == noTargetMSHR)
			{
				clearBlocked (Blocked_NoTargets);
				noTargetMSHR = NULL;
			}

			// throw away first target as it was the clone_pkt used to
			// send request to DRAM
			delete mshr->getTarget()->pkt;
			mshr->popTarget();

			while (mshr->hasTargets ())
			{
				MSHR::Target *target = mshr->getTarget ();
				Packet *tgt_pkt = target->pkt;
				tgt_pkt->allocate();
				memcpy(tgt_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(),
							dramCache_block_size);
				tgt_pkt->makeResponse();
				respond(tgt_pkt,1);
				mshr->popTarget();
			}

			mq->deallocate(pr->mshr);
			if (wasFull)
				clearBlocked ((BlockedCause) mq->index);

			// update tags
			unsigned int cacheBlock = pkt->getAddr()/dramCache_block_size;
			unsigned int cacheSet = cacheBlock % dramCache_num_sets;
			unsigned int cacheTag = cacheBlock / dramCache_num_sets;

			Addr evictAddr = regenerateBlkAddr(cacheSet, set[cacheSet].tag);
			DPRINTF(DRAMCache, "%s Evicting addr %d in cacheSet %d isClean %d\n",
					__func__, evictAddr ,cacheSet, set[cacheSet].dirty);

			// this block needs to be evicted
			if (set[cacheSet].dirty)
				doWriteBack(evictAddr);

			// change the tag directory
			set[cacheSet].tag = cacheTag;
			set[cacheSet].dirty = false;
		}

		delete pkt->req;
		delete pkt;
		delete pr;
		pamQueue.erase(pamQueueItr);

		return;

	}
#endif

	MSHR *mshr = dynamic_cast<MSHR*> (pkt->senderState);

	assert(mshr);

	DPRINTF(DRAMCache, "Handling response %s for addr %d size %d MSHR %d\n",
			pkt->cmdString(), pkt->getAddr(), pkt->getSize(), mshr->queue->index);

	MSHRQueue *mq = mshr->queue;
	bool wasFull = mq->isFull ();

	if (mshr == noTargetMSHR)
	{
		clearBlocked (Blocked_NoTargets);
		noTargetMSHR = NULL;
	}

	// write allocate policy - read resp do cache things
	if(mshr->queue->index == 0 && pkt->isRead())
	{
		MSHR::Target *initial_tgt = mshr->getTarget ();
		// find the cache block, set id and tag
		// fix the tag entry now that memory access has returned
		unsigned int cacheBlock = pkt->getAddr()/dramCache_block_size;
		unsigned int cacheSet = cacheBlock % dramCache_num_sets;
		unsigned int cacheTag = cacheBlock / dramCache_num_sets;

		if (!set[cacheSet].valid)
		{
			// cold miss - the set was not valid
			set[cacheSet].tag = cacheTag;
			set[cacheSet].dirty = false;
			set[cacheSet].valid = true;

			// add to fillQueue since we encountered a miss
			// create a packet and add to fillQueue
			// we can delete the packet as we never derefence it
			RequestPtr req = new Request(pkt->getAddr(),
					dramCache_block_size, 0, Request::wbMasterId);
			PacketPtr clone_pkt = new Packet(req, MemCmd::WriteReq);
			addToFillQueue(clone_pkt,DRAM_PKT_COUNT);
			delete clone_pkt;
		}
		else if (set[cacheSet].tag == cacheTag)
		{
			// predictor could have possibly sent out a PAM request incorrectly
			// discard this and do nothing to tag directory (doesn't reach here)
		}
		else if (set[cacheSet].tag != cacheTag)
		{
			Addr evictAddr = regenerateBlkAddr(cacheSet, set[cacheSet].tag);
			DPRINTF(DRAMCache, "%s Evicting addr %d in cacheSet %d isClean %d\n",
					__func__,evictAddr ,cacheSet, set[cacheSet].dirty);
			// this block needs to be evicted
			if (set[cacheSet].dirty)
				doWriteBack(evictAddr);

			// change the tag directory
			set[cacheSet].tag = cacheTag;
			set[cacheSet].dirty = false;

			// add to fillQueue since we encountered a miss
			// create a packet and add to fillQueue
			// we can delete the packet as we never derefence it
			RequestPtr req = new Request(pkt->getAddr(),
					dramCache_block_size, 0, Request::wbMasterId);
			PacketPtr clone_pkt = new Packet(req, MemCmd::WriteReq);
			addToFillQueue(clone_pkt,DRAM_PKT_COUNT);
			delete clone_pkt;
		}

		Tick miss_latency = curTick () - initial_tgt->recvTime;
		if (pkt->req->contextId () == 31)
			dramCache_mshr_miss_latency[1] += miss_latency; //gpu mshr miss latency
		else
			dramCache_mshr_miss_latency[0] += miss_latency; //cpu mshr miss latency


		while (mshr->hasTargets ())
		{
			MSHR::Target *target = mshr->getTarget ();
			Packet *tgt_pkt = target->pkt;

			assert(pkt->getAddr() == tgt_pkt->getAddr());

			DPRINTF(DRAMCache,"%s return from read miss\n", __func__);
			access(tgt_pkt);
			respond(tgt_pkt, backendLatency);

			mshr->popTarget ();
		}
	}

	else if(mshr->queue->index == 1 && pkt->isWrite())
	{
		// write buffer cannot coalesce assert if more than 1 target
		assert(mshr->getNumTargets()==1);
		DPRINTF(DRAMCache,"write back completed addr %d\n", pkt->getAddr());
		delete mshr->getTarget ()->pkt;
		mshr->popTarget ();
		delete pkt->req;
	}
	else
	{
		// if MSHR and write or if WriteBuffer and read its fatal
		// We should never reach here
		fatal("DRAMCache doesn't understand this response");
	}

	delete pkt;
	mq->deallocate (mshr);
	if (wasFull)
		clearBlocked ((BlockedCause) mq->index);

}

void
DRAMCacheCtrl::doDRAMAccess(DRAMPacket* dram_pkt)
{
	//UPDATE the only change here is open adaptive and close adaptive scheme
	// checks fill queue for potential row buffer hits as well for fill Req
	// we add a parameter called isFill in dram_pkt which is false is default
	// and only true for fillQueue requests

    DPRINTF(DRAMCache, "Timing access to addr %lld, rank/bank/row %d %d %d\n",
            dram_pkt->addr, dram_pkt->rank, dram_pkt->bank, dram_pkt->row);

    // get the rank
    Rank& rank = dram_pkt->rankRef;

    // get the bank
    Bank& bank = dram_pkt->bankRef;

    // for the state we need to track if it is a row hit or not
    bool row_hit = true;

    // respect any constraints on the command (e.g. tRCD or tCCD)
    Tick cmd_at = std::max(bank.colAllowedAt, curTick());

    // Determine the access latency and update the bank state
    if (bank.openRow == dram_pkt->row) {
        // nothing to do
    } else {
        row_hit = false;

        // If there is a page open, precharge it.
        if (bank.openRow != Bank::NO_ROW) {
            prechargeBank(rank, bank, std::max(bank.preAllowedAt, curTick()));
        }

        // next we need to account for the delay in activating the
        // page
        Tick act_tick = std::max(bank.actAllowedAt, curTick());

        // Record the activation and deal with all the global timing
        // constraints caused be a new activation (tRRD and tXAW)
        activateBank(rank, bank, act_tick, dram_pkt->row);

        // issue the command as early as possible
        cmd_at = bank.colAllowedAt;
    }

    // we need to wait until the bus is available before we can issue
    // the command
    cmd_at = std::max(cmd_at, busBusyUntil - tCL);

    // update the packet ready time
    dram_pkt->readyTime = cmd_at + tCL + tBURST;

    // only one burst can use the bus at any one point in time
    assert(dram_pkt->readyTime - busBusyUntil >= tBURST);

    // update the time for the next read/write burst for each
    // bank (add a max with tCCD/tCCD_L here)
    Tick cmd_dly;
    for(int j = 0; j < ranksPerChannel; j++) {
        for(int i = 0; i < banksPerRank; i++) {
            // next burst to same bank group in this rank must not happen
            // before tCCD_L.  Different bank group timing requirement is
            // tBURST; Add tCS for different ranks
            if (dram_pkt->rank == j) {
                if (bankGroupArch &&
                   (bank.bankgr == ranks[j]->banks[i].bankgr)) {
                    // bank group architecture requires longer delays between
                    // RD/WR burst commands to the same bank group.
                    // Use tCCD_L in this case
                    cmd_dly = tCCD_L;
                } else {
                    // use tBURST (equivalent to tCCD_S), the shorter
                    // cas-to-cas delay value, when either:
                    // 1) bank group architecture is not supportted
                    // 2) bank is in a different bank group
                    cmd_dly = tBURST;
                }
            } else {
                // different rank is by default in a different bank group
                // use tBURST (equivalent to tCCD_S), which is the shorter
                // cas-to-cas delay in this case
                // Add tCS to account for rank-to-rank bus delay requirements
                cmd_dly = tBURST + tCS;
            }
            ranks[j]->banks[i].colAllowedAt = std::max(cmd_at + cmd_dly,
                                             ranks[j]->banks[i].colAllowedAt);
        }
    }

    // Save rank of current access
    activeRank = dram_pkt->rank;

    // If this is a write, we also need to respect the write recovery
    // time before a precharge, in the case of a read, respect the
    // read to precharge constraint
    bank.preAllowedAt = std::max(bank.preAllowedAt,
                                 dram_pkt->isRead ? cmd_at + tRTP :
                                 dram_pkt->readyTime + tWR);

    // increment the bytes accessed and the accesses per row
    bank.bytesAccessed += burstSize;
    ++bank.rowAccesses;

    // if we reached the max, then issue with an auto-precharge
    bool auto_precharge = pageMgmt == Enums::close ||
        bank.rowAccesses == maxAccessesPerRow;

    // if we did not hit the limit, we might still want to
    // auto-precharge
    if (!auto_precharge &&
        (pageMgmt == Enums::open_adaptive ||
         pageMgmt == Enums::close_adaptive)) {
        // a twist on the open and close page policies:
        // 1) open_adaptive page policy does not blindly keep the
        // page open, but close it if there are no row hits, and there
        // are bank conflicts in the queue
        // 2) close_adaptive page policy does not blindly close the
        // page, but closes it only if there are no row hits in the queue.
        // In this case, only force an auto precharge when there
        // are no same page hits in the queue
        bool got_more_hits = false;
        bool got_bank_conflict = false;

        // either look at the read queue or write queue or fill queue
        const deque<DRAMPacket*>& queue = dram_pkt->isRead ? readQueue :
            (dram_pkt->isFill ? fillQueue : writeQueue);
        auto p = queue.begin();
        // make sure we are not considering the packet that we are
        // currently dealing with (which is the head of the queue)
        ++p;

        // keep on looking until we find a hit or reach the end of the queue
        // 1) if a hit is found, then both open and close adaptive policies keep
        // the page open
        // 2) if no hit is found, got_bank_conflict is set to true if a bank
        // conflict request is waiting in the queue
        while (!got_more_hits && p != queue.end()) {
            bool same_rank_bank = (dram_pkt->rank == (*p)->rank) &&
                (dram_pkt->bank == (*p)->bank);
            bool same_row = dram_pkt->row == (*p)->row;
            got_more_hits |= same_rank_bank && same_row;
            got_bank_conflict |= same_rank_bank && !same_row;
            ++p;
        }

        // auto pre-charge when either
        // 1) open_adaptive policy, we have not got any more hits, and
        //    have a bank conflict
        // 2) close_adaptive policy and we have not got any more hits
        auto_precharge = !got_more_hits &&
            (got_bank_conflict || pageMgmt == Enums::close_adaptive);
    }

    // DRAMPower trace command to be written
    std::string mem_cmd = dram_pkt->isRead ? "RD" : "WR";

    // MemCommand required for DRAMPower library
    MemCommand::cmds command = (mem_cmd == "RD") ? MemCommand::RD :
                                                   MemCommand::WR;

    // if this access should use auto-precharge, then we are
    // closing the row
    if (auto_precharge) {
        // if auto-precharge push a PRE command at the correct tick to the
        // list used by DRAMPower library to calculate power
        prechargeBank(rank, bank, std::max(curTick(), bank.preAllowedAt));

        DPRINTF(DRAMCache, "Auto-precharged bank: %d\n", dram_pkt->bankId);
    }

    // Update bus state
    busBusyUntil = dram_pkt->readyTime;

    DPRINTF(DRAMCache, "Access to %lld, ready at %lld bus busy until %lld.\n",
            dram_pkt->addr, dram_pkt->readyTime, busBusyUntil);

    dram_pkt->rankRef.power.powerlib.doCommand(command, dram_pkt->bank,
                                                 divCeil(cmd_at, tCK) -
                                                 timeStampOffset);

    //DPRINTF(DRAMPower, "%llu,%s,%d,%d\n", divCeil(cmd_at, tCK) -
    //        timeStampOffset, mem_cmd, dram_pkt->bank, dram_pkt->rank);

    // Update the minimum timing between the requests, this is a
    // conservative estimate of when we have to schedule the next
    // request to not introduce any unecessary bubbles. In most cases
    // we will wake up sooner than we have to.
    nextReqTime = busBusyUntil - (tRP + tRCD + tCL);

    // Update the stats and schedule the next request
    if (dram_pkt->isRead) {
        ++readsThisTime;
        if (row_hit)
            readRowHits++;
        bytesReadDRAM += burstSize;
        perBankRdBursts[dram_pkt->bankId]++;

        // Update latency stats
        totMemAccLat += dram_pkt->readyTime - dram_pkt->entryTime;
        totBusLat += tBURST;
        totQLat += cmd_at - dram_pkt->entryTime;
        if(dram_pkt->pkt->req->contextId() == 31) {
             gpuQLat += cmd_at - dram_pkt->entryTime;
        }
        else {
            cpuQLat += cmd_at - dram_pkt->entryTime;
        }
    } else {
        ++writesThisTime;
        if (row_hit)
            writeRowHits++;
        bytesWritten += burstSize;
        perBankWrBursts[dram_pkt->bankId]++;
    }
}

DrainState
DRAMCacheCtrl::drain()
{
    // if there is anything in any of our internal queues, keep track
    // of that as well
    if (!(writeQueue.empty() && readQueue.empty() && respQueue.empty()
            && dramPktWriteRespQueue.empty())) {
        DPRINTF(Drain, "DRAM Cache controller not drained, write: %d, read: %d,"
                " resp: %d writeResp: %d\n", writeQueue.size(), readQueue.size(),
                respQueue.size(), dramPktWriteRespQueue.size());

        // the only part that is not drained automatically over time
        // is the write queue, thus kick things into action if needed
        if (!writeQueue.empty() && !nextReqEvent.scheduled()) {
            schedule(nextReqEvent, curTick());
        }
        return DrainState::Draining;
    } else {
        return DrainState::Drained;
    }
}

void
DRAMCacheCtrl::regStats ()
{
	using namespace Stats;
	DRAMCtrl::regStats ();

	dramCache_read_hits
		.name (name () + ".dramCache_read_hits")
		.desc ("Number of read hits in dramcache");

	dramCache_read_misses
		.name (name () + ".dramCache_read_misses")
		.desc ("Number of read misses in dramcache");

	dramCache_write_hits
		.name (name () + ".dramCache_write_hits")
		.desc ("Number of write hits in dramcache");

	dramCache_write_misses
	.name (name () + ".dramCache_write_misses")
	.desc ("Number of write misses in dramcache");

	dramCache_evicts
		.name (name () + ".dramCache_evicts")
		.desc ("Number of evictions from dramcache");

	dramCache_write_backs
		.name (name () + ".dramCache_write_backs")
		.desc ("Number of write backs from dramcache");

	dramCache_write_backs_on_read
		.name (name () + ".dramCache_write_backs_on_read")
		.desc ("Number of write backs caused by a read from dramcache");

	dramCache_writes_to_dirty_lines
		.name (name () + "dramCache_writes_to_dirty_lines")
		.desc ("Number of writes to dirty lines in dramcache");

	dramCache_gpu_replaced_cpu
		.name (name () + ".dramCache_gpu_replaced_cpu")
		.desc ("Number of times gpu replaced cpu line ");

	dramCache_cpu_replaced_gpu
		.name (name () + ".dramCache_cpu_replaced_gpu")
		.desc ("Number of times cpu replaced gpu line ");

	switched_to_gpu_line
		.name (name () + ".switched_to_gpu_line")
		.desc ("Number of times CPU line became GPU line in cache ");

	switched_to_cpu_line
		.name (name () + ".switched_to_cpu_line")
		.desc ("Number of times GPU line became CPU line in cache ");

	dramCache_cpu_hits
		.name (name () + ".dramCache_cpu_hits")
		.desc ("Number of hits for CPU Requests");

	dramCache_cpu_misses
		.name (name () + ".dramCache_cpu_misses")
		.desc ("Number of misses for CPU Requests");

	dramCache_mshr_hits
		.name (name () + ".dramCache_mshr_hits")
		.desc ("Number of hits in the MSHR");

	dramCache_cpu_mshr_hits
		.name (name () + ".dramCache_cpu_mshr_hits")
		.desc ("Number of hits for CPU requests in the MSHR");

	dramCache_writebuffer_hits
		.name (name () + ".dramCache_writebuffer_hits")
		.desc ("Number of hits in the writebuffer");

	dramCache_cpu_writebuffer_hits
		.name (name () + ".dramCache_cpu_writebuffer_hits")
		.desc ("Number of hits for CPU requests in the writebuffer");

	dramCache_max_gpu_dirty_lines
		.name (name () + ".dramCache_max_gpu_dirty_lines")
		.desc ("maximum number of gpu dirty lines in cache");

	dramCache_max_gpu_lines
		.name (name () + ".dramCache_max_gpu_lines")
		.desc ("maximum number of gpu lines in cache");

	dramCache_total_pred
		.name ( name() + ".dramCache_total_pred")
		.desc("total number of predictions made");

	dramCache_incorrect_pred
		.name ( name() + ".dramCache_incorrect_pred")
		.desc("Number of incorrect predictions");

    dramCache_fillBursts
        .name(name() + ".dramCache_fillBursts")
        .desc("Number of DRAM Fill bursts");

    dramCache_avgFillQLen
        .name(name() + ".dramCache_avgFillQLen")
        .desc("Average fill queue length when enqueuing")
        .precision(2);

    fillsPerTurnAround
        .init(fillBufferSize)
        .name(name() + ".fillsPerTurnAround")
        .desc("Fills before turning the bus around for writes")
        .flags(nozero);

    dramCache_servicedByFillQ
        .name(name() + ".dramCache_servicedByFillQ")
        .desc("Number of DRAM read bursts serviced by the fill queue");

	dramCache_mshr_miss_latency
		.init (2)
		.name (name () + ".dramCache_mshr_miss_latency")
		.desc ("total miss latency of mshr for cpu and gpu");

	dramCache_gpu_occupancy_per_set
		.init (1000)
		.name (name () + ".dramCache_gpu_occupancy_per_set")
		.desc ("Number of times the way was occupied by GPU line")
		.flags (nozero);

	dramCache_max_gpu_occupancy
		.name (name() + "")
		.desc("% max gpu occupancy in dramcache");

	dramCache_max_gpu_occupancy = (dramCache_max_gpu_lines / dramCache_num_sets) * 100;

	dramCache_hit_rate
		.name (name () + ".dramCache_hit_rate")
		.desc ("hit rate of dramcache");

	dramCache_hit_rate = (dramCache_read_hits + dramCache_write_hits)
           / (dramCache_read_hits + dramCache_read_misses + dramCache_write_hits
    				  + dramCache_write_misses);

	dramCache_rd_hit_rate
		.name (name () + ".dramCache_rd_hit_rate")
		.desc ("read hit rate of dramcache");

	dramCache_rd_hit_rate = (dramCache_read_hits)
          / (dramCache_read_hits + dramCache_read_misses);

	dramCache_wr_hit_rate
		.name (name () + "dramCache_wr_hit_rate")
		.desc ("write hit rate of dramcache");

	dramCache_wr_hit_rate = (dramCache_write_hits)
         / (dramCache_write_hits + dramCache_write_misses);

	dramCache_evict_rate
		.name (name () + ".dramCache_evict_rate")
		.desc ("evict rate of dramcache - evicts/accesses");

	dramCache_evict_rate = dramCache_evicts
        / (dramCache_read_hits + dramCache_read_misses + dramCache_write_hits
					+ dramCache_write_misses);

	dramCache_cpu_hit_rate
		.name (name () + "dramCache_cpu_hit_rate")
		.desc ("hit rate of dramcache for CPU");

	dramCache_cpu_hit_rate = (dramCache_cpu_hits)
        / (dramCache_cpu_hits + dramCache_cpu_misses);

	dramCache_gpu_hit_rate
		.name (name () + "dramCache_gpu_hit_rate")
		.desc ("hit rate of dramcache for GPU");

	dramCache_gpu_hit_rate =
			((dramCache_read_hits + dramCache_write_hits) - dramCache_cpu_hits)
			/ ((dramCache_read_hits + dramCache_write_hits) - dramCache_cpu_hits)
			+ ((dramCache_read_misses + dramCache_write_misses)
					- dramCache_cpu_misses);

	dramCache_correct_pred
		.name ( name() + "dramCache_correct_pred")
		.desc ("Number of correct predictions");

	dramCache_correct_pred = dramCache_total_pred - dramCache_incorrect_pred;

	blocked_cycles
		.init (NUM_BLOCKED_CAUSES)
		.name (name () + ".blocked_cycles")
		.desc ("number of cycles access was blocked")
		.subname (Blocked_NoMSHRs,"no_mshrs")
		.subname (Blocked_NoTargets, "no_targets");

	blocked_causes
		.init (NUM_BLOCKED_CAUSES)
		.name (name () + ".blocked")
		.desc ("number of times access was blocked")
		.subname (Blocked_NoMSHRs,"no_mshrs")
		.subname (Blocked_NoTargets, "no_targets");
}

BaseMasterPort &
DRAMCacheCtrl::getMasterPort (const std::string &if_name, PortID idx)
{
	if (if_name == "dramcache_masterport")
	{
		return dramCache_masterport;
	}
	else
	{
		return MemObject::getMasterPort (if_name, idx);
	}
}

MSHR *
DRAMCacheCtrl::getNextMSHR ()
{
	// Check both MSHR queue and write buffer for potential requests,
	// note that null does not mean there is no request, it could
	// simply be that it is not ready
	MSHR *miss_mshr = mshrQueue.getNextMSHR ();
	MSHR *write_mshr = writeBuffer.getNextMSHR ();

	// If we got a write buffer request ready, first priority is a
	// full write buffer, otherwhise we favour the miss requests
	if (write_mshr
			&& ((writeBuffer.isFull () && writeBuffer.inServiceEntries == 0)
					|| !miss_mshr))
	{
		// need to search MSHR queue for conflicting earlier miss.
		MSHR *conflict_mshr = mshrQueue.findPending (write_mshr->blkAddr, true);

		if (conflict_mshr && conflict_mshr->order < write_mshr->order)
		{
			// Service misses in order until conflict is cleared.
			return conflict_mshr;

			// @todo Note that we ignore the ready time of the conflict here
		}

		// No conflicts; issue write
		return write_mshr;
	}
	else if (miss_mshr)
	{
		// need to check for conflicting earlier writeback
		MSHR *conflict_mshr = writeBuffer.findPending (miss_mshr->blkAddr,
				miss_mshr->isSecure);
		if (conflict_mshr)
		{
			// not sure why we don't check order here... it was in the
			// original code but commented out.

			// The only way this happens is if we are
			// doing a write and we didn't have permissions
			// then subsequently saw a writeback (owned got evicted)
			// We need to make sure to perform the writeback first
			// To preserve the dirty data, then we can issue the write

			// should we return write_mshr here instead?  I.e. do we
			// have to flush writes in order?  I don't think so... not
			// for Alpha anyway.  Maybe for x86?
			return conflict_mshr;

			// @todo Note that we ignore the ready time of the conflict here
		}

		// No conflicts; issue read
		return miss_mshr;
	}

	return NULL;
}

PacketPtr
DRAMCacheCtrl::getTimingPacket ()
{
	MSHR *mshr = getNextMSHR ();

	if (mshr == NULL)
		return NULL;

	// use request from 1st target
	PacketPtr tgt_pkt = mshr->getTarget ()->pkt;
	PacketPtr pkt;

	DPRINTF(DRAMCache, "%s %s for addr %d size %d isFowardNoResponse %d mshr index %d\n",
			__func__, tgt_pkt->cmdString(), tgt_pkt->getAddr(),
			tgt_pkt->getSize(), mshr->isForwardNoResponse(), mshr->queue->index);

	// set the command as readReq if it was MSHR else writeReq if it was writeBuffer
	MemCmd cmd = mshr->queue->index ? MemCmd::WriteReq : MemCmd::ReadReq;

	//if MSHR (readReq) copy packet;
	// for writeBuffer (writeReq) send same packet, but it was crashing
	// so even for writeBuffer we created new packet
	if(cmd == MemCmd::ReadReq)
		pkt = new Packet(tgt_pkt,false,true);
	else
	{
		pkt = new Packet (tgt_pkt->req, cmd, dramCache_block_size);
		pkt->allocate();
		pkt->setData(tgt_pkt->getPtr<uint8_t>());
	}

	DPRINTF(DRAMCache, "MSHR packet: %d", tgt_pkt->print());
	DPRINTF(DRAMCache, "new packet: %d", pkt->print());
	pkt->senderState = mshr;

	return pkt;

}

Tick
DRAMCacheCtrl::nextMSHRReadyTime () const
{
	return std::min (mshrQueue.nextMSHRReadyTime (),
			writeBuffer.nextMSHRReadyTime ());
}

void
DRAMCacheCtrl::DRAMCacheReqPacketQueue::sendDeferredPacket ()
{
	assert(!waitingOnRetry);

	DPRINTF(DRAMCache, "inside sendDeferredPacket\n");

	PacketPtr pkt = cache.getTimingPacket();

	if (pkt == NULL)
		DPRINTF(DRAMCache, "sendDefferedPacket got no timing Packet");
	else
	{
		MSHR *mshr = dynamic_cast<MSHR*> (pkt->senderState);

		waitingOnRetry = !masterPort.sendTimingReq (pkt);

		if (waitingOnRetry){
			DPRINTF(DRAMCache, "now waiting on retry");
			delete pkt;
		}
		else{
			// pending_dirty_resp is false as the cache is after LLSC
			cache.markInService (mshr, false);
		}

		if (!waitingOnRetry)
		{
			schedSendEvent (cache.nextMSHRReadyTime ());
		}
	}

}

uint64_t
DRAMCacheCtrl::hash_pc (Addr pc)
{
	// folded xor as in http://www.jilp.org/cbp/Pierre.pdf
	uint64_t hash_pc = pc;
	for( int k = 1; k<8; k++)
		hash_pc ^= (pc>>(k*8));
	// mask higher order bits
	hash_pc = hash_pc & 0x00000000000000ff;

	return hash_pc;
}

// returns true for hit and false for miss
bool
DRAMCacheCtrl::predict(ContextID contextId, Addr pc)
{
	dramCache_total_pred++;
	if(predictor[contextId][hash_pc(pc)]>3)
		return false;
	else
		return true;
}

void
DRAMCacheCtrl::decMac(ContextID contextId, Addr pc)
{
	if(predictor[contextId][hash_pc(pc)]!=0)
		predictor[contextId][hash_pc(pc)]--;
}

void
DRAMCacheCtrl::incMac(ContextID contextId, Addr pc)
{
	if(predictor[contextId][hash_pc(pc)]!=8)
		predictor[contextId][hash_pc(pc)]++;
}

Addr
DRAMCacheCtrl::regenerateBlkAddr(uint64_t set, uint64_t tag)
{
	return ((tag * dramCache_num_sets) + set) * dramCache_block_size;
}

void
DRAMCacheCtrl::access(PacketPtr pkt)
{
	assert(pkt->isRequest());
	system()->getPhysMem().access(pkt);

}

void
DRAMCacheCtrl::respond(PacketPtr pkt, Tick latency)
{
	assert(pkt->isResponse());

	Tick response_time = curTick() + latency + pkt->headerDelay
			+ pkt->payloadDelay;

	pkt->headerDelay = pkt->payloadDelay = 0;

	port.schedTimingResp(pkt, response_time);
}
