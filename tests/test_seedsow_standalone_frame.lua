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
print("Standalone Stage C redraw boundaries verified on host.")
