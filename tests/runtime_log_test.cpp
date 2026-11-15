#include "moader/runtime_backend.hpp"
#include "moader/runtime_log_wire.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

extern "C" {
void native_log_setup(const char*), native_log_event(unsigned), native_log_retry(), native_log_stop();
void native_log_route(uint64_t,uint64_t,uint64_t), native_log_fault(int,int,int),native_log_report_failure(),native_log_busy(int);
unsigned native_log_next_slot(); uint32_t native_log_result(); uint64_t native_log_sequence();
}
static std::string read(const std::string& path) {
    std::ifstream file(path,std::ios::binary); assert(file);
    return {(std::istreambuf_iterator<char>(file)),{}};
}
static void write(const std::string& path,const std::string& bytes) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc); file.write(bytes.data(),bytes.size()); assert(file.good());
}
static bool has(const std::vector<std::string>& rows,const std::string& value) {
    return std::any_of(rows.begin(),rows.end(),[&](const std::string& row){return row.find(value)!=std::string::npos;});
}
static void repair_header(std::string& bytes) {
    MoaderLogHeader h{};std::memcpy(&h,bytes.data(),sizeof(h));
    h.header_crc=moader_log_crc32(&h,offsetof(MoaderLogHeader,header_crc));std::memcpy(bytes.data(),&h,sizeof(h));
}
int main(int argc,char** argv) {
    assert(argc==2);const std::string root=argv[1];
    std::filesystem::create_directories(root+"/runtime");
    const std::string paths[]={root+"/runtime/log0.bin",root+"/runtime/log1.bin"};
    for(const auto& path:paths)write(path,std::string(MOADER_LOG_CAPACITY,'\0'));
    moader::RuntimeBackend backend(root,{11,22});
    assert(has(backend.events(),"first snapshot"));
    native_log_setup(root.c_str());
    auto rows=backend.events();assert(rows.size()==3&&has(rows,"Activation complete: 1"));
    assert(!has(rows,"diagnostics:")); // unused zero-filled slot is expected
    native_log_event(1);rows=backend.events();assert(rows.size()==4&&has(rows,"0000000000000001"));
    assert(backend.events()==rows); // overlapping tails never duplicate a row

    // Simulated Horizon TargetLocked, busy log lock and transient native read
    // failure preserve routing metadata and the other successfully closed slot.
    const unsigned target=native_log_next_slot(),good=target^1;
    const auto saved=read(paths[good]);
    native_log_fault(static_cast<int>(target),0,0);native_log_event(2);
    assert(native_log_result()==0xe02&&read(paths[good])==saved&&backend.events()==rows);
    native_log_fault(-1,0,1);native_log_retry();assert(native_log_result()==0x321&&read(paths[good])==saved);
    native_log_fault(-1,0,0);native_log_busy(1);const auto sequence=native_log_sequence();
    native_log_route(2,11,22);assert(native_log_sequence()==sequence);native_log_busy(0);native_log_retry();
    rows=backend.events();assert(rows.size()==5&&has(rows,"0000000000000002"));

    for(int failure=1;failure<=3;++failure) {
        const auto last=read(paths[native_log_next_slot()^1]);
        native_log_fault(-1,failure,0);native_log_event(10+failure);
        assert(native_log_result()!=0&&read(paths[native_log_next_slot()^1])==last);
        native_log_fault(-1,0,0);native_log_retry();assert(!native_log_result());
        rows=backend.events();assert(has(rows,"/asset"));
    }
    const unsigned undersized=native_log_next_slot();
    const auto prepared=read(paths[undersized]),other=read(paths[undersized^1]);
    write(paths[undersized],std::string(99,'\0'));native_log_event(14);
    assert(native_log_result()==0xd401&&read(paths[undersized^1])==other);
    write(paths[undersized],prepared);native_log_retry();assert(!native_log_result());rows=backend.events();

    // Reject stale epoch, corrupt header/payload, impossible lengths, zero
    // sequence, truncation and oversize. Every rejection retains cached rows.
    const auto valid0=read(paths[0]),valid1=read(paths[1]);
    for(unsigned fault=0;fault<13;++fault) {
        std::string bytes=valid0;MoaderLogHeader h{};std::memcpy(&h,bytes.data(),sizeof(h));
        switch(fault) {
        case 0:h.epoch_low=99;break;
        case 1:h.sequence=0;break;
        case 2:h.payload_size=MOADER_LOG_PAYLOAD+1;break;
        case 3:h.end_offset=h.start_offset-1;break;
        case 4:bytes[0]='X';break;
        case 5:bytes[sizeof(h)]^=1;break;
        case 6:bytes.resize(sizeof(h)+2);break;
        case 7:bytes+='X';break;
        case 8:h.header_size=64;break;
        case 9:h.sequence=native_log_sequence();++h.generation;break;
        case 10:h.sequence=native_log_sequence()+2;h.generation=0;break;
        case 11:h.epoch_low=h.epoch_high=0;break;
        case 12:bytes[sizeof(h)]='\0';h.payload_crc=moader_log_crc32(bytes.data()+sizeof(h),h.payload_size);break;
        }
        if(fault<=3||fault>=8){std::memcpy(bytes.data(),&h,sizeof(h));repair_header(bytes);}
        write(paths[0],bytes);write(paths[1],std::string(MOADER_LOG_CAPACITY,'\0'));
        const auto failed=backend.events();assert(has(failed,"Runtime diagnostics:"));
        for(const auto& row:rows)if(row.rfind("Runtime: ",0)==0)assert(has(failed,row));
    }
    write(paths[0],valid0);write(paths[1],valid1);
    std::filesystem::rename(paths[0],paths[0]+".hidden");std::filesystem::rename(paths[1],paths[1]+".hidden");
    assert(has(backend.events(),"open failed"));
    std::filesystem::rename(paths[0]+".hidden",paths[0]);std::filesystem::rename(paths[1]+".hidden",paths[1]);

    // Concurrent real reader and single native log owner; each opened slot
    // closes, and racing reads must either validate or retain the last copy.
    std::thread reader([&]{for(unsigned i=0;i<80;++i){const auto out=backend.events();assert(!out.empty()&&out.size()<=129);}});
    for(unsigned i=20;i<90;++i)native_log_event(i);
    reader.join();rows=backend.events();assert(has(rows,"0000000000000059"));
    // Cross 64 KiB: valid lengths exclude zero padding; only an initial partial
    // line is discarded. A new app instance reads the newest bounded tail.
    native_log_fault(static_cast<int>(native_log_next_slot()),0,0);
    for(unsigned i=100;i<1300;++i)native_log_event(i);
    native_log_fault(-1,0,0);native_log_retry();
    moader::RuntimeBackend fresh(root,{11,22});rows=fresh.events();
    assert(rows.size()<=128&&has(rows,"0000000000000513"));
    native_log_report_failure();assert(has(backend.events(),"native report writer failed"));

    // Current service epoch isolation, including generation zero on restart.
    moader::RuntimeBackend next(root,{33,44});assert(!has(next.events(),"Runtime: "));
    native_log_route(0,33,44);assert(has(next.events(),"Runtime: "));
    assert(has(backend.events(),"Native event 0100152000022000 0000000000000513")); // old epoch keeps its last valid cache
    native_log_stop();
    std::cout<<"PASS: actual C producer -> C++ reader, locks, busy retry, torn/CRC/epoch bounds, concurrent reads, 64 KiB tails and ownership\n";
}
