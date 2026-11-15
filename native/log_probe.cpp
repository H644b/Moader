// Ordinary NRO: fixed-path SD reads and separately owned diagnostic outputs only.
#include <switch.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
constexpr char ReportPath[] = "sdmc:/moader/log-probe.json";
constexpr char CapturePath[] = "sdmc:/moader/log-probe-capture.log";
constexpr size_t PreviewLimit = 256, LogLimit = 1024 * 1024;
struct Step { bool attempted = false; Result result = 0; };
struct Target {
    const char* path;
    Step entry, open, size, read;
    FsDirEntryType type{};
    s64 observed_size = -1;
    u64 requested = 0, actual = 0;
    bool file_closed = false, read_complete = false;
    size_t preview_size = 0;
    std::array<unsigned char, PreviewLimit> preview{};
};
struct Report {
    Step sd;
    bool complete = false, sd_closed = false, capture_eligible = false, capture_attempted = false;
    int capture_error = 0;
    std::string error;
    std::array<Target, 3> targets{{{"/moader/runtime/runtime.log"},
        {"/moader/runtime/catalogue.bin"}, {"/moader/runtime/ack.bin"}}};
    std::vector<unsigned char> capture;
};
struct SdOwner {
    FsFileSystem value{};
    bool owned = false;
    Report& report;
    explicit SdOwner(Report& report) : report(report) {}
    ~SdOwner() { if (owned) { fsFsClose(&value); report.sd_closed = true; } }
    SdOwner(const SdOwner&) = delete;
    SdOwner& operator=(const SdOwner&) = delete;
};
struct FileOwner {
    FsFile value{};
    bool owned = false;
    Target& target;
    explicit FileOwner(Target& target) : target(target) {}
    ~FileOwner() { if (owned) { fsFileClose(&value); target.file_closed = true; } }
    FileOwner(const FileOwner&) = delete;
    FileOwner& operator=(const FileOwner&) = delete;
};
const char* boolean(bool value) { return value ? "true" : "false"; }
void quote(FILE* out, const std::string& text) {
    std::fputc('"', out);
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { std::fputc('\\', out); std::fputc(c, out); }
        else if (c < 32 || c >= 127) std::fprintf(out, "\\u%04x", c);
        else std::fputc(c, out);
    }
    std::fputc('"', out);
}
void step(FILE* out, const char* name, const Step& value) {
    std::fprintf(out, "\"%s\":{\"attempted\":%s,\"raw\":", name, boolean(value.attempted));
    if (value.attempted) std::fprintf(out, "%u,\"hex\":\"0x%08x\",\"module\":%u,\"description\":%u}",
        value.result, value.result, R_MODULE(value.result), R_DESCRIPTION(value.result));
    else std::fprintf(out, "null,\"hex\":null,\"module\":null,\"description\":null}");
}
void run(Report& report) {
    SdOwner sd(report);
    report.sd.attempted = true;
    report.sd.result = fsOpenSdCardFileSystem(&sd.value);
    if (R_FAILED(report.sd.result)) { report.complete = true; return; }
    sd.owned = true;
    for (size_t i = 0; i < report.targets.size(); ++i) {
        auto& target = report.targets[i];
        std::printf("Reading %s ...\n", target.path); consoleUpdate(nullptr);
        target.entry.attempted = true;
        target.entry.result = fsFsGetEntryType(&sd.value, target.path, &target.type);
        // Open independently even if entry lookup failed; record both Results.
        FileOwner file(target);
        target.open.attempted = true;
        target.open.result = fsFsOpenFile(&sd.value, target.path, FsOpenMode_Read, &file.value);
        if (R_FAILED(target.open.result)) continue;
        file.owned = true;
        target.size.attempted = true;
        target.size.result = fsFileGetSize(&file.value, &target.observed_size);
        if (R_FAILED(target.size.result) || target.observed_size < 0) continue;
        const u64 size = static_cast<u64>(target.observed_size);
        target.requested = i == 0 && size <= LogLimit ? size : std::min<u64>(size, PreviewLimit);
        // One bounded read per opened file, including size zero; no retries.
        std::vector<unsigned char> bytes(std::max<size_t>(1, static_cast<size_t>(target.requested)));
        target.read.attempted = true;
        target.read.result = fsFileRead(&file.value, 0, bytes.data(), target.requested,
                                       FsReadOption_None, &target.actual);
        target.read_complete = R_SUCCEEDED(target.read.result) && target.actual == target.requested;
        if (R_FAILED(target.read.result) || target.actual > target.requested) continue;
        target.preview_size = std::min<size_t>(static_cast<size_t>(target.actual), PreviewLimit);
        std::copy_n(bytes.begin(), target.preview_size, target.preview.begin());
        if (i == 0 && target.read_complete) {
            bytes.resize(static_cast<size_t>(target.actual));
            report.capture = std::move(bytes); report.capture_eligible = true;
        }
    }
    report.complete = true;
}
// Exclusive creation never truncates an existing report/capture. On any output
// error, preserve what was written and do not retry, rename or delete anything.
int finish(FILE* file) {
    int error = std::ferror(file) ? (errno ? errno : EIO) : 0;
    if (std::fflush(file) && !error) error = errno ? errno : EIO;
    if (!error && fsync(fileno(file))) error = errno ? errno : EIO;
    if (std::fclose(file) && !error) error = errno ? errno : EIO;
    return error;
}
void capture(Report& report) {
    if (!report.capture_eligible) return;
    report.capture_attempted = true; errno = 0;
    FILE* file = std::fopen(CapturePath, "wbx");
    if (!file) { report.capture_error = errno ? errno : EIO; return; }
    if (!report.capture.empty() && std::fwrite(report.capture.data(), 1, report.capture.size(), file) != report.capture.size()) {
        report.capture_error = errno ? errno : EIO;
    }
    const int error = finish(file);
    if (!report.capture_error) report.capture_error = error;
}
int save(const Report& report) {
    errno = 0;
    FILE* out = std::fopen(ReportPath, "wbx");
    if (!out) return errno ? errno : EIO;
    std::fprintf(out, "{\"schema\":1,\"revision\":1,\"diagnostic\":\"MoaderLogProbe\",\"complete\":%s,"
        "\"previewLimit\":%zu,\"wholeLogLimit\":%zu,\"sdClosed\":%s,\"sdOpen\":",
        boolean(report.complete), PreviewLimit, LogLimit, boolean(report.sd_closed));
    // step() emits a key, so the filesystem-open result is a named subobject.
    std::fputc('{', out); step(out, "openSdCardFileSystem", report.sd); std::fprintf(out, "},\"targets\":[");
    for (size_t i = 0; i < report.targets.size(); ++i) {
        const auto& target = report.targets[i];
        std::fprintf(out, "%s{\"path\":\"%s\",", i ? "," : "", target.path);
        step(out, "getEntryType", target.entry); std::fputc(',', out);
        step(out, "openFileRead", target.open); std::fputc(',', out);
        step(out, "getSize", target.size); std::fputc(',', out);
        step(out, "read", target.read); std::fprintf(out, ",\"observedType\":");
        if (target.entry.attempted && R_SUCCEEDED(target.entry.result)) std::fprintf(out, "%u", static_cast<unsigned>(target.type));
        else std::fprintf(out, "null");
        std::fprintf(out, ",\"observedSize\":");
        if (target.size.attempted && R_SUCCEEDED(target.size.result)) std::fprintf(out, "%lld", static_cast<long long>(target.observed_size));
        else std::fprintf(out, "null");
        std::fprintf(out, ",\"requestedBytes\":%llu,\"bytesRead\":%llu,\"readComplete\":%s,\"fileClosed\":%s,\"previewHex\":\"",
            static_cast<unsigned long long>(target.requested), static_cast<unsigned long long>(target.actual),
            boolean(target.read_complete), boolean(target.file_closed));
        for (size_t j = 0; j < target.preview_size; ++j) std::fprintf(out, "%02x", target.preview[j]);
        std::fprintf(out, "\"}");
    }
    std::fprintf(out, "],\"capture\":{\"path\":\"/moader/log-probe-capture.log\",\"eligible\":%s,\"attempted\":%s,\"bytes\":%zu,\"errno\":%d,\"wholeObservedLog\":%s},\"error\":",
        boolean(report.capture_eligible), boolean(report.capture_attempted), report.capture.size(), report.capture_error,
        boolean(report.capture_eligible && report.targets[0].observed_size <= static_cast<s64>(LogLimit)));
    quote(out, report.error);
    std::fprintf(out, ",\"limitations\":\"One ordinary SDK read per fixed path. Concurrent writer may change bytes; this is not an atomic log snapshot. Existing diagnostic outputs are never overwritten.\"}\n");
    return finish(out);
}
void draw(const Report& report, bool attempted, bool save_attempted, int save_error) {
    consoleClear();
    std::printf("MoaderLogProbe r1\n\nA: Read three fixed SD paths ONCE and save\n+: Return (report already saved after A)\n\n");
    if (!attempted) { std::printf("No diagnostic file I/O until A. No network or runtime changes.\n"); return; }
    std::printf("Pass complete: %s; SD open 0x%08x; closed: %s\n", boolean(report.complete), report.sd.result, boolean(report.sd_closed));
    for (const auto& target : report.targets) {
        if (!target.entry.attempted) { std::printf("\n%s\nSkipped: SD filesystem did not open.\n", target.path); continue; }
        std::printf("\n%s\nentry 0x%08x; open 0x%08x", target.path, target.entry.result, target.open.result);
        if (target.size.attempted) std::printf("; size 0x%08x (%lld)", target.size.result, static_cast<long long>(target.observed_size));
        if (target.read.attempted) std::printf("\nread 0x%08x; bytes %llu/%llu", target.read.result,
            static_cast<unsigned long long>(target.actual), static_cast<unsigned long long>(target.requested));
        std::printf("\n");
    }
    if (!report.error.empty()) std::printf("\nProbe error: %s\n", report.error.c_str());
    if (save_attempted && !save_error) std::printf("\nSaved %s\n", ReportPath);
    else if (save_attempted) std::printf("\nReport save error: errno %d (no retry/overwrite).\n", save_error);
    if (report.capture_attempted) std::printf("Capture save errno: %d\n", report.capture_error);
    std::printf("\nA is now inhibited. + returns normally.\n");
}
}
int main() {
    if (!consoleInit(nullptr)) return 1;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad{}; padInitializeDefault(&pad);
    Report report; bool attempted = false, save_attempted = false; int save_error = 0;
    draw(report, attempted, save_attempted, save_error);
    while (appletMainLoop()) {
        padUpdate(&pad); const u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if ((down & HidNpadButton_A) && !attempted) {
            attempted = true;
            try { run(report); }
            catch (const std::exception& e) { report.error = e.what(); }
            capture(report);
            save_attempted = true; save_error = save(report);
            draw(report, attempted, save_attempted, save_error);
        }
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr); return save_error ? 1 : 0;
}
