#include "moader/library.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

namespace moader {
namespace {
constexpr uint64_t MaxArchive = 128ull << 20, MaxExpanded = 256ull << 20, MaxFile = 64ull << 20;
uint16_t u16(const unsigned char* p) { return p[0] | uint16_t(p[1]) << 8; }
uint32_t u32(const unsigned char* p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
void require(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
std::vector<unsigned char> read_at(std::ifstream& f, uint64_t at, size_t n) {
    f.clear(); f.seekg(at); std::vector<unsigned char> b(n);
    require(bool(f.read(reinterpret_cast<char*>(b.data()), n)), "ZIP is truncated."); return b;
}
struct Entry { std::string name, target; uint16_t method, flags; uint32_t crc, packed, size, offset; bool dir; };
bool allowed_root(const std::string& p) {
    return p == "romfs" || p == "exefs" || p == "exefs_patches" ||
        p == "romfs.bin" || p == "exefs.nsp" || p == "mod.meta" ||
        p.compare(0, 6, "romfs/") == 0 || p.compare(0, 6, "exefs/") == 0 ||
        p.compare(0, 14, "exefs_patches/") == 0;
}
}
void extract_mod_zip(const std::string& archive, const std::string& destination) {
    std::ifstream in(archive, std::ios::binary | std::ios::ate);
    require(bool(in), "Cannot open the uploaded ZIP.");
    const uint64_t length = static_cast<uint64_t>(in.tellg());
    require(length >= 22 && length <= MaxArchive, "ZIP must be between 22 bytes and 128 MiB.");
    const auto tail = read_at(in, length - std::min<uint64_t>(length, 65557), std::min<uint64_t>(length, 65557));
    size_t end = tail.size();
    for (size_t i = tail.size() - 22;; --i) {
        if (u32(tail.data() + i) == 0x06054b50 && i + 22 + u16(tail.data() + i + 20) == tail.size()) { end = i; break; }
        if (!i) break;
    }
    require(end != tail.size(), "ZIP end record was not found.");
    const auto* eocd = tail.data() + end;
    require(!u16(eocd + 4) && !u16(eocd + 6) && u16(eocd + 8) == u16(eocd + 10), "Split ZIP files are unsupported.");
    const uint32_t count = u16(eocd + 10), central_size = u32(eocd + 12), central_at = u32(eocd + 16);
    require(count && count <= 4096 && central_size <= (2u << 20), "ZIP has too many files or too much metadata.");
    const uint64_t absolute_end = length - tail.size() + end;
    require(uint64_t(central_at) + central_size == absolute_end, "ZIP64 and unexpected ZIP layouts are unsupported.");
    auto central = read_at(in, central_at, central_size);
    std::vector<Entry> files; size_t pos = 0; uint64_t expanded = 0;
    for (uint32_t index = 0; index < count; ++index) {
        require(pos + 46 <= central.size() && u32(central.data() + pos) == 0x02014b50, "Invalid ZIP directory entry.");
        const auto* c = central.data() + pos;
        const size_t name_len = u16(c + 28), extra = u16(c + 30), comment = u16(c + 32);
        require(name_len && name_len <= 256 && pos + 46 + name_len + extra + comment <= central.size(), "Invalid ZIP filename size.");
        Entry ent; ent.name.assign(reinterpret_cast<const char*>(c + 46), name_len);
        ent.dir = ent.name.back() == '/';
        if (ent.dir) ent.name.pop_back();
        checked_relative_path(ent.name);
        ent.flags = u16(c + 8); ent.method = u16(c + 10); ent.crc = u32(c + 16);
        ent.packed = u32(c + 20); ent.size = u32(c + 24); ent.offset = u32(c + 42);
        require(!(ent.flags & 1) && (ent.method == 0 || ent.method == 8), "Encrypted ZIPs and this compression method are unsupported.");
        const uint32_t kind = (u32(c + 38) >> 16) & 0170000;
        require(kind == 0 || kind == 0100000 || kind == 0040000, "ZIP contains links or special files.");
        require(ent.size <= MaxFile && ent.packed <= MaxArchive && uint64_t(ent.offset) + 30 <= central_at, "ZIP file exceeds the supported bounds.");
        expanded += ent.size; require(expanded <= MaxExpanded, "Expanded ZIP exceeds 256 MiB.");
        pos += 46 + name_len + extra + comment;
        if (ent.name.compare(0, 9, "__MACOSX/") == 0 || ent.name == "__MACOSX" ||
            ent.name == ".DS_Store" || (ent.name.size() > 10 && ent.name.substr(ent.name.size() - 10) == "/.DS_Store")) continue;
        files.push_back(std::move(ent));
    }
    require(pos == central.size(), "Unexpected extra ZIP directory data.");
    std::string prefix; bool prefix_set = false; unsigned payload_count = 0;
    // A mod may have one enclosing folder. All real files must share it.
    for (const auto& ent : files) if (!ent.dir) {
        std::string candidate;
        if (!allowed_root(ent.name)) {
            auto slash = ent.name.find('/');
            require(slash != std::string::npos && allowed_root(ent.name.substr(slash + 1)),
                "ZIP must contain romfs/, exefs/, exefs_patches/, romfs.bin or exefs.nsp, optionally inside one folder.");
            candidate = ent.name.substr(0, slash + 1);
        }
        require(!prefix_set || prefix == candidate, "ZIP contains multiple unrelated mod roots.");
        prefix = candidate; prefix_set = true;
        if (ent.name.substr(candidate.size()) != "mod.meta") ++payload_count;
    }
    require(payload_count != 0, "ZIP has no mod files.");
    std::set<std::string> unique;
    make_directories(destination);
    for (auto& ent : files) {
        if (ent.dir && (!prefix.empty() && ent.name + '/' == prefix)) continue;
        require(ent.name.compare(0, prefix.size(), prefix) == 0, "ZIP directory is outside the mod root.");
        ent.target = ent.name.substr(prefix.size()); checked_relative_path(ent.target);
        require(allowed_root(ent.target), "ZIP contains an unsupported file at its root.");
        std::string folded = ent.target;
        for (char& c : folded) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        require(unique.insert(folded).second, "ZIP has duplicate filenames on the SD filesystem.");
        const auto full = destination + '/' + ent.target;
        if (ent.dir) { make_directories(full); continue; }
        require(ent.target != "romfs" && ent.target != "exefs" && ent.target != "exefs_patches",
            "The romfs and exefs roots must be directories.");
        const auto local = read_at(in, ent.offset, 30);
        require(u32(local.data()) == 0x04034b50 && u16(local.data() + 8) == ent.method &&
            u16(local.data() + 6) == ent.flags, "ZIP local header differs from its directory.");
        const uint32_t local_name_size = u16(local.data() + 26), local_extra = u16(local.data() + 28);
        const auto local_name = read_at(in, uint64_t(ent.offset) + 30, local_name_size);
        const std::string expected = ent.name;
        require(std::string(local_name.begin(), local_name.end()) == expected, "ZIP local filename differs from its directory.");
        const uint64_t data_at = uint64_t(ent.offset) + 30 + local_name_size + local_extra;
        require(data_at + ent.packed <= central_at, "ZIP payload overlaps its directory.");
        make_directories(full.substr(0, full.find_last_of('/')));
        FILE* out = std::fopen(full.c_str(), "wbx");
        require(out != nullptr, "Cannot create an extracted file.");
        z_stream stream{}; bool inflating = false;
        try {
            unsigned char source[32768], output[32768]; uint64_t remaining = ent.packed, written = 0;
            uint32_t crc = crc32(0, Z_NULL, 0); int result = Z_OK;
            if (ent.method == 8) {
                const int initialized = inflateInit2(&stream, -MAX_WBITS);
                if (initialized != Z_OK) throw std::runtime_error("Cannot initialize ZIP decoder for \"" + ent.target +
                    "\" (zlib " + std::to_string(initialized) + ").");
                inflating = true;
            }
            in.clear(); in.seekg(data_at);
            while (remaining) {
                const size_t n = std::min<uint64_t>(remaining, sizeof(source));
                require(bool(in.read(reinterpret_cast<char*>(source), n)), "ZIP payload is truncated."); remaining -= n;
                stream.next_in = source; stream.avail_in = n;
                do {
                    size_t produced = n;
                    const unsigned char* data = source;
                    if (inflating) {
                        stream.next_out = output; stream.avail_out = sizeof(output);
                        const auto input_before = stream.avail_in;
                        result = inflate(&stream, Z_NO_FLUSH);
                        // An exact output boundary can trigger an empty-input
                        // pump. Refill only with no progress and input remaining;
                        // the same result at actual EOF must still reject.
                        if (result == Z_BUF_ERROR && !input_before && !stream.avail_in &&
                            stream.avail_out == sizeof(output) && remaining) break;
                        if (result != Z_OK && result != Z_STREAM_END) throw std::runtime_error("ZIP decompression failed for \"" +
                            ent.target + "\" (zlib " + std::to_string(result) + ").");
                        produced = sizeof(output) - stream.avail_out; data = output;
                    }
                    require(written + produced <= ent.size, "ZIP expands beyond its declared size.");
                    require(std::fwrite(data, 1, produced, out) == produced, "Cannot write extracted file.");
                    crc = crc32(crc, data, produced); written += produced;
                    if (!inflating || result == Z_STREAM_END) break;
                } while (stream.avail_in || !stream.avail_out);
                require(result != Z_STREAM_END || (!remaining && !stream.avail_in), "ZIP has trailing compressed data.");
            }
            if ((inflating && result != Z_STREAM_END) || written != ent.size || crc != ent.crc)
                throw std::runtime_error("ZIP size or CRC verification failed for \"" + ent.target +
                    "\" (zlib " + std::to_string(result) + ").");
            if (inflating) { inflateEnd(&stream); inflating = false; }
            const int close_result = std::fclose(out); out = nullptr;
            require(close_result == 0, "Cannot close extracted file.");
        } catch (...) { if (inflating) inflateEnd(&stream); if (out) std::fclose(out); throw; }
    }
}
Mod Library::import_zip(const std::string& title, const std::string& id,
                        const std::string& archive, const std::string& display_name) {
    std::unique_lock<std::mutex> operation(operation_mutex_, std::try_to_lock);
    require(operation.owns_lock() && !importing_, "An import or another library operation is in progress. Try again when it finishes.");
    auto mods = cached_inventory();
    require(mods.size() < 512, "The library supports at most 512 mods.");
    const auto destination = mod_path(title, id);
    struct stat st{};
    require(lstat(destination.c_str(), &st) != 0, "A mod with this ID already exists. Use a new ID for this version.");
    require(errno == ENOENT, "Cannot inspect the import destination. Check SD access and refresh the library.");
    require(!display_name.empty() && display_name.size() <= 120, "Choose a mod name of at most 120 bytes.");
    for (unsigned char c : display_name) require(c >= 32 && c != 127, "Mod name contains a control character.");
    static std::atomic<unsigned> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto stage = root_ + "/.incoming/" + std::to_string(now) + '-' + std::to_string(++counter);
    // Reserve this import without holding the publication lock during decode.
    // Availability-only withdrawal can proceed; other user mutations stay busy.
    importing_ = true;
    operation.unlock();
    try {
        // Failed extraction stays outside the active library for inspection.
        extract_mod_zip(archive, stage);
        atomic_write(stage + "/mod.meta", display_name);
        operation.lock();
        // Availability may have advanced the cache/generation during extraction.
        // Merge the new entry into that current inventory, never the old copy.
        mods = cached_inventory();
        require(lstat(destination.c_str(), &st) != 0, "A mod with this ID already exists. Use a new ID for this version.");
        require(errno == ENOENT, "Cannot inspect the import destination. Check SD access and refresh the library.");
        make_directories(destination.substr(0, destination.find_last_of('/')));
        require(rename(stage.c_str(), destination.c_str()) == 0, "Cannot publish the imported mod.");
        std::string canonical_title = title;
        for (char& c : canonical_title) if (c >= 'A' && c <= 'F') c += 'a' - 'A';
        size_t count = 0;
        for (const auto& mod : mods) count += mod.files.size();
        try { mods.push_back(scan_mod(canonical_title, id, count)); }
        catch (const std::exception& e) { scan_failed(e.what()); throw; }
        std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) {
            return a.title_id < b.title_id || (a.title_id == b.title_id && a.id < b.id);
        });
        commit(std::move(mods), true); event("Imported disabled: " + title + '/' + id);
        importing_ = false;
        for (const auto& mod : snapshot().mods) if (mod.title_id == canonical_title && mod.id == id) return mod;
        throw std::runtime_error("Imported mod could not be indexed.");
    } catch (...) {
        if (!operation.owns_lock()) operation.lock();
        importing_ = false;
        throw;
    }
}
}
