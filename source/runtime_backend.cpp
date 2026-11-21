#include "moader/runtime_backend.hpp"
#include "moader/runtime_wire.h"
#include "moader/runtime_log_wire.h"
#include "moader/library.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <map>
#include <stdexcept>
#include <sys/stat.h>
#include <tuple>
#include <cerrno>
#include <memory>

namespace moader {
namespace {
struct Entry { uint64_t title, size; uint32_t kind; std::string name, source; };
std::string folded(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return s; }
Entry entry(const Mod& mod, const std::string& path, const std::string& root) {
    checked_relative_path(path);
    Entry out{}; out.title = std::stoull(mod.title_id,nullptr,16);
    if (path.rfind("romfs/",0) == 0) { out.kind = 1; out.name = '/' + path.substr(6); }
    else if (path.rfind("exefs/",0) == 0) {
        out.kind = 2; out.name = path.substr(6);
        if (out.name.find('/') != std::string::npos) throw std::runtime_error("ExeFS modules must be directly inside exefs/.");
    } else if (path == "romfs.bin") { out.kind = 4; out.name = path; }
    else if (path == "exefs.nsp") { out.kind = 5; out.name = path; }
    else if (path.rfind("exefs_patches/",0) == 0) {
        out.kind = 6; out.name = path.substr(14);
        if (out.name.size() < 5 || folded(out.name.substr(out.name.size()-4)) != ".ips")
            throw std::runtime_error("Executable patches currently require binary IPS files.");
    } else throw std::runtime_error("Unsupported mod payload: " + path);
    const auto relative = install_path("/library/" + folded(mod.title_id) + '/' + mod.id, mod.install_slot) + '/' + path;
    struct stat st{};
    if (lstat((root + relative).c_str(), &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > 64*1024*1024)
        throw std::runtime_error("The mod source is missing, linked, or exceeds 64 MiB: " + path);
    out.size = st.st_size;
    out.source = "/moader" + relative;
    return out;
}
}
RuntimeBackend::RuntimeBackend(std::string root, std::array<uint64_t,2> epoch,
                               std::function<void()> kick)
    : root_(std::move(root)), epoch_(epoch), kick_(std::move(kick)) {
    if (!(epoch_[0] | epoch_[1])) throw std::invalid_argument("A nonzero runtime epoch is required.");
    make_directories(root_ + "/runtime");
    recover_atomic_file(root_ + "/runtime/catalogue.bin");
    // Preserve a live FS writer's inode. Receipts are fixed size and CRC checked.
    struct stat st{};
    if (lstat((root_ + "/runtime/ack.bin").c_str(), &st))
        atomic_write(root_ + "/runtime/ack.bin", std::string(sizeof(MoaderRuntimeAck),'\0'));
    else if (!S_ISREG(st.st_mode)) throw std::runtime_error("Runtime receipt path is not a regular file.");
}
RuntimeStatus RuntimeBackend::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    RuntimeStatus out; out.backend = "moader-filesystem";
    out.detail = "Waiting for the installed filesystem runtime to acknowledge this session.";
    std::ifstream file(root_ + "/runtime/ack.bin",std::ios::binary);
    MoaderRuntimeAck ack{};
    if (!file.read(reinterpret_cast<char*>(&ack),sizeof(ack))) return out;
    if (std::memcmp(ack.magic,MOADER_ACK_MAGIC,8) || ack.version != MOADER_WIRE_VERSION ||
        ack.crc != moader_crc32(&ack,offsetof(MoaderRuntimeAck,crc)) ||
        ack.epoch_low != epoch_[0] || ack.epoch_high != epoch_[1]) return out;
    out.connected = ack.flags & 1;
    out.applied_generation = ack.generation;
    out.result = ack.result;
    if (ack.result) {
        char code[32]; std::snprintf(code,sizeof(code),"0x%08x",ack.result);
        out.detail = std::string("FS rejected the catalogue: ") + code + ". Previous routing remains in use.";
    } else if (ack.generation < requested_generation_) {
        out.detail = "The library changed. Waiting for FS to accept the new routing.";
    } else {
        out.detail = "FS accepted routing. Same-sized loose replacements can affect later reads. Cached assets, images, additions and executable changes may need content to reopen or the game to relaunch.";
    }
    return out;
}
void RuntimeBackend::publish(uint64_t generation, const std::vector<Mod>& mods) {
    std::vector<Entry> entries;
    for (const auto& mod : mods) if (mod.requested && mod.load_allowed) {
        if (!valid_title_id(mod.title_id) || !valid_mod_id(mod.id)) throw std::runtime_error("Invalid mod catalogue identity.");
        if (!mod.valid_source) throw std::runtime_error(mod.detail);
        for (const auto& path : mod.files) {
            if (entries.size() >= MOADER_MAX_ROWS) throw std::runtime_error("Runtime catalogue exceeds 16384 files.");
            entries.push_back(entry(mod,path,root_));
        }
    }
    std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b) {
        return std::make_tuple(a.title,a.kind,folded(a.name)) < std::make_tuple(b.title,b.kind,folded(b.name));
    });
    std::string bytes(sizeof(MoaderCatalogueHeader) + entries.size()*sizeof(MoaderCatalogueRow),'\0');
    std::vector<MoaderCatalogueRow> rows;
    for (const auto& row : entries) {
        MoaderCatalogueRow wire{}; wire.title_id = row.title; wire.size = row.size; wire.kind = row.kind;
        wire.name_offset = bytes.size(); bytes += row.name; bytes += '\0';
        wire.source_offset = bytes.size(); bytes += row.source; bytes += '\0'; rows.push_back(wire);
    }
    if (bytes.size() > MOADER_MAX_CATALOGUE) throw std::runtime_error("Runtime catalogue exceeds 8 MiB.");
    if (!rows.empty()) std::memcpy(&bytes[sizeof(MoaderCatalogueHeader)],rows.data(),rows.size()*sizeof(rows[0]));
    MoaderCatalogueHeader header{}; std::memcpy(header.magic,MOADER_CATALOGUE_MAGIC,8);
    header.version = MOADER_WIRE_VERSION; header.rows = rows.size(); header.generation = generation;
    header.epoch_low = epoch_[0]; header.epoch_high = epoch_[1];
    header.strings_offset = sizeof(header) + rows.size()*sizeof(MoaderCatalogueRow); header.total_size = bytes.size();
    header.payload_crc = moader_crc32(bytes.data()+sizeof(header),bytes.size()-sizeof(header));
    std::memcpy(&bytes[0],&header,sizeof(header));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        atomic_write(root_ + "/runtime/catalogue.bin",bytes); requested_generation_ = generation;
    }
    if (kick_) kick_();
}
void RuntimeBackend::poll() {
    const auto current = status();
    uint64_t requested;
    { std::lock_guard<std::mutex> lock(mutex_); requested = requested_generation_; }
    if ((!current.connected || current.applied_generation < requested) && kick_) kick_();
}
std::vector<std::string> RuntimeBackend::events() const {
    std::lock_guard<std::mutex> lock(mutex_);
    MoaderLogHeader newest{}; std::string text, failures;
    auto failure = [&failures](unsigned slot,const std::string& stage,int error = 0) {
        if (!failures.empty()) failures += "; ";
        failures += "log" + std::to_string(slot) + ": " + stage;
        if (error) failures += " (errno " + std::to_string(error) + ')';
    };
    for (unsigned slot = 0; slot < 2; ++slot) {
        errno = 0;
        std::unique_ptr<FILE,decltype(&std::fclose)> file(
            std::fopen((root_ + "/runtime/log" + std::to_string(slot) + ".bin").c_str(),"rb"),std::fclose);
        const int open_error = errno;
        if (!file) { failure(slot,"open failed",open_error); continue; }
        errno = 0;
        if (std::fseek(file.get(),0,SEEK_END)) { const int error = errno; failure(slot,"size seek failed",error); continue; }
        errno = 0; const long size = std::ftell(file.get()); const int size_error = errno;
        if (size < 0) { failure(slot,"size query failed",size_error); continue; }
        if (size != MOADER_LOG_CAPACITY) { failure(slot,"unexpected file size"); continue; }
        errno = 0;
        if (std::fseek(file.get(),0,SEEK_SET)) { const int error = errno; failure(slot,"header seek failed",error); continue; }
        MoaderLogHeader header{}; errno = 0;
        if (std::fread(&header,1,sizeof(header),file.get()) != sizeof(header)) {
            const int error = errno; failure(slot,"short or failed header read",error); continue;
        }
        // An untouched precreated slot is normal until the first publication.
        const MoaderLogHeader zero{};
        if (!std::memcmp(&header,&zero,sizeof(header))) continue;
        if (std::memcmp(header.magic,MOADER_LOG_MAGIC,8) || header.version != MOADER_LOG_VERSION ||
            header.header_size != sizeof(header) || !header.sequence || !(header.epoch_low | header.epoch_high) ||
            header.payload_size > MOADER_LOG_PAYLOAD || header.end_offset < header.start_offset ||
            header.end_offset - header.start_offset != header.payload_size ||
            header.start_offset != (header.end_offset > MOADER_LOG_PAYLOAD ? header.end_offset - MOADER_LOG_PAYLOAD : 0) ||
            header.report_failed > 1 || header.header_crc != moader_log_crc32(&header,offsetof(MoaderLogHeader,header_crc))) {
            failure(slot,"invalid or torn header"); continue;
        }
        if (header.epoch_low != epoch_[0] || header.epoch_high != epoch_[1]) { failure(slot,"different runtime epoch"); continue; }
        std::string payload(header.payload_size,'\0'); errno = 0;
        if (std::fread(payload.data(),1,payload.size(),file.get()) != payload.size()) {
            const int error = errno; failure(slot,"short or failed payload read",error); continue;
        }
        if (header.payload_crc != moader_log_crc32(payload.data(),payload.size()) || payload.find('\0') != std::string::npos) {
            failure(slot,"invalid or torn payload"); continue;
        }
        if (header.sequence < log_sequence_ || header.end_offset < log_end_ || header.generation < log_generation_ ||
            (header.sequence == log_sequence_ && header.header_crc != log_crc_)) {
            // The other alternating slot normally contains the previous copy.
            if (header.sequence >= log_sequence_) failure(slot,"regressed snapshot");
            continue;
        }
        if (newest.sequence && header.sequence == newest.sequence &&
            (header.header_crc != newest.header_crc || header.payload_crc != newest.payload_crc)) {
            failure(slot,"conflicting snapshot sequence"); newest = {}; text.clear(); continue;
        }
        if (header.sequence > newest.sequence) { newest = header; text = std::move(payload); }
    }
    if (newest.sequence > log_sequence_) {
        // Positions identify rows across overlapping tails without deduplicating
        // distinct events that happen to have identical text.
        size_t at = 0;
        if (newest.start_offset) { const auto newline = text.find('\n'); at = newline == std::string::npos ? text.size() : newline + 1; }
        while (at < text.size()) {
            const auto end = text.find('\n',at); if (end == std::string::npos) break;
            const uint64_t position = newest.start_offset + at;
            if (end > at && (log_rows_.empty() || position > log_rows_.back().first)) {
                log_rows_.emplace_back(position,"Runtime: " + text.substr(at,std::min<size_t>(end-at,1024)));
                if (log_rows_.size() > 128) log_rows_.erase(log_rows_.begin());
            }
            at = end + 1;
        }
        log_sequence_ = newest.sequence; log_end_ = newest.end_offset;
        log_generation_ = newest.generation; log_crc_ = newest.header_crc;
        log_report_failed_ = newest.report_failed;
    }
    std::vector<std::string> out;
    for (const auto& row : log_rows_) out.push_back(row.second);
    if (!log_sequence_ && failures.empty()) failures = "waiting for the native runtime's first snapshot";
    if (log_sequence_ && !newest.sequence && failures.empty()) failures = "no snapshot matches the latest verified sequence";
    if (log_report_failed_) {
        if (!failures.empty()) failures += "; ";
        failures += "native report writer failed";
    }
    if (!failures.empty()) out.push_back("Runtime diagnostics: " + failures + (log_sequence_ ? "; retaining last valid events." : "."));
    return out;
}
}
