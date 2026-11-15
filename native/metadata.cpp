// Bounded normal-NRO metadata diagnostic. No network, hooks or privileged setup.
#include <switch.h>
#include "metadata_names.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace {
constexpr const char* ReportPath = "sdmc:/moader/metadata-diagnostics-small-icon.json";
constexpr unsigned DiagnosticRevision = 2, ControlReadLimit = 2;
constexpr size_t RecordLimit = 32;
constexpr u64 TargetApplicationId = 0x0100f43008c44000ull;
constexpr u8 IconFlag = 1;
struct Step { bool attempted = false; Result result = 0; };
struct Selection { u64 id; const char* reason; };
struct Probe {
    u64 id = 0, actual = 0;
    u32 auxiliary = 0;
    u8 acd_index = 0;
    Result result = 0;
    bool size_valid = false;
    u8 format = 0, nacp_acd_index = 0, apparent_platform = 0;
    moader_metadata::NameResult parsed;
};
struct Report {
    u32 hos = hosversionGet();
    Step ns, getter, records, language_initialize, language_read, language_convert;
    bool count_valid = false, complete = false;
    s32 raw_count = 0;
    int preferred_language_index = 0;
    unsigned control_reads = 0;
    std::vector<NsApplicationRecord> actual_records;
    std::vector<Selection> selected;
    std::vector<Probe> probes;
    std::string branch = "not_started", error;
};
std::string hex(u64 value, unsigned width = 16) {
    char text[24]; std::snprintf(text, sizeof(text), "%0*llx", static_cast<int>(width), static_cast<unsigned long long>(value)); return text;
}
std::string quote(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32 || c == 127) { char escape[7]; std::snprintf(escape, sizeof(escape), "\\u%04x", c); out += escape; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
const char* boolean(bool value) { return value ? "true" : "false"; }
std::string result_json(Result result) {
    return "{\"code\":\"0x" + hex(result, 8) + "\",\"module\":" + std::to_string(R_MODULE(result)) +
        ",\"description\":" + std::to_string(R_DESCRIPTION(result)) + '}';
}
std::string step_json(const Step& step) { return step.attempted ? result_json(step.result) : "null"; }
void progress(const char* text) { std::printf("%s\n", text); consoleUpdate(nullptr); }

// Same language-index order as libounce's nacpGetLanguageEntry. Unknown/new
// system-language enumerants prefer English, then the parser's valid fallback.
void preferred_language(Report& report) {
    report.language_initialize.attempted = true;
    report.language_initialize.result = setInitialize();
    if (R_FAILED(report.language_initialize.result)) return;
    struct Guard { ~Guard() { setExit(); } } guard;
    u64 code = 0;
    report.language_read.attempted = true;
    report.language_read.result = setGetSystemLanguage(&code);
    if (R_FAILED(report.language_read.result)) return;
    SetLanguage language = SetLanguage_ENUS;
    report.language_convert.attempted = true;
    report.language_convert.result = setMakeLanguage(code, &language);
    if (R_FAILED(report.language_convert.result)) return;
    constexpr std::array<unsigned, 18> entries{{2, 0, 3, 4, 7, 6, 14, 12, 8, 10, 11, 13, 1, 9, 5, 14, 13, 15}};
    if (static_cast<unsigned>(language) < entries.size()) report.preferred_language_index = entries[language];
}
void select_records(Report& report) {
    // Only the baseline's exact ZA ApplicationId, freshly returned by NS.
    // No suffix recognition, synthesized alias, or alternate title selection.
    for (const auto& record : report.actual_records) if (record.application_id == TargetApplicationId) {
        report.selected.push_back({record.application_id, "Exact ZA ApplicationId present in fresh NS record page"});
        break;
    }
}
void run(Report& report) {
    progress("Initializing the normal NS service...");
    report.ns.attempted = true;
    report.ns.result = nsInitialize();
    if (R_FAILED(report.ns.result)) { report.branch = "skipped_ns_initialize_failed"; report.complete = true; return; }
    struct Guard { ~Guard() { nsExit(); } } guard;
    Service getter{};
    report.getter.attempted = true;
    report.getter.result = nsGetReadOnlyApplicationControlDataInterface(&getter);
    if (serviceIsActive(&getter)) serviceClose(&getter);
    std::printf("Read-only control interface: 0x%08x\n", report.getter.result);
    progress("Reading one page of up to 32 NS application records...");
    std::array<NsApplicationRecord, RecordLimit> records{};
    report.records.attempted = true;
    report.records.result = nsListApplicationRecord(records.data(), static_cast<s32>(records.size()), 0, &report.raw_count);
    report.count_valid = R_SUCCEEDED(report.records.result) && report.raw_count >= 0 && report.raw_count <= static_cast<s32>(RecordLimit);
    if (report.count_valid) report.actual_records.assign(records.begin(), records.begin() + report.raw_count);
    std::printf("Application records: 0x%08x, count %d, valid %s\n", report.records.result, report.raw_count, boolean(report.count_valid));
    if (R_FAILED(report.getter.result)) { report.branch = "skipped_getter_failed"; report.complete = true; return; }
    if (!report.count_valid) { report.branch = "skipped_records_failed_or_invalid_count"; report.complete = true; return; }
    select_records(report);
    if (report.selected.empty()) { report.branch = "skipped_exact_za_record_not_in_page"; report.complete = true; return; }
    preferred_language(report);
    for (const auto& selected : report.selected) for (u8 index : {u8(0), u8(1)}) {
        std::printf("ControlData2: %s flag %u ACD index %u (%u/%u)...\n", hex(selected.id).c_str(), IconFlag,
            index, report.control_reads + 1, ControlReadLimit); consoleUpdate(nullptr);
        auto control = std::make_unique<NsApplicationControlData>();
        Probe probe; probe.id = selected.id; probe.acd_index = index;
        ++report.control_reads;
        probe.result = nsGetApplicationControlData2(NsApplicationControlSource_Storage, selected.id,
            control.get(), sizeof(*control), IconFlag, index, &probe.actual, &probe.auxiliary);
        if (R_SUCCEEDED(probe.result)) {
            probe.size_valid = probe.actual >= sizeof(NacpStruct) && probe.actual <= sizeof(*control);
            if (probe.size_valid) {
                probe.format = control->nacp.titles_data_format;
                probe.nacp_acd_index = control->nacp.acd_index;
                probe.apparent_platform = control->nacp.apparent_platform;
                probe.parsed = moader_metadata::parse_name(control->nacp, static_cast<size_t>(probe.actual), report.preferred_language_index);
            } else probe.parsed.error = "ControlData2 returned an invalid output size";
        } else probe.parsed.error = "Name parsing skipped: control-data request failed";
        std::printf("  rc 0x%08x, bytes %llu, name %s\n", probe.result, static_cast<unsigned long long>(probe.actual),
            probe.parsed.valid ? probe.parsed.name.c_str() : "unavailable");
        report.probes.push_back(std::move(probe));
    }
    report.branch = "finished_bounded_control_reads"; report.complete = true;
}
std::string serialize(const Report& report) {
    std::string out = "{\n\"schema\":1,\"diagnostic\":\"moader-metadata\",\"revision\":" + std::to_string(DiagnosticRevision) +
        ",\"variant\":\"small-icon\",\"reportPath\":" + quote(ReportPath) + ",\"complete\":" + boolean(report.complete) +
        ",\"networkServer\":false,\"gameLaunches\":0,\"privilegedSetup\":false,\n\"hosVersion\":{\"raw\":\"0x" + hex(report.hos, 8) +
        "\",\"major\":" + std::to_string(HOSVER_MAJOR(report.hos)) + ",\"minor\":" + std::to_string(HOSVER_MINOR(report.hos)) +
        ",\"micro\":" + std::to_string(HOSVER_MICRO(report.hos)) + ",\"source\":\"SDK runtime value; may be startup minimum fallback\"},\n";
    out += "\"sizes\":{\"applicationRecord\":" + std::to_string(sizeof(NsApplicationRecord)) + ",\"controlData\":" + std::to_string(sizeof(NsApplicationControlData)) +
        ",\"nacp\":" + std::to_string(sizeof(NacpStruct)) + ",\"languageEntry\":" + std::to_string(sizeof(NacpLanguageEntry)) + "},\n";
    out += "\"nsInitialize\":" + step_json(report.ns) + ",\"readOnlyControlInterface\":" + step_json(report.getter) +
        ",\"applicationRecordsResult\":" + step_json(report.records) + ",\"recordOffset\":0,\"recordLimit\":32,\"rawRecordCount\":" + std::to_string(report.raw_count) +
        ",\"recordCountValid\":" + boolean(report.count_valid) + ",\"records\":[";
    bool comma = false;
    for (const auto& record : report.actual_records) {
        if (comma) out += ',';
        comma = true;
        out += "{\"applicationId\":" + quote(hex(record.application_id)) + ",\"attributes\":" + std::to_string(record.attributes) + ",\"lastEvent\":" + std::to_string(record.last_event) + '}';
    }
    out += "],\n\"language\":{\"initialize\":" + step_json(report.language_initialize) + ",\"read\":" + step_json(report.language_read) +
        ",\"convert\":" + step_json(report.language_convert) + ",\"preferredEntryIndex\":" + std::to_string(report.preferred_language_index) + "},\n\"selected\":[";
    comma = false;
    for (const auto& selected : report.selected) {
        if (comma) out += ',';
        comma = true; out += "{\"applicationId\":" + quote(hex(selected.id)) + ",\"reason\":" + quote(selected.reason) + '}';
    }
    out += "],\n\"controlReadLimit\":" + std::to_string(ControlReadLimit) + ",\"controlReads\":" + std::to_string(report.control_reads) + ",\"controlBranch\":" + quote(report.branch) + ",\"probes\":[";
    comma = false;
    for (const auto& probe : report.probes) {
        if (comma) out += ',';
        comma = true;
        const char* stage = R_FAILED(probe.result) ? "control_data2" : !probe.size_valid ? "control_size" : "name_parse";
        out += "{\"stage\":" + quote(stage) + ",\"applicationId\":" + quote(hex(probe.id)) + ",\"source\":1,\"flag1\":" + std::to_string(IconFlag) + ",\"acdIndex\":" + std::to_string(probe.acd_index) +
            ",\"result\":" + result_json(probe.result) + ",\"actualSize\":" + std::to_string(probe.actual) + ",\"auxiliary\":" + std::to_string(probe.auxiliary) +
            ",\"sizeValid\":" + boolean(probe.size_valid) + ",\"nacp\":";
        if (probe.size_valid) out += "{\"titlesDataFormat\":" + std::to_string(probe.format) + ",\"acdIndex\":" + std::to_string(probe.nacp_acd_index) +
            ",\"apparentPlatform\":" + std::to_string(probe.apparent_platform) + ",\"compressedBytes\":" + std::to_string(probe.parsed.compressed_bytes) + '}';
        else out += "null";
        out += ",\"name\":" + (probe.parsed.valid ? quote(probe.parsed.name) : "null") + ",\"nameValid\":" + boolean(probe.parsed.valid) +
            ",\"languageCount\":" + std::to_string(probe.parsed.language_count) + ",\"languageIndex\":" + (probe.parsed.valid ? std::to_string(probe.parsed.language_index) : "null") +
            ",\"parseError\":" + quote(probe.parsed.error) + '}';
    }
    return out + "],\n\"error\":" + quote(report.error) + "\n}\n";
}
bool save(const std::string& json) {
    if (json.size() > 32768) return false;
    if (mkdir("sdmc:/moader", 0777) && errno != EEXIST) return false;
    struct stat entry{};
    if (stat("sdmc:/moader", &entry) || !S_ISDIR(entry.st_mode)) return false;
    FILE* file = std::fopen(ReportPath, "wb");
    if (!file) return false;
    const bool wrote = std::fwrite(json.data(), 1, json.size(), file) == json.size();
    const int flushed = std::fflush(file), closed = std::fclose(file);
    return wrote && !flushed && !closed;
}
}
int main() {
    if (!consoleInit(nullptr)) return 1;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    std::printf("Moader Metadata Diagnostic r%u: small icon\n\nA: Run once and save report    +: Return to hbmenu\n\n", DiagnosticRevision);
    std::printf("One NS record page (32 maximum). Exact ZA ID %s only.\nFlag %u; ACD indices 0/1; %u ControlData2 reads maximum.\nNo network or game launch.\nReport: %s\n\n",
        hex(TargetApplicationId).c_str(), IconFlag, ControlReadLimit, ReportPath);
    bool attempted = false;
    while (appletMainLoop()) {
        padUpdate(&pad); const u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if ((down & HidNpadButton_A) && !attempted) {
            attempted = true;
            Report report;
            try { run(report); }
            catch (const std::exception& error) { report.error = error.what(); report.branch = "stopped_exception"; }
            try {
                if (save(serialize(report))) std::printf("\nSaved %s\n", ReportPath);
                else std::printf("\nSD report save failed. No retry in this run.\n");
            } catch (const std::exception& error) { std::printf("\nReport failure: %s\n", error.what()); }
            std::printf("NS init 0x%08x\n", report.ns.result);
            if (report.getter.attempted) std::printf("Getter 0x%08x\n", report.getter.result);
            if (report.records.attempted) std::printf("Records 0x%08x\n", report.records.result);
            std::printf("Control reads: %u/%u. Branch: %s\n\n+: Return to hbmenu\n", report.control_reads, ControlReadLimit, report.branch.c_str());
        }
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr); return 0;
}
