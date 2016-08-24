/*
 * Copyright (c) Indian Institute of Science
 * All rights reserved
 *
 * Authors: Adarsh Patil
 */

#include "mem/dramcache_ctrl.hh"
#include "debug/DRAMCache.hh"
#include "debug/Drain.hh"

using namespace std;
using namespace Data;

DRAMCacheCtrl::DRAMCacheCtrl (const DRAMCacheCtrlParams* p) :
    DRAMCtrl (p), dramCache_masterport (name () + ".dramcache_masterport",
					*this), mshrQueue (
	"MSHRs", p->mshrs, 4, p->demand_mshr_reserve, MSHRQueue_MSHRs), writeBuffer (
	"write buffer", p->write_buffers, p->mshrs + 1000, 0,
	MSHRQueue_WriteBuffer), dramCache_size (p->dramcache_size), dramCache_assoc (
	p->dramcache_assoc), dramCache_block_size (p->dramcache_block_size), dramCache_access_count (
	0), dramCache_num_sets (0), replacement_scheme (
	p->dramcache_replacement_scheme), totalRows (0), system_cache_block_size (
	128), // hardcoded to 128
    num_sub_blocks_per_way (0), total_gpu_lines (0), total_gpu_dirty_lines (0), order (
	0), numTarget (p->tgts_per_mshr), blocked (0)
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

	DPRINTF(
			DRAMCache,
			"DRAMCache totalRows:%d columnsPerStripe:%d, channels:%d, banksPerRank:%d, ranksPerChannel:%d, columnsPerRowBuffer:%d, rowsPerBank:%d, burstSize:%d\n",
			totalRows, columnsPerStripe, channels, banksPerRank, ranksPerChannel,
			columnsPerRowBuffer, rowsPerBank, burstSize);

	// columsPerRowBuffer is actually rowBufferSize/burstSize => each column is 1 burst
	// rowBufferSize = devicesPerRank * deviceRowBufferSize
	//    for DRAM Cache devicesPerRank = 1 => rowBufferSize = deviceRowBufferSize = 2kB
	//    for DDR3 Mem   devicesPerRank = 8 => 8kB
	// DRAMCache totalRows = 64k
	// DRAMCache sets = 960k
	// Assoc is hard to increase but we can increase dramCache_block_size
}

void
DRAMCacheCtrl::init ()
{
	DRAMCtrl::init ();
	int i, j;
	set = new dramCacheSet_t[dramCache_num_sets];
	DPRINTF(DRAMCache, "dramCache_num_sets: %d\n", dramCache_num_sets);

	// initialize sub block counter for each set - num_cache_blocks_per_way
	for (i = 0; i < dramCache_num_sets; i++)
	{
		set[i].tag = 0;
		set[i].valid = false;
		set[i].dirty = false;
		set[i].used = new bool[num_sub_blocks_per_way];
		set[i].accessed = new uint64_t[num_sub_blocks_per_way];
		set[i].written = new uint64_t[num_sub_blocks_per_way];
		for (j = 0; j < num_sub_blocks_per_way; j++)
		{
			set[i].used[j] = false;
			set[i].accessed[j] = 0;
			set[i].written[j] = 0;
		}
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
	DPRINTF(DRAMCache, "pktAddr:%d, blockid:%d set:%d tag:%d\n", pkt->getAddr(),
			cacheBlock, cacheSet, cacheTag);
	assert(cacheSet >= 0);
	assert(cacheSet < dramCache_num_sets);

	// check if the tag matches and line is valid
	if ((set[cacheSet].tag == cacheTag) && set[cacheSet].valid)
	{
		// HIT
		if (pkt->req->contextId () == 31) // it is a GPU req
		{
			DPRINTF(DRAMCache, "GPU request %d is a hit\n", pkt->getAddr());
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
			DPRINTF(DRAMCache, "CPU request %d is a hit\n", pkt->getAddr());
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

		// TODO do the sub block thing here NOT YET IMPLEMENTED
		return true;

	}
	else
	{
		// MISS
		if (pkt->req->contextId () == 31) // it is a GPU req
		{
			DPRINTF(DRAMCache, "GPU request %d is a miss\n", pkt->getAddr());
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
			DPRINTF(DRAMCache, "CPU request %d is a miss\n", pkt->getAddr());
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

		return false;
	}
	// don't worry about sending a fill request here, that will done elsewhere
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

	// ADARSH here the address should be the address of a set
	// cuz DRAMCache is addressed by set
	dramPktAddr = dramPktAddr / dramCache_block_size;
	dramPktAddr = dramPktAddr % dramCache_num_sets;

	// truncate the address to a DRAM burst, which makes it unique to
	// a specific column, row, bank, rank and channel
	Addr addr = dramPktAddr / burstSize;
	DPRINTF(DRAMCache, "decode addr:%ld, dramPktAddr:%ld\n", addr, dramPktAddr);

	// we have removed the lowest order address bits that denote the
	// position within the column
	// Using addrMapping == Enums::RoCoRaBaCh)
	// optimise for closed page mode and utilise maximum
	// parallelism of the DRAM (at the cost of power)

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
DRAMCacheCtrl::processRespondEvent ()
{
	DPRINTF(DRAMCache,
			"processRespondEvent(): Some req has reached its readyTime\n");

	DRAMPacket* dram_pkt = respQueue.front ();

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
				allocateMissBuffer (dram_pkt->pkt, tBURST, true);
			}
			else
			{
				// continue to accessAndRespond if it is a hit

				// @todo we probably want to have a different front end and back
				// end latency for split packets
				accessAndRespond (dram_pkt->pkt,
						frontendLatency + backendLatency);
				delete dram_pkt->burstHelper;
				dram_pkt->burstHelper = NULL;
			}
		}
	}
	else
	{
		// it is not a split packet
		accessAndRespond (dram_pkt->pkt, frontendLatency + backendLatency);
	}

	delete respQueue.front ();
	respQueue.pop_front ();

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
				&& readQueue.empty ())
		{

			DPRINTF(Drain, "DRAM Cache controller done draining\n");
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

bool
DRAMCacheCtrl::recvTimingReq (PacketPtr pkt)
{
	/// @todo temporary hack to deal with memory corruption issues until
	/// 4-phase transactions are complete
	for (int x = 0; x < pendingDelete.size (); x++)
		delete pendingDelete[x];
	pendingDelete.clear ();

	// This is where we enter from the outside world
	DPRINTF(
			DRAMCache,
			"ADARSH recvTimingReq: request %s addr %lld size %d, ContextId: %d Threadid: %d\n",
			pkt->cmdString(), pkt->getAddr(), pkt->getSize(), pkt->req->contextId(),
			pkt->req->threadId());

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
		return false;

	// Calc avg gap between requests
	if (prevArrival != 0)
	{
		totGap += curTick () - prevArrival;
	}
	prevArrival = curTick ();

	// operation is as follows: check hit or miss
	// if miss send fill request, add to MSHR
	// don't remove cache conflict line till fill req returns
	// once fill req returns, remove from MSHR, alloc cache block
	// if conflict line is dirty - allocate writeBuffer and send for WB
	// once received wb ack remove from writeBuffer

	// ADARSH check MSHR if there is outstanding request for this address
	Addr blk_addr = blockAlign (pkt->getAddr ());
	MSHR *mshr = mshrQueue.findMatch (blk_addr, false);
	if (mshr)
	{
		DPRINTF(DRAMCache, "%s coalescing MSHR for %s addr %#llx\n", __func__,
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
	mshr = writeBuffer.findMatch (blk_addr, false);
	if (mshr)
	{
		DPRINTF(DRAMCache, "%s coalescing WriteBuffer for %s addr %#llx\n",
				__func__, pkt->cmdString(), pkt->getAddr());
		dramCache_writebuffer_hits++;
		if (pkt->req->contextId () != 31)
			dramCache_cpu_writebuffer_hits++;

		mshr->allocateTarget (pkt, pkt->headerDelay, order++);
		if (mshr->getNumTargets () == numTarget)
		{
			setBlocked (Blocked_NoTargets);
		}
		return true;
	}

	// Find out how many dram packets a pkt translates to
	// If the burst size is equal or larger than the pkt size, then a pkt
	// translates to only one dram packet. Otherwise, a pkt translates to
	// multiple dram packets
	unsigned size = pkt->getSize ();
	unsigned offset = pkt->getAddr () & (burstSize - 1);
	unsigned int dram_pkt_count = divCeil (offset + size, burstSize);

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
		neitherReadNorWrite++;
		accessAndRespond (pkt, 1);
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

	// find the cache block, set id and tag
	//unsigned int cacheBlock = pkt->getAddr()/dramCache_block_size;
	//unsigned int cacheSet = cacheBlock % dramCache_num_sets;
	//unsigned int cacheTag = cacheBlock / dramCache_num_sets;

	Tick miss_latency = curTick () - initial_tgt->recvTime;
	if (pkt->req->contextId () == 31)
		dramCache_mshr_miss_latency[1] += miss_latency; //gpu mshr miss latency
	else
		dramCache_mshr_miss_latency[0] += miss_latency; //cpu mshr miss latency

	while (mshr->hasTargets ())
	{
		MSHR::Target *target = mshr->getTarget ();
		Packet *tgt_pkt = target->pkt;

		if (tgt_pkt->cmd == MemCmd::WriteReq)
		{
			// if the original packet was a write request
		}
		tgt_pkt->makeTimingResponse ();
		Tick completetion_time = pkt->headerDelay;
		tgt_pkt->headerDelay = tgt_pkt->payloadDelay = 0;
		port.schedTimingResp (tgt_pkt, completetion_time);

		mshr->popTarget ();
	}

	mq->deallocate (mshr);
	if (wasFull)
		clearBlocked ((BlockedCause) mq->index);

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

	dramCache_mshr_miss_latency.init (2).name (
			name () + ".dramCache_mshr_miss_latency").desc (
					"total miss latency of mshr for cpu and gpu");

	dramCache_gpu_occupancy_per_set.init (dramCache_num_sets).name (
			name () + ".dramCache_gpu_occupancy_per_way").desc (
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

	DPRINTF(DRAMCache, "get timing packet\n");
	DPRINTF(DRAMCache, "%s %s for addr %d size %d isFowardNoResponse %d\n",
			__func__, tgt_pkt->cmdString(), tgt_pkt->getAddr(),
			tgt_pkt->getSize(), mshr->isForwardNoResponse());

	// set the command as readReq if it was MSHR else writeReq if it was writeBuffer
	MemCmd cmd = (mshr->queue->index - 1) ? MemCmd::ReadReq : MemCmd::WriteReq;

	pkt = new Packet (tgt_pkt->req, cmd, dramCache_block_size);
	tgt_pkt->senderState = mshr;

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

	PacketPtr pkt = cache.getTimingPacket ();
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
