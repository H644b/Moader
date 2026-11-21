-- Host check that Stage C yields to the launcher between broad status lines.
local overlay, upstream = ...
assert(overlay and upstream)
package.path = upstream .. "/?.lua;" .. package.path
local runner_module = assert(loadfile(overlay .. "/s2_kh_ro.lua"))()
local screen, calls = {}, {}
_G.n2h = {log = function(line) screen[#screen + 1] = line end}
local function mark(name)
    return function() calls[#calls + 1] = name end
end
local runner = runner_module.begin_ro_and_bootstrap({}, {
    rt = {}, fs = {}, ctx = {apiWorkers = {}, handles = {}}, slots = {}, ep = {},
    kh_log = function() end, arena_alloc = function() return 0x1000 end,
    track = function() end, open_sd_card = function() end,
    sd_read_file = function() end, read_fs = function() end,
    write_fs_unverified = function() end, compare_write_fs = function() end,
    export_handle = function() end,
    verify_and_relocate_sd = mark("sd"), arm_endpoint = mark("endpoint"),
    derive_layout_and_loader = mark("layout"), open_notifier = mark("notifier"),
    open_pm_sessions = mark("pm"), copy_loader_and_open_codefs = mark("codefs"),
})
assert(not runner.step() and #calls == 0)
assert(screen[1] == "MOADER KH: checking SD package")
assert(not runner.step() and calls[1] == "sd")
assert(not runner.step() and #calls == 1)
assert(screen[2] == "MOADER KH: arming FS endpoint")
assert(not runner.step() and calls[2] == "endpoint")

-- Exercise the real generated bootstrap poll with a published operation record.
-- The PC route deliberately never joins this worker after Complete.
local file = assert(io.open(overlay .. "/s2_kh_ro.lua", "rb"))
local source = assert(file:read("*a"))
file:close()
local first = assert(source:find("    local bootstrap_operations = {", 1, true))
local last = assert(source:find("    local stages = {", first, true))
local poll_source = source:sub(first, last - 1)
local poll_chunk = assert((loadstring or load)("local ctx,rt,kh_log,live_stage,P,str_format = ...\n" ..
    poll_source .. "\nreturn poll_bootstrap_nro"))
local P = require("s2_prim")
local clock, operation, result = 0, 2, 0
local ctx = {bootstrapArgsAddress = 0x2000, bootstrapDeadline = 120000,
    moaderUncertain = true}
local rt = {
    rd = function(address, bytes)
        assert(address == 0x2000 + 408 and bytes == 24)
        return P.u32s(operation) .. P.u32s(result) .. P.u64s(42) ..
            P.u32s(0) .. P.u32s(0)
    end,
    now_ms = function() return clock end,
    wait_ms = function(ms) clock = clock + ms end,
    N = {call_deep = function() error("bootstrap worker must not be joined") end},
}
local status = {}
local poll = poll_chunk(ctx, rt, function() end,
    function(line) status[#status + 1] = line end, P, string.format)
_G.__KH = {stages = {}}
_G.__MOADER_FS_UNCERTAIN = true
assert(poll() == false and clock == 0)
assert(status[1] == "bootstrap park Loader (2)")
assert(poll() == false and #status == 1)
clock = 1000
operation = 23
assert(poll() == false)
assert(status[2] == "bootstrap wait PM worker (23) mounts=21 exchange=no gate=0x0 channel=0x0")
clock = 2000
operation = 7
assert(poll() == false)
assert(status[3] == "bootstrap PM worker settled (7)")
clock = 3000
operation = 10
assert(poll() == nil and clock == 3500)
assert(ctx.henStartup.operation == 10 and ctx.henStartup.pid == 42)
assert(ctx.moaderUncertain == false and _G.__MOADER_FS_UNCERTAIN == false)
assert(_G.__KH.stages.complete == true)

clock, operation, result = 120001, 9, 0
ctx.moaderUncertain = true
poll = poll_chunk(ctx, rt, function() end, function() end, P, string.format)
local ok, failure = pcall(poll)
assert(not ok and failure:find("bootstrap timed out at operation 9", 1, true))
assert(ctx.moaderUncertain == true)
print("Standalone Stage C redraw boundaries verified on host.")
