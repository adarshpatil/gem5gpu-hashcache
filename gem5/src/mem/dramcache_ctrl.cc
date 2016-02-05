/*
 * Copyright (c) 2010-2015 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2013 Amin Farmahini-Farahani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Andreas Hansson
 *          Ani Udipi
 *          Neha Agarwal
 *          Omar Naji
 */

#include "mem/dramcache_ctrl.hh"

using namespace std;
using namespace Data;

DRAMCacheCtrl::DRAMCacheCtrl(const DRAMCacheCtrlParams* p) :
    DRAMCtrl(p), memoryport(name() + ".memoryport", *this),
	dramCache_size(p->dramcache_size),dramCache_assoc(p->dramcache_assoc),
	dramCache_block_size(p->dramcache_block_size)
{
	inform("DRAMCacheCtrl constructor\n");
}

void
DRAMCacheCtrl::init()
{
	DRAMCtrl::init();
}

DRAMCacheCtrl*
DRAMCacheCtrlParams::create()
{
	inform("Create DRAMCacheCtrl\n");
    return new DRAMCacheCtrl(this);
}

DRAMCacheCtrl::MemoryMasterPort::MemoryMasterPort(const std::string& name,
	DRAMCacheCtrl& _dramcache): QueuedMasterPort(name, &_dramcache, reqqueue, snoopdummy),
	reqqueue(_dramcache, *this), snoopdummy(_dramcache, *this), dramcache(_dramcache)
{
}

BaseMasterPort &
DRAMCacheCtrl::getMasterPort(const std::string &if_name, PortID idx)
{
    if (if_name == "memoryport") {
        return memoryport;
    }  else {
        return MemObject::getMasterPort(if_name, idx);
    }
}

void
DRAMCacheCtrl::regStats()
{
	using namespace Stats;
    DRAMCtrl::regStats();

    dramCache_read_hits
        .name(name() + ".dramCache_read_hits")
        .desc("Number of read hits in dramcache");

    dramCache_read_misses
        .name(name() + ".dramCache_read_misses")
        .desc("Number of read misses in dramcache");

    dramCache_write_hits
        .name(name() + ".dramCache_write_hits")
        .desc("Number of write hits in dramcache");

    dramCache_write_misses
        .name(name() + ".dramCache_write_misses")
        .desc("Number of write misses in dramcache");

    dramCache_evicts
	    .name(name() + ".dramCache_evicts")
		.desc("Number of evictions from dramcache");

    dramCache_write_backs
	    .name(name() + ".dramCache_write_backs")
		.desc("Number of write backs from dramcache");

    dramCache_writes_to_dirty_lines
	    .name(name() + "dramCache_writes_to_dirty_lines")
		.desc("Number of writes to dirty lines in dramcache");

    dramCache_hit_rate
	    .name(name() + ".dramCache_hit_rate")
		.desc("hit rate of dramcache");
    dramCache_hit_rate = (dramCache_read_hits + dramCache_write_hits)/
    		            (dramCache_read_hits + dramCache_read_misses +
    		            dramCache_write_hits + dramCache_write_misses);

    dramCache_rd_hit_rate
	    .name(name() + ".dramCache_rd_hit_rate")
		.desc("read hit rate of dramcache");
    dramCache_rd_hit_rate = (dramCache_read_hits)/(dramCache_read_hits+dramCache_read_misses);

    dramCache_wr_hit_rate
	    .name(name() + "dramCache_wr_hit_rate")
		.desc("write hit rate of dramcache");
	dramCache_wr_hit_rate = (dramCache_write_hits)/(dramCache_write_hits+dramCache_write_misses);

    dramCache_evict_rate
	    .name(name() + ".dramCache_evict_rate")
		.desc("evict rate of dramcache");
    dramCache_evict_rate = dramCache_evicts /
    		            (dramCache_read_hits + dramCache_read_misses +
    		    		dramCache_write_hits + dramCache_write_misses);

}

