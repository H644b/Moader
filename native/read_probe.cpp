// Ordinary, read-only MK8 diagnostic. No hooks, network or native-memory access.
// Link only libounce_core; see coordination/backend-filesystem.md for ownership.
#include <switch.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr u64 RequestedProgram = UINT64_C(0x0100152000022000);
constexpr char MountName[] = "mk8probe";
constexpr char AssetPath[] = "/UI/cmn/menu/ym_CoursePict_Gu_Cake_00^u.bntx";
constexpr char FilePath[] = "mk8probe:/UI/cmn/menu/ym_CoursePict_Gu_Cake_00^u.bntx";
constexpr u64 FileByteLimit = 4 * 1024 * 1024;
constexpr size_t ObservationLimit = 32, ChunkBytes = 16 * 1024;
struct Step { bool attempted = false; Result result = 0; };
struct Observation {
    u64 tick = 0, file_bytes = 0, bytes_read = 0;
    long long unix_time = 0;
    s64 storage_bytes = -1;
    unsigned read_calls = 0;
    const char* stage = "not_started";
    Result result = 0;
    int error_number = 0;
    bool size_valid = false, hash_complete = false;
    char sha256[SHA256_HASH_SIZE * 2 + 1]{};
};
struct Probe {
    FsStorage storage{};
    FILE* file = nullptr;
    bool storage_owned = false, mounted = false, opened_once = false, stdio_unbuffered = false;
    bool stopped = false, cleanup_done = false, storage_closed_by_sdk = false;
    unsigned storage_open_attempts = 0, storage_opens = 0, mount_attempts = 0, mounts = 0;
    unsigned file_open_attempts = 0, file_opens = 0, file_closes = 0, direct_storage_closes = 0;
    Step storage_open, initial_size, mount, unmount;
    s64 initial_storage_bytes = -1;
    int file_close_errno = 0;
    size_t count = 0;
    std::array<Observation, ObservationLimit> observations{};
    char token[17]{}, report_path[128]{};

    Probe() {
        u64 identity = 0; randomGet(&identity, sizeof(identity));
        if (!identity) identity = armGetSystemTick() | 1;
        std::snprintf(token, sizeof(token), "%016llx", static_cast<unsigned long long>(identity));
        std::snprintf(report_path, sizeof(report_path), "sdmc:/moader/read-probe-%s.json", token);
    }
    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;
    ~Probe() { close(); }

    void close() {
        if (cleanup_done) return;
        cleanup_done = true;
        if (file) {
            errno = 0;
            if (std::fclose(file)) file_close_errno = errno ? errno : EIO;
            file = nullptr; ++file_closes;
        }
        if (mounted) {
            unmount.attempted = true; unmount.result = romfsUnmount(MountName);
            storage_closed_by_sdk = R_SUCCEEDED(unmount.result);
            mounted = false;
            // The SDK mount owns this storage. Do not close its borrowed copy.
            storage = {};
        } else if (storage_owned) {
            fsStorageClose(&storage); storage_owned = false; ++direct_storage_closes;
            storage = {};
        }
    }
    bool fail(Observation& out, const char* stage, Result result = 0, int error = 0) {
        out.stage = stage; out.result = result; out.error_number = error;
        stopped = true; close(); return false;
    }
    bool open_once(Observation& out) {
        // No retry, alternate ID, permission changes or fallback mount route.
        if (opened_once) return file && mounted;
        opened_once = true;
        ++storage_open_attempts; storage_open.attempted = true;
        storage_open.result = fsOpenDataStorageByProgramId(&storage, RequestedProgram);
        if (R_FAILED(storage_open.result)) return fail(out, "open_storage", storage_open.result);
        storage_owned = true; ++storage_opens;
        initial_size.attempted = true;
        initial_size.result = fsStorageGetSize(&storage, &initial_storage_bytes);
        if (R_FAILED(initial_size.result)) return fail(out, "initial_storage_size", initial_size.result);
        if (initial_storage_bytes <= 0) return fail(out, "invalid_storage_size", 0, EINVAL);

        ++mount_attempts; mount.attempted = true;
        // Matching SDK: romfsMountFromStorage copies this service into its
        // mount, then closes it on common-path failure or romfsUnmount. Its
        // only non-consuming failure is no free slot BEFORE mount allocation.
        // This fresh standalone, single-threaded NRO mounts nothing else;
        // default __appInit mounts SD only, so all 32 RomFS slots are free.
        mount.result = romfsMountFromStorage(storage, 0, MountName);
        storage_owned = false;
        if (R_FAILED(mount.result)) {
            storage_closed_by_sdk = true; storage = {};
            return fail(out, "mount_romfs", mount.result);
        }
        mounted = true; ++mounts;
        ++file_open_attempts; errno = 0;
        file = std::fopen(FilePath, "rb");
        if (!file) return fail(out, "open_file", 0, errno ? errno : EIO);
        ++file_opens; errno = 0;
        // Set before any stream operation: later seeks/rereads cannot use
        // newlib's data buffer. The SDK still caches immutable file metadata.
        if (std::setvbuf(file, nullptr, _IONBF, 0)) return fail(out, "disable_buffering", 0, errno ? errno : EIO);
        stdio_unbuffered = true;
        return true;
    }
    void observe() {
        if (stopped || count == ObservationLimit) return;
        Observation& out = observations[count++];
        out.tick = armGetSystemTick(); out.unix_time = static_cast<long long>(std::time(nullptr));
        if (!open_once(out)) return;
        Result rc = fsStorageGetSize(&storage, &out.storage_bytes); // Borrowed from the still-live mount.
        if (R_FAILED(rc)) { fail(out, "storage_size", rc); return; }
        if (out.storage_bytes <= 0) { fail(out, "invalid_storage_size", 0, EINVAL); return; }
        struct stat st{}; errno = 0;
        if (fstat(fileno(file), &st)) { fail(out, "file_stat", 0, errno ? errno : EIO); return; }
        if (!S_ISREG(st.st_mode) || st.st_size < 0 || static_cast<u64>(st.st_size) > FileByteLimit) {
            fail(out, "file_size_limit", 0, EFBIG); return;
        }
        out.file_bytes = static_cast<u64>(st.st_size); out.size_valid = true;
        std::clearerr(file); errno = 0;
        if (std::fseek(file, 0, SEEK_SET)) { fail(out, "seek_file", 0, errno ? errno : EIO); return; }
        alignas(0x1000) std::array<unsigned char, ChunkBytes> bytes{};
        Sha256Context hash{}; sha256ContextCreate(&hash);
        while (out.bytes_read < out.file_bytes) {
            const u64 remaining = out.file_bytes - out.bytes_read;
            const size_t wanted = remaining < bytes.size() ? static_cast<size_t>(remaining) : bytes.size();
            errno = 0; ++out.read_calls;
            const size_t actual = std::fread(bytes.data(), 1, wanted, file);
            if (actual) { sha256ContextUpdate(&hash, bytes.data(), actual); out.bytes_read += actual; }
            if (actual != wanted) { fail(out, "read_file", 0, errno ? errno : EIO); return; }
        }
        errno = 0;
        if (std::fgetc(file) != EOF || std::ferror(file)) { fail(out, "file_eof", 0, errno ? errno : EIO); return; }
        unsigned char digest[SHA256_HASH_SIZE]{}; sha256ContextGetHash(&hash, digest);
        constexpr char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < sizeof(digest); ++i) { out.sha256[i * 2] = hex[digest[i] >> 4]; out.sha256[i * 2 + 1] = hex[digest[i] & 15]; }
        out.hash_complete = true; out.stage = "hashed";
    }
};
const char* boolean(bool value) { return value ? "true" : "false"; }
void step(FILE* out, const char* key, const Step& value) {
    std::fprintf(out, "\"%s\":{\"attempted\":%s,\"result\":\"0x%08x\"}", key, boolean(value.attempted), value.result);
}
int save_report(const Probe& p) {
    errno = 0;
    if (mkdir("sdmc:/moader", 0777) && errno != EEXIST) return errno ? errno : EIO;
    FILE* out = std::fopen(p.report_path, "wb");
    if (!out) return errno ? errno : EIO;
    std::fprintf(out,
        "{\"schema\":1,\"diagnostic\":\"ordinary-mk8-read-probe\",\"requestedProgramId\":\"0100152000022000\","
        "\"assetPath\":\"%s\",\"lifetimeToken\":\"%s\",\"fileByteLimit\":%llu,\"observationLimit\":%zu,"
        "\"hosVersion\":%u,\"tickFrequency\":%llu,\"stoppedOnError\":%s,\"cleanupAttempted\":%s,\"stdioUnbuffered\":%s,"
        "\"initialStorageBytes\":%lld,\"storageOpenAttempts\":%u,\"storageOpenCount\":%u,"
        "\"mountAttempts\":%u,\"mountCount\":%u,\"fileOpenAttempts\":%u,\"fileOpenCount\":%u,"
        "\"fileCloseCount\":%u,\"directStorageCloseCount\":%u,\"storageClosedBySdk\":%s,\"fileCloseErrno\":%d,"
        "\"storageOwnership\":\"SDK mount owns storage after transfer; local copy is borrowed until unmount\","
        "\"limitations\":\"ProgramId request only; no independently resolved content ID/version. RomFS caches directory/file metadata. "
        "Unbuffered content reads use SDK storage; hashes are observations, not FS receipts or game-visible proof. "
        "RomFS stdio failures expose errno, not the underlying FS Result. No raw file data exported.\",\"steps\":{",
        AssetPath, p.token, static_cast<unsigned long long>(FileByteLimit), ObservationLimit,
        hosversionGet(), static_cast<unsigned long long>(armGetSystemTickFreq()), boolean(p.stopped), boolean(p.cleanup_done), boolean(p.stdio_unbuffered),
        static_cast<long long>(p.initial_storage_bytes), p.storage_open_attempts, p.storage_opens, p.mount_attempts, p.mounts,
        p.file_open_attempts, p.file_opens, p.file_closes, p.direct_storage_closes, boolean(p.storage_closed_by_sdk), p.file_close_errno);
    step(out, "openStorage", p.storage_open); std::fputc(',', out);
    step(out, "initialStorageSize", p.initial_size); std::fputc(',', out);
    step(out, "mountRomfs", p.mount); std::fputc(',', out); step(out, "unmountRomfs", p.unmount);
    std::fprintf(out, "},\"observations\":[");
    for (size_t i = 0; i < p.count; ++i) {
        const auto& o = p.observations[i];
        std::fprintf(out,
            "%s{\"index\":%zu,\"tick\":%llu,\"unixTime\":%lld,\"lifetimeToken\":\"%s\",\"stage\":\"%s\","
            "\"result\":\"0x%08x\",\"errno\":%d,\"storageBytes\":%lld,\"fileSizeValid\":%s,\"fileBytes\":%llu,"
            "\"bytesRead\":%llu,\"readCalls\":%u,\"hashComplete\":%s,\"sha256\":\"%s\","
            "\"storageOpenCount\":%u,\"mountCount\":%u,\"fileOpenCount\":%u}",
            i ? "," : "", i + 1, static_cast<unsigned long long>(o.tick), o.unix_time, p.token, o.stage, o.result,
            o.error_number, static_cast<long long>(o.storage_bytes), boolean(o.size_valid),
            static_cast<unsigned long long>(o.file_bytes), static_cast<unsigned long long>(o.bytes_read), o.read_calls,
            boolean(o.hash_complete), o.sha256, p.storage_opens, p.mounts, p.file_opens);
    }
    std::fprintf(out, "]}\n");
    int error = std::ferror(out) ? (errno ? errno : EIO) : 0;
    if (std::fflush(out) && !error) error = errno ? errno : EIO;
    if (!error && fsync(fileno(out))) error = errno ? errno : EIO;
    if (std::fclose(out) && !error) error = errno ? errno : EIO;
    return error;
}
void draw(const Probe& p, int save_error) {
    consoleClear();
    std::printf("Moader MK8 read probe\nProgram: 0100152000022000\n%s\n\n", AssetPath);
    std::printf("A: First open/hash, then reread SAME file\n+: Close file, unmount, save and return\n\n");
    std::printf("Lifetime: %s\nObservations: %zu/%zu; file opens: %u; mounts: %u\n", p.token, p.count, ObservationLimit, p.file_opens, p.mounts);
    if (p.count) {
        const auto& o = p.observations[p.count - 1];
        std::printf("Stage: %s; Result 0x%08x; errno %d\nStorage: %lld; file: %llu; read: %llu\nSHA256: %s\n",
            o.stage, o.result, o.error_number, static_cast<long long>(o.storage_bytes),
            static_cast<unsigned long long>(o.file_bytes), static_cast<unsigned long long>(o.bytes_read), o.sha256);
    }
    if (p.stopped) std::printf("STOPPED: access/report error. No retry. Press +.\n");
    else if (p.count == ObservationLimit) std::printf("Observation limit reached. Press +.\n");
    if (save_error) std::printf("SD report failed: errno %d\n", save_error);
    else if (p.count) std::printf("Saved: %s\n", p.report_path);
    std::printf("\nHashes prove observed bytes only. No game data exported.\n");
}
}
int main() {
    if (!consoleInit(nullptr)) return 1;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad{}; padInitializeDefault(&pad);
    Probe probe; int save_error = 0;
    draw(probe, save_error);
    while (appletMainLoop()) {
        padUpdate(&pad); const u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if ((down & HidNpadButton_A) && !probe.stopped && probe.count < ObservationLimit) {
            probe.observe(); save_error = save_report(probe);
            if (save_error) { probe.stopped = true; probe.close(); }
            draw(probe, save_error);
        }
        consoleUpdate(nullptr);
    }
    probe.close();
    if (probe.count) {
        save_error = save_report(probe);
        if (save_error) { draw(probe, save_error); consoleUpdate(nullptr); }
    }
    const bool failed = probe.stopped || save_error || probe.file_close_errno || R_FAILED(probe.unmount.result);
    consoleExit(nullptr); return failed ? 1 : 0;
}
