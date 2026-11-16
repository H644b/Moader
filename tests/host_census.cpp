#include "moader/host_census.hpp"
#define LIBNX_HOST_TEST_TLS 1
#include "../runtime/kaihen/hen/source/census.hpp"
#include <cassert>
#include <thread>
#include <atomic>
#include <iostream>
using moader::HostCensus;
static KaihenCensusHeader header(uint64_t first=1, uint64_t next=4, uint64_t boot=11) {
    return {KAIHEN_CENSUS_MAGIC, 1, 64, 64, 64, first > 1 ? 1u : 0u,
            boot, first, next, first-1, static_cast<uint32_t>(next-first), KAIHEN_CENSUS_KNOWN_SOURCE_MASK, 19200000};
}
static KaihenCensusRecord record(uint64_t seq, uint64_t tick=0) {
    return {seq,tick,0,KaihenCensusSource_Hen,KaihenCensusEvent_HenCounts,0,KaihenCensusFlag_Cached,1,2,3};
}
int main() {
    // Consume the actual producer's ring/wire rather than matching only an
    // independently assembled fixture (including its source-mask convention).
    HenCensus producer; producer.initialize(777,19200000);
    for(uint64_t i=0;i<75;++i)
        producer.emit(i%2?KaihenCensusSource_Hen:KaihenCensusSource_Sm,
                      i%2?KaihenCensusEvent_HenCounts:KaihenCensusEvent_SmCounts,
                      i,0,0,0,KaihenCensusFlag_Cached,0,i%2?i+2:i);
    moader::HostCensus actual(123); KaihenCensusAck actual_ack{};
    auto actual_header=producer.header();
    assert(actual.receive(KAIHEN_CENSUS_HEADER_COMMAND,&actual_header,64,actual_ack));
    for(uint64_t i=actual_header.first_sequence;i<actual_header.next_sequence;++i) {
        KaihenCensusRecord from_producer{}; assert(producer.read(i,from_producer));
        assert(actual.receive(KAIHEN_CENSUS_RECORD_COMMAND,&from_producer,64,actual_ack));
    }
    HostCensus::Snapshot actual_snapshot;
    assert(actual.snapshot(actual_snapshot) && actual_snapshot.count==64 && actual_snapshot.receiver_id==123 && actual_snapshot.tick_regressions);
    HostCensus c; KaihenCensusAck ack{99,99}; HostCensus::Snapshot s;
    auto h=header(); auto r=record(1);
    bool busy=true;
    assert(!c.receive(KAIHEN_CENSUS_RECORD_COMMAND,&r,64,ack,&busy) && !busy);
    assert(!c.receive(KAIHEN_CENSUS_HEADER_COMMAND,&h,63,ack));
    assert(ack.boot_id==99 && ack.sequence==99);
    for (int field=0; field<15; ++field) {
        auto bad=h;
        switch(field) {
        case 0: bad.magic=0; break; case 1: bad.schema=2; break;
        case 2: bad.header_bytes=63; break; case 3: bad.record_bytes=63; break;
        case 4: bad.capacity=65; break; case 5: bad.capacity=0; break;
        case 6: bad.flags=2; break; case 7: bad.boot_id=0; break;
        case 8: bad.first_sequence=0; break; case 9: bad.next_sequence=0; break;
        case 10: bad.record_count=65; break; case 11: bad.source_mask=128; break;
        case 12: bad.tick_frequency=0; break; case 13: bad.overwritten=1; break;
        case 14: bad.source_mask=0; break;
        }
        assert(!c.receive(KAIHEN_CENSUS_HEADER_COMMAND,&bad,64,ack));
    }
    assert(c.receive(KAIHEN_CENSUS_HEADER_COMMAND,&h,64,ack) && ack.boot_id==11 && ack.sequence==0);
    assert(c.receive(KAIHEN_CENSUS_HEADER_COMMAND,&h,64,ack));
    for(int field=0;field<6;++field) {
        auto bad=r;
        switch(field) {case 0:bad.source=4;break;case 1:bad.event=16;break;case 2:bad.flags=64;break;
        case 3:bad.flags=24;break;case 4:bad.sequence=0;break;case 5:bad.sequence=4;break;}
        assert(!c.receive(KAIHEN_CENSUS_RECORD_COMMAND,&bad,64,ack));
    }
    r=record(1,3); assert(c.receive(KAIHEN_CENSUS_RECORD_COMMAND,&r,64,ack) && ack.sequence==1);
    assert(!c.receive(KAIHEN_CENSUS_RECORD_COMMAND,&r,64,ack));
    r=record(3,2); assert(c.receive(KAIHEN_CENSUS_RECORD_COMMAND,&r,64,ack));
    r=record(2,4); assert(!c.receive(KAIHEN_CENSUS_RECORD_COMMAND,&r,64,ack));
    assert(c.snapshot(s) && s.count==2 && s.sequence_gaps==1 && s.last_sequence==3 && s.tick_regressions==1);
    h=header(3,67); assert(c.receive(KAIHEN_CENSUS_HEADER_COMMAND,&h,64,ack));
    for(uint64_t i=4;i<67;++i) {r=record(i,i+1);assert(c.receive(KAIHEN_CENSUS_RECORD_COMMAND,&r,64,ack));}
    assert(c.snapshot(s) && s.count==64 && s.records[0].sequence==3 && s.records[63].sequence==66);
    h=header(4,68); assert(c.receive(KAIHEN_CENSUS_HEADER_COMMAND,&h,64,ack));
    r=record(67,68); assert(c.receive(KAIHEN_CENSUS_RECORD_COMMAND,&r,64,ack));
    h=header(3,67); assert(!c.receive(KAIHEN_CENSUS_HEADER_COMMAND,&h,64,ack));
    h=header(1,2,12); assert(c.receive(KAIHEN_CENSUS_HEADER_COMMAND,&h,64,ack));
    assert(c.snapshot(s) && s.last_record_boot_id==11 && s.last_record_generation==1 && s.header.boot_id==12 && s.count==0);
    r=record(1,1); assert(c.receive(KAIHEN_CENSUS_RECORD_COMMAND,&r,64,ack));
    assert(c.snapshot(s) && s.generation==2 && s.count==1 && s.last_sequence==1);
    h=header(4,68,11); assert(!c.receive(KAIHEN_CENSUS_HEADER_COMMAND,&h,64,ack));
    assert(c.snapshot(s) && s.last_status==HostCensus::Status::RetiredBoot && s.last_header_frame.boot_id==11 && s.header.boot_id==12);
    // Concurrent writer/readers operate only through the actual cache API.
    HostCensus concurrent; std::atomic<bool> done{false};
    std::thread writer([&] {
        for(uint64_t i=1;i<=20000;++i) {
            auto wh=header(i>64?i-63:1,i+1,55); KaihenCensusAck a{};
            bool was_busy=false;
            while(!concurrent.receive(KAIHEN_CENSUS_HEADER_COMMAND,&wh,64,a,&was_busy)) {
                assert(was_busy); std::this_thread::yield();
            }
            assert(!was_busy);
            auto wr=record(i,i);
            while(!concurrent.receive(KAIHEN_CENSUS_RECORD_COMMAND,&wr,64,a,&was_busy)) {
                assert(was_busy); std::this_thread::yield();
            }
            assert(!was_busy);
        }
        done=true;
    });
    while(!done) {
        HostCensus::Snapshot v;
        if(!concurrent.snapshot(v))continue;
        assert(v.count<=64);
        for(uint32_t i=0;i<v.count;++i) {
            assert(v.records[i].sequence>=v.header.first_sequence && v.records[i].sequence<v.header.next_sequence);
            if(i)assert(v.records[i-1].sequence<v.records[i].sequence);
        }
    }
    writer.join(); assert(concurrent.snapshot(s) && s.accepted_records==20000);
    std::cout<<"PASS: fixed census wire validation, wrap, gaps, boot reset, stale frames and concurrent snapshots\n";
}
