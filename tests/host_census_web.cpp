// Loopback-only integration driver using the actual WebServer and census cache.
#include "moader/web.hpp"
#include "moader/host_census.hpp"
#include <iostream>
int main(int argc, char** argv) {
    if(argc != 4) return 1;
    moader::UnavailableBackend backend; moader::Library library(argv[1],backend);
    moader::HostCensus cache;
    KaihenCensusHeader h{KAIHEN_CENSUS_MAGIC,1,64,64,64,0,UINT64_MAX,1,2,0,1,KAIHEN_CENSUS_SOURCE_MASK(KaihenCensusSource_Hen),19200000};
    KaihenCensusRecord r{1,UINT64_MAX-1,42,1,KaihenCensusEvent_HenCounts,0,KaihenCensusFlag_Cached,3,4,5};
    KaihenCensusAck ack{};
    if(!cache.receive(KAIHEN_CENSUS_HEADER_COMMAND,&h,64,ack) || !cache.receive(KAIHEN_CENSUS_RECORD_COMMAND,&r,64,ack)) return 2;
    moader::WebServer server(library,argv[1],argv[2],{},std::string(argv[3])=="cache"?&cache:nullptr);
    if(!server.start("127.0.0.1",0))return 3;
    std::cout<<server.port()<<std::endl;
    std::string stop; std::getline(std::cin,stop); server.stop(); return 0;
}
