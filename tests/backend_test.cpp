#include "moader/runtime_backend.hpp"
#include "moader/runtime_wire.h"
#include "moader/runtime_log_wire.h"
#include "moader/library.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void expect(bool condition,const char* text) { if (!condition) throw std::runtime_error(text); }
static void receipt(const std::string& root,uint64_t generation,uint32_t result = 0,uint64_t epoch = 11,bool corrupt = false) {
    MoaderRuntimeAck ack{}; std::memcpy(ack.magic,MOADER_ACK_MAGIC,8); ack.version = MOADER_WIRE_VERSION;
    ack.generation = generation; ack.result = result; ack.epoch_low = epoch; ack.epoch_high = 22; ack.flags = 1;
    ack.crc = moader_crc32(&ack,offsetof(MoaderRuntimeAck,crc)); if (corrupt) ++ack.crc;
    moader::atomic_write(root + "/runtime/ack.bin",std::string(reinterpret_cast<const char*>(&ack),sizeof(ack)));
}
int main(int argc,char** argv) {
    try {
        expect(argc == 4,"Pass the original ZIP, an empty test directory and the replacement ZIP.");
        const std::string root = argv[2],title = "04000000000d0001";
        unsigned kicks = 0; moader::RuntimeBackend backend(root,{11,22},[&] { ++kicks; });
        moader::Library library(root,backend);
        library.import_zip(title,"fixture",argv[1],"Catalogue fixture");
        auto mod = library.set_enabled(title,"fixture",true);
        expect(mod.applied == moader::ApplyState::Pending && kicks == 2,"Publishing files alone must not claim application.");
        std::filesystem::copy_file(root + "/runtime/catalogue.bin",root + "/native-fixture.bin");
        std::ifstream file(root + "/native-fixture.bin",std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)),{});
        MoaderCatalogueHeader header{}; std::memcpy(&header,bytes.data(),sizeof(header));
        expect(header.rows == 2 && header.total_size == bytes.size() && header.generation == library.generation(),"Catalogue header must describe actual payload.");
        expect(header.payload_crc == moader_crc32(bytes.data()+sizeof(header),bytes.size()-sizeof(header)),"Catalogue CRC must match serialized records.");
        receipt(root,library.generation(),0,99); expect(!backend.status().connected,"Prior service epoch must not establish current-boot success.");
        receipt(root,library.generation(),0,11,true); expect(!backend.status().connected,"Torn or corrupt native receipt must reject.");
        receipt(root,library.generation());
        expect(backend.status().connected && library.list()[0].applied == moader::ApplyState::ReopenContent,"A matching receipt must retain the existing-mount qualification.");
        receipt(root,library.generation()-1,0xd401);
        expect(backend.status().result == 0xd401 && library.list()[0].applied == moader::ApplyState::Error,"Native rejection must be visible.");
        library.set_enabled(title,"fixture",false);
        receipt(root,library.generation()); mod = library.list()[0];
        expect(!mod.requested && mod.applied == moader::ApplyState::ReopenContent,"Disabling routing must not claim old mounts or game caches were cleared.");
        const std::string log = "Moader routing published\nRomFS mounted\n";
        MoaderLogHeader log_header{}; std::memcpy(log_header.magic,MOADER_LOG_MAGIC,8);
        log_header.version = MOADER_LOG_VERSION; log_header.header_size = sizeof(log_header);
        log_header.epoch_low = 11; log_header.epoch_high = 22; log_header.sequence = 1;
        log_header.generation = library.generation(); log_header.end_offset = log.size(); log_header.payload_size = log.size();
        log_header.payload_crc = moader_log_crc32(log.data(),log.size());
        log_header.header_crc = moader_log_crc32(&log_header,offsetof(MoaderLogHeader,header_crc));
        std::string snapshot(MOADER_LOG_CAPACITY,'\0');
        std::memcpy(snapshot.data(),&log_header,sizeof(log_header)); std::memcpy(snapshot.data()+sizeof(log_header),log.data(),log.size());
        moader::atomic_write(root + "/runtime/log0.bin",snapshot);
        moader::atomic_write(root + "/runtime/log1.bin",std::string(MOADER_LOG_CAPACITY,'\0'));
        expect(backend.events().size() == 2 && library.events_json().find("Runtime: RomFS mounted") != std::string::npos,"Native diagnostic output must reach the debug API.");
        moader::RuntimeBackend next(root,{33,44}); expect(!next.status().connected,"Restarted service must require a fresh native receipt.");
        moader::RuntimeBackend gated(root + "/gated-backend",{55,66});
        moader::Mod blocked; blocked.title_id = title; blocked.id = "blocked";
        blocked.requested = true; blocked.load_allowed = false; blocked.valid_source = false;
        blocked.detail = "This unavailable payload must not be opened or validated.";
        blocked.files = {"romfs/missing.bin"};
        gated.publish(1,{blocked});
        std::ifstream gated_file(root + "/gated-backend/runtime/catalogue.bin",std::ios::binary);
        MoaderCatalogueHeader gated_header{};
        expect(bool(gated_file.read(reinterpret_cast<char*>(&gated_header),sizeof(gated_header))) && gated_header.rows == 0,
               "An unavailable requested mod must be omitted before source validation or file access.");
        library.remove(title, "fixture");
        moader::Library reopened(root, backend);
        expect(reopened.list().empty(), "A removed legacy installation must stay out of the Library after restart.");
        const auto fresh = reopened.import_zip(title, "fixture", argv[3], "Reinstalled fixture");
        expect(!fresh.requested && !fresh.was_activated, "Reinstalling must start with fresh settings.");
        reopened.set_enabled(title, "fixture", true);
        std::ifstream routes(root + "/runtime/catalogue.bin", std::ios::binary);
        MoaderCatalogueRow row{};
        expect(bool(routes.read(reinterpret_cast<char*>(&header), sizeof(header))) && header.rows == 1 &&
               bool(routes.read(reinterpret_cast<char*>(&row), sizeof(row))), "Expected one replacement file.");
        routes.seekg(row.source_offset);
        std::string source;
        std::getline(routes, source, '\0');
        expect(source.rfind("/moader/library/", 0) == 0, "The new route must point into the library.");
        const auto read_text = [](const std::string& path) {
            std::ifstream stream(path);
            return std::string(std::istreambuf_iterator<char>(stream), {});
        };
        const auto old_file = root + "/library/" + title + "/fixture/romfs/config.txt";
        const auto new_file = root + source.substr(7);
        expect(read_text(old_file) == "configuration" && read_text(new_file) == "replacement",
               "Earlier mounts and new mounts must each read the payload for their installation.");
        moader::Library restarted(root, backend);
        const auto active = restarted.list();
        expect(active.size() == 1 && active.front().id == "fixture" && active.front().requested,
               "Restarting must restore the active copy and its enabled setting.");
        restarted.remove(title, "fixture");
        const auto again = restarted.import_zip(title, "fixture", argv[1], "Third install");
        expect(!again.requested && !again.was_activated && restarted.list().size() == 1 &&
               read_text(new_file) == "replacement", "Repeated reinstalls must preserve the prior copy.");
        std::cout << "PASS: catalogue publication, epoch and CRC receipts, native errors, disable semantics and runtime logs\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
