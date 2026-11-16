// Development smoke test. No mod hooks, game assets or runtime patches are installed.
#include <switch.h>
#include <kaihen.h>
#include <cstdio>
#include <sys/stat.h>

namespace {
struct GameStatus { u32 phase; Result result; u32 serial, mounts; u64 pid; };
struct GameTrace { u32 sequence, point; Result result; u32 value, requests, steps; };
Service hen{};
Result open_result = 0;
void report(FILE* out) {
    std::fprintf(out, "Moader Diagnostics 0.1.1\nNative Switch 2 build with ounce-devkit 20261114\n\n");
    std::fprintf(out, "HEN session: 0x%08x\n", open_result);
    if (R_SUCCEEDED(open_result)) {
        HenStatus status{};
        const Result rc = henGetStatus(&hen, &status);
        std::fprintf(out, "HEN status query: 0x%08x\n", rc);
        if (R_SUCCEEDED(rc)) std::fprintf(out, "HEN PID: %llu, protocol: %u, startup: 0x%08x\n",
            static_cast<unsigned long long>(status.process_id), status.protocol_version, status.initialization_result);
        GameStatus game{};
        const Result game_rc = serviceDispatchOut(&hen, 8, game);
        std::fprintf(out, "Game status query: 0x%08x\n", game_rc);
        if (R_SUCCEEDED(game_rc)) std::fprintf(out, "Game phase: %u, result: 0x%08x, mounts: %u, cycles: %u\n",
            game.phase, game.result, game.mounts, game.serial);
        GameTrace trace{};
        const Result trace_rc = serviceDispatchOut(&hen, 0x4d4f0001, trace);
        std::fprintf(out, "Moader trace query: 0x%08x\n", trace_rc);
        if (R_SUCCEEDED(trace_rc)) std::fprintf(out, "Trace point: %u, result: 0x%08x, value: 0x%08x\n", trace.point, trace.result, trace.value);
    }
    std::fprintf(out, "\nThis build checks HEN, input, display and SD storage.\nMod application and the web service are still in development.\n");
}
}
int main() {
    if (!consoleInit(nullptr)) return 1;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    Service ro{};
    open_result = kaihenGetRoSession(&ro);
    if (R_SUCCEEDED(open_result)) open_result = kaihenOpenHomebrewSession(&ro, &hen);
    bool redraw = true;
    const char* saved = "";
    while (appletMainLoop()) {
        padUpdate(&pad); const u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (down & HidNpadButton_Y) redraw = true;
        if (down & HidNpadButton_A) {
            mkdir("sdmc:/moader", 0777);
            FILE* file = std::fopen("sdmc:/moader/diagnostics.txt", "w");
            if (file) { report(file); saved = std::fclose(file) ? "SD write failed" : "Saved sdmc:/moader/diagnostics.txt"; }
            else saved = "Cannot open SD diagnostic report";
            redraw = true;
        }
        if (redraw) {
            consoleClear(); report(stdout);
            std::printf("\nA: Save report    Y: Refresh    +: Return to hbmenu\n%s\n", saved);
            redraw = false;
        }
        consoleUpdate(nullptr);
    }
    if (serviceIsActive(&hen)) serviceClose(&hen);
    consoleExit(nullptr); return 0;
}
