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
	dramCache_access_count (0), dramCache_num_sets (0),
	replacement_scheme (p->dramcache_replacement_scheme), totalRows (0),
	system_cache_block_size (128), // hardcoded to 128
    num_sub_blocks_per_way (0), total_gpu_lines (0), total_gpu_dirty_lines (0),
	order (0), numTarget (p->tgts_per_mshr), blocked (0), num_cores(p->num_cores),
	dramCacheTimingMode(p->dramcache_timing)
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
				switched_to_gpu_line++;
			}
			dramCache_gpu_occupancy_per_set[cacheSet]++;
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

		// Knowing its a hit; inc the prediction and do stats
		Addr alignedAddr = pkt->getAddr() & ~(Addr(128 - 1));
		int cid = pkt->req->contextId();
		Addr pc = RubyPort::pcTable[cid][alignedAddr];
		if(predict(cid, alignedAddr) == false) // predicted miss but was hit
			dramCache_incorrect_pred++;
		incMac(cid, pc);

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

		// Knowing its a miss; dec the prediction and do stats
		Addr alignedAddr = pkt->getAddr() & ~(Addr(128 - 1));
		int cid = pkt->req->contextId();
		Addr pc = RubyPort::pcTable[cid][alignedAddr];
		if(predict(cid, alignedAddr) == true) // predicted hit but was miss
			dramCache_incorrect_pred++;
		decMac(cid, pc);

		return false;
	}
	// don't worry about sending a fill request here, that will done elsewhere
	assert(false);
	return false;
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
	// ADARSH only change we make here remove burstAlign for isInwriteQueue
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
    } else if (busState == WRITE_TO_READ) {
        DPRINTF(DRAMCache, "Switching to reads after %d writes with %d writes "
                "waiting\n", writesThisTime, writeQueue.size());

        wrPerTurnAround.sample(writesThisTime);
        writesThisTime = 0;

        busState = READ;
        switched_cmd_type = true;
    }

    // when we get here it is either a read or a write
    if (busState == READ) {

        // track if we should switch or not
        bool switch_to_writes = false;

        if (readQueue.empty()) {
            // In the case there is no read request to go next,
            // trigger writes if we have passed the low threshold (or
            // if we are draining)
            if (!writeQueue.empty() &&
                (drainState() == DrainState::Draining ||
                 writeQueue.size() > writeLowThreshold)) {

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
            if (writeQueue.size() > writeHighThreshold) {
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
        isInWriteQueue.erase(dram_pkt->addr);
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

        // If we emptied the write queue, or got sufficiently below the
        // threshold (using the minWritesPerSwitch as the hysteresis) and
        // are not draining, or we have reads waiting and have done enough
        // writes, then switch to reads.
        if (writeQueue.empty() ||
            (writeQueue.size() + minWritesPerSwitch < writeLowThreshold &&
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
			// we have now serviced all children packets of a system packet

			delete dram_pkt->burstHelper;
			dram_pkt->burstHelper = NULL;

			// Now access is complete, TAD unit available => doCacheLookup
			bool isHit = doCacheLookup (dram_pkt->pkt);

			if (!isHit)  // miss send cache fill request
			{
				// send cache fill to master port - master port sends from MSHR
				DPRINTF(DRAMCache,"allocate miss buffer for write request\n");
				allocateMissBuffer (dram_pkt->pkt, 1, true);
			}
			else
			{
				// access has already happened in addtowriteQueue
				// we now respond since know its a hit
				PacketPtr respPkt = dram_pkt->pkt;
				DPRINTF(DRAMCache,"Responding to address %d\n", respPkt->getAddr());
				// packet should not be a response yet
				assert(!respPkt->isResponse());
				respPkt->makeResponse();

				Tick response_time = curTick() + frontendLatency +
						respPkt->headerDelay + respPkt->payloadDelay;
				respPkt->headerDelay = respPkt->payloadDelay = 0;

				port.schedTimingResp(respPkt, response_time);
			}
		}
	}
	else
	{
		PacketPtr respPkt = dram_pkt->pkt;
		DPRINTF(DRAMCache,"Responding to single burst address %d\n",
				respPkt->getAddr());

		fatal("DRAMCache ctrl doesnt support single burst for write");

		// packet should not be a response yet
		assert(!respPkt->isResponse());
		respPkt->makeResponse();

		Tick response_time = curTick() + frontendLatency +
				respPkt->headerDelay + respPkt->payloadDelay;
		respPkt->headerDelay = respPkt->payloadDelay = 0;

		port.schedTimingResp(respPkt, response_time);

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
			// we have now serviced all children packets of a system packet
			// so we can now respond to the requester

			// Now access is complete, TAD unit available => doCacheLookup
			bool isHit = doCacheLookup (dram_pkt->pkt);

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

				delete dram_pkt->burstHelper;
				dram_pkt->burstHelper = NULL;
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

    unsigned pktsServicedByWrQ = 0;
    BurstHelper* burst_helper = NULL;

    for (int cnt = 0; cnt < pktCount; ++cnt) {
        //unsigned size = std::min((addr | (burstSize - 1)) + 1,
        //                pkt->getAddr() + pkt->getSize()) - addr;

    	// ADARSH for us the size is always burstSize
    	unsigned size = burstSize;

        readPktSize[ceilLog2(size)]++;
        readBursts++;

        // First check write buffer to see if the data is already at
        // the controller
        bool foundInWrQ = false;
        Addr burst_addr = addr;
        // if the burst address is not present then there is no need
        // looking any further
        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
            for (const auto& p : writeQueue) {
                // check if the read is subsumed in the write queue
                // packet we are looking at
                if (p->addr <= addr && (addr + size) <= (p->addr + p->size)) {
                    foundInWrQ = true;
                    servicedByWrQ++;
                    pktsServicedByWrQ++;
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
    if (pktsServicedByWrQ == pktCount) {
        access(pkt);
        respond(pkt, frontendLatency);
        return;
    }

    // Update how many split packets are serviced by write queue
    if (burst_helper != NULL)
        burst_helper->burstsServiced = pktsServicedByWrQ;

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
	// ADARSH packet count is ; we need to number our sets in multiplies of 2
	addr = addr * pktCount;

	BurstHelper* burst_helper = NULL;

    for (int cnt = 0; cnt < pktCount; ++cnt) {
        unsigned size = burstSize;

        writePktSize[ceilLog2(size)]++;
        writeBursts++;

        // see if we can merge with an existing item in the write
        // queue and keep track of whether we have merged or not
        /* ADARSH We are removing merging in write queue because both requests have
           to be serviced access here and respond later*/
        /*bool merged = isInWriteQueue.find(addr) !=
            isInWriteQueue.end();*/

        // if the item was not merged we need to create a new write
        // and enqueue it
        //if (!merged) {


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
		isInWriteQueue.insert(addr);
		//DPRINTF(DRAMCache, "writeQueue size - %d\n", writeQueue.size());
		//DPRINTF(DRAMCache, "isInWriteQueue size - %d\n", isInWriteQueue.size());
		assert(writeQueue.size() == isInWriteQueue.size());

		// Update stats
		avgWrQLen = writeQueue.size();

        //}
        /* ADARSH We are removing merging in write queue because both requests have
         * to be serviced access here and respond later
         * else {
            DPRINTF(DRAMCache, "Merging write burst with existing queue entry\n");

            // keep track of the fact that this burst effectively
            // disappeared as it was merged with an existing one
            mergedWrBursts++;
            pktsMergedInWrQ++;
        }
        */

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

	// operation is as follows: check MSHR hit or miss
	// do prediction using predictorTable to perform SAM or PAM
	// start DRAMCache access; verify with tag if hit or miss
	// if miss send fill request, add to MSHR
	// don't remove cache conflict line till fill req returns
	// once fill req returns, remove from MSHR, alloc cache block
	// if conflict line is dirty - allocate writeBuffer and send for WB
	// once wb ack is received, remove from writeBuffer
	// this cache is non inclusive, so there could be write miss events

	// ADARSH check MSHR if there is outstanding request for this address
	Addr blk_addr = blockAlign(pkt->getAddr());
	MSHR *mshr = mshrQueue.findMatch (blk_addr, false);
	if (mshr)
	{
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

	// perform prediction using cache address; lookup RubyPort::predictorTable
	int cid = pkt->req->contextId();
	Addr pc = RubyPort::pcTable[cid][blk_addr];
	DPRINTF(DRAMCache,"PC %d for addr: %d\n",pc, blk_addr);

	// adjust mruPcAddrList to keep it in MRU
	RubyPort::mruPcAddrList[cid].remove(blk_addr);
	RubyPort::mruPcAddrList[cid].push_front(blk_addr);

	// do prediction here and decide if this access should be SAM or PAM
	if (predict(pc, cid) == false)
	{
		// predicted as miss do PAM
		// create an entry in MSHR and which will send a request to memory
		// The MSHR needs to hold some meta data to identify if this request was
		// a PAM  or an actual SAM miss so when the resp arrives we can identify
		// what action to take

		// instead of maintaining in MSHR we use pamQueue to track PAM requests
		// pamQueue[blk_addr] = new pamReqStatus();
		// create MSHR entry
		// we are sure that MSHR entry doesn't exist for this address since if
		// there was an MSHR it would have been coalesced above
		// allocateMissBuffer(pkt,1,true);
	}

	// Find out how many dram packets a pkt translates to
	// If the burst size is equal or larger than the pkt size, then a pkt
	// translates to only one dram packet. Otherwise, a pkt translates to
	// multiple dram packets
	unsigned size = pkt->getSize ();
	//unsigned offset = pkt->getAddr () & (burstSize - 1);
	//unsigned int dram_pkt_count = divCeil (offset + size, burstSize);
	// ADARSH i know i need 5 burts for TAD; 4 for data and 1 for tag
	// we modified this to 2 bursts including tag
	unsigned int dram_pkt_count = 2;

	// check local buffers and do not accept if full
	if (pkt->isRead ())
	{
		assert(size != 0);
		if (readQueueFull (dram_pkt_count))
		{
			DPRINTF(DRAMCache, "Read queue full, not accepting\n");
			// remember that we have to retry this port
			retryRdReq = true;
			numRdRetry++;
			return false;
		}
		else
		{
			addToReadQueue (pkt, dram_pkt_count);
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
		if (writeQueueFull (dram_pkt_count))
		{
			DPRINTF(DRAMCache, "Write queue full, not accepting\n");
			// remember that we have to retry this port
			retryWrReq = true;
			numWrRetry++;
			return false;
		}
		else
		{
			addToWriteQueue (pkt, dram_pkt_count);
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

	MSHR::Target *initial_tgt = mshr->getTarget ();

	// write allocate policy - read resp do cache things
	if(mshr->queue->index == 0 && pkt->isRead())
	{
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
		}
		else if (set[cacheSet].tag == cacheTag)
		{
			// predictor could have possibly sent out a PAM request incorrectly
			// discard this and do nothing to tag directory
		}
		else if (set[cacheSet].tag != cacheTag)
		{
			Addr evictAddr = regenerateBlkAddr(cacheSet, set[cacheSet].tag);
			DPRINTF(DRAMCache, "Evicting addr %d in cacheSet %d isClean %d\n",
					evictAddr ,cacheSet, set[cacheSet].dirty);
			// this block needs to be evicted
			if (set[cacheSet].dirty)
			{
				// write back needed as this line is dirty
				// allocate writeBuffer for this block
				// reconstruct address from tag and set
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

			// change the tag directory
			set[cacheSet].tag = cacheTag;
			set[cacheSet].dirty = false;
		}

		Tick miss_latency = curTick () - initial_tgt->recvTime;
		if (pkt->req->contextId () == 31)
			dramCache_mshr_miss_latency[1] += miss_latency; //gpu mshr miss latency
		else
			dramCache_mshr_miss_latency[0] += miss_latency; //cpu mshr miss latency

		Tick completion_time = curTick() + pkt->headerDelay;

		while (mshr->hasTargets () && (mshr->queue->index == 0))
		{
			MSHR::Target *target = mshr->getTarget ();
			Packet *tgt_pkt = target->pkt;

			assert(pkt->getAddr() == tgt_pkt->getAddr());

			if (tgt_pkt->cmd == MemCmd::WriteReq)
			{
				// wont be a problem here actually because DRAM writes to backing store
				//warn("unimplemented write req resp %d", pkt->getAddr());
				DPRINTF(DRAMCache,"%s return from write miss\n", __func__);

				set[cacheSet].dirty = true;
				//Sending only response as access is already done during writeQ allocation
				tgt_pkt->makeResponse();
				port.schedTimingResp(tgt_pkt,completion_time);

			}
			else if (tgt_pkt->cmd == MemCmd::ReadReq)
			{
				DPRINTF(DRAMCache,"%s return from read miss\n", __func__);
				tgt_pkt->setData(pkt->getConstPtr<uint8_t>());

				tgt_pkt->makeResponse ();
				tgt_pkt->headerDelay = tgt_pkt->payloadDelay = 0;
				port.schedTimingResp (tgt_pkt, completion_time);
			}

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

	dramCache_read_hits.name (name () + ".dramCache_read_hits").desc (
			"Number of read hits in dramcache");

	dramCache_read_misses.name (name () + ".dramCache_read_misses").desc (
			"Number of read misses in dramcache");

	dramCache_write_hits.name (name () + ".dramCache_write_hits").desc (
			"Number of write hits in dramcache");

	dramCache_write_misses.name (name () + ".dramCache_write_misses").desc (
			"Number of write misses in dramcache");

	dramCache_evicts.name (name () + ".dramCache_evicts").desc (
			"Number of evictions from dramcache");

	dramCache_write_backs.name (name () + ".dramCache_write_backs").desc (
			"Number of write backs from dramcache");

	dramCache_write_backs_on_read.name (
			name () + ".dramCache_write_backs_on_read").desc (
					"Number of write backs caused by a read from dramcache");

	dramCache_writes_to_dirty_lines.name (
			name () + "dramCache_writes_to_dirty_lines").desc (
					"Number of writes to dirty lines in dramcache");

	dramCache_gpu_replaced_cpu.name (name () + ".dramCache_gpu_replaced_cpu").desc (
			"Number of times gpu replaced cpu line ");

	dramCache_cpu_replaced_gpu.name (name () + ".dramCache_cpu_replaced_gpu").desc (
			"Number of times cpu replaced gpu line ");

	switched_to_gpu_line.name (name () + ".switched_to_gpu_line").desc (
			"Number of times CPU line became GPU line in cache ");

	switched_to_cpu_line.name (name () + ".switched_to_cpu_line").desc (
			"Number of times GPU line became CPU line in cache ");

	dramCache_cpu_hits.name (name () + ".dramCache_cpu_hits").desc (
			"Number of hits for CPU Requests");

	dramCache_cpu_misses.name (name () + ".dramCache_cpu_misses").desc (
			"Number of misses for CPU Requests");

	dramCache_mshr_hits.name (name () + ".dramCache_mshr_hits").desc (
			"Number of hits in the MSHR");

	dramCache_cpu_mshr_hits.name (name () + ".dramCache_cpu_mshr_hits").desc (
			"Number of hits for CPU requests in the MSHR");

	dramCache_writebuffer_hits.name (name () + ".dramCache_writebuffer_hits").desc (
			"Number of hits in the writebuffer");

	dramCache_cpu_writebuffer_hits.name (name () + ".dramCache_cpu_writebuffer_hits").desc (
			"Number of hits for CPU requests in the writebuffer");

	dramCache_max_gpu_dirty_lines.name (name () + ".dramCache_max_gpu_dirty_lines").desc (
			"maximum number of gpu dirty lines in cache");

	dramCache_total_pred.name ( name() + ".dramCache_total_pred").desc(
			"total number of predictions made");

	dramCache_incorrect_pred.name ( name() + ".dramCache_incorrect_pred").desc(
				"Number of incorrect predictions");

	dramCache_mshr_miss_latency.init (2).name (
			name () + ".dramCache_mshr_miss_latency").desc (
					"total miss latency of mshr for cpu and gpu");

	dramCache_gpu_occupancy_per_set.init (dramCache_num_sets).name (
			name () + ".dramCache_gpu_occupancy_per_set").desc (
					"Number of times the way was occupied by GPU line");

	dramCache_hit_rate.name (name () + ".dramCache_hit_rate").desc (
			"hit rate of dramcache");
	dramCache_hit_rate = (dramCache_read_hits + dramCache_write_hits)
           / (dramCache_read_hits + dramCache_read_misses + dramCache_write_hits
    				  + dramCache_write_misses);

	dramCache_rd_hit_rate.name (name () + ".dramCache_rd_hit_rate").desc (
			"read hit rate of dramcache");
	dramCache_rd_hit_rate = (dramCache_read_hits)
          / (dramCache_read_hits + dramCache_read_misses);

	dramCache_wr_hit_rate.name (name () + "dramCache_wr_hit_rate").desc (
			"write hit rate of dramcache");
	dramCache_wr_hit_rate = (dramCache_write_hits)
         / (dramCache_write_hits + dramCache_write_misses);

	dramCache_evict_rate.name (name () + ".dramCache_evict_rate").desc (
			"evict rate of dramcache - evicts/accesses");
	dramCache_evict_rate = dramCache_evicts
        / (dramCache_read_hits + dramCache_read_misses + dramCache_write_hits
					+ dramCache_write_misses);

	dramCache_cpu_hit_rate.name (name () + "dramCache_cpu_hit_rate").desc (
			"hit rate of dramcache for CPU");
	dramCache_cpu_hit_rate = (dramCache_cpu_hits)
        / (dramCache_cpu_hits + dramCache_cpu_misses);

	dramCache_gpu_hit_rate.name (name () + "dramCache_gpu_hit_rate").desc (
			"hit rate of dramcache for GPU");
	dramCache_gpu_hit_rate =
			((dramCache_read_hits + dramCache_write_hits) - dramCache_cpu_hits)
			/ ((dramCache_read_hits + dramCache_write_hits) - dramCache_cpu_hits)
			+ ((dramCache_read_misses + dramCache_write_misses)
					- dramCache_cpu_misses);

	dramCache_correct_pred.name ( name() + "dramCache_correct_pred").desc (
			"Number of correct predictions");

	dramCache_correct_pred = dramCache_total_pred - dramCache_incorrect_pred;

	blocked_cycles.init (NUM_BLOCKED_CAUSES);
	blocked_cycles.name (name () + ".blocked_cycles").desc (
			"number of cycles access was blocked").subname (Blocked_NoMSHRs,
					"no_mshrs").subname (
							Blocked_NoTargets, "no_targets");

	blocked_causes.init (NUM_BLOCKED_CAUSES);
	blocked_causes.name (name () + ".blocked").desc (
			"number of times access was blocked").subname (Blocked_NoMSHRs,
					"no_mshrs").subname (
							Blocked_NoTargets, "no_targets");
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

	//if read request copy packet else write request create new packet and setData
	if(cmd == MemCmd::ReadReq)
	{
		//MSHR entry and a WriteReq
		if(tgt_pkt->cmd == MemCmd::WriteReq)
		{
//			pkt = new Packet (tgt_pkt->req, cmd, dramCache_block_size);
			pkt = new Packet(tgt_pkt,false,true);
			pkt->cmd = cmd;
		}
		//MSHR entry and a ReadReq
		else
			pkt = new Packet(tgt_pkt,false,true);
	}
	else //WriteBuffer entry and a WritebackReq
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
