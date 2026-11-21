-- Offline guard test using the exact qualified capture. No native calls.
local capture_path, payload_dir, upstream_lua, root = ...
assert(capture_path and payload_dir and upstream_lua and root,
    "usage: lua test_seedsow_standalone_lua.lua CAPTURE PAYLOAD UPSTREAM_LUA ROOT")
package.path = upstream_lua .. "/?.lua;" .. package.path
loadstring = loadstring or load

local function file(path)
    local handle = assert(io.open(path, "rb"))
    local data = assert(handle:read("*a"))
    handle:close()
    return data
end

local capture = file(capture_path)
local P = require("s2_prim")
local installer = assert(loadfile(root .. "/standalone/s2_moader.lua"))()
local base = 0x4000000000
local function run(alter_at, ro_active, corrupt_profile, fail_first_query)
    local writes, services, reads, sends = 0, 0, 0, 0
    local ctx = {layout = {captureStart = base + 0x10000000,
        rootVptr = base + 0x2aabcd0},
        donorRow = {vptr = base + 0x2b1e20},
        fsProcess = {handle = 0x45}, launch = {pid = 218}}
    _G.__MOADER_FS_UNCERTAIN = false
    local env = {
        rt = {get_service_sm = function()
            services = services + 1
            return ro_active and 0x44 or 0
        end},
        fs = {send_packet = function(_, packet)
            sends = sends + 1
            local at = P.domain_data_start(packet)
            local command = P.u32(packet, at + 8)
            if command == 0x524f0010 and not fail_first_query then
                local data = string.rep("\0", 104)
                for offset, value in pairs({[0] = 941,
                    [8] = 0x0400000000000000, [32] = base,
                    [40] = #capture, [48] = 3, [56] = 5}) do
                    data = P.put_u64(data, offset, value)
                end
                return {transport = 0, result = 0, pid = 218,
                    data = data, copies = {}, moves = {}}
            end
            return {transport = 0xea01, result = 0, copies = {}, moves = {}}
        end},
        ctx = ctx,
        slots = {pkt = 0},
        track = function() end,
        open_sd_card = function() return 1 end,
        sd_read_file = function(_, path)
            local name = assert(path:match("^/kaihen/moader%-fs/([%w%-.]+)$"))
            local bytes = file(payload_dir .. "/" .. name)
            if corrupt_profile and name == "profile.lua" then
                bytes = bytes:sub(1, #bytes - 1) .. " "
            end
            return {str = bytes}
        end,
        read_fs = function(address, length)
            reads = reads + 1
            local offset = address - base
            assert(offset >= 0 and offset + length <= #capture)
            local bytes = capture:sub(offset + 1, offset + length)
            if alter_at and offset <= alter_at and alter_at < offset + length then
                local index = alter_at - offset + 1
                bytes = bytes:sub(1, index - 1) ..
                    string.char((bytes:byte(index) + 1) % 256) .. bytes:sub(index + 1)
            end
            return bytes
        end,
        write_fs = function() writes = writes + 1 end,
        compare_write_fs = function() writes = writes + 1 end,
        log = function() end,
    }
    local ok, error_text = pcall(installer.install, env)
    assert(not ok and writes == 0 and
        (reads >= 1 or corrupt_profile or not ro_active or fail_first_query),
        "preflight failed to stop before a native write")
    return tostring(error_text), services, reads, sends, ctx
end

local normal, normal_services, normal_reads, normal_sends = run(nil, true)
assert(normal:find("RO process transport/identity uncertain", 1, true) and
       normal_services == 1 and normal_reads >= 38 and normal_sends == 3,
       "exact live FS did not pass all code and cave checks")
local changed, changed_services, _, changed_sends = run(0x4b8c0, true)
assert(changed:find("live FS function differs", 1, true) and
       changed_services == 1 and changed_sends == 2,
       "changed FS instruction reached a native mutation")
local occupied, occupied_services = run(0x24d100, true)
assert(occupied:find("already occupied", 1, true) and
       occupied_services == 1, "occupied code cave reached a native mutation")
local unavailable, unavailable_services, unavailable_reads = run(nil, false)
assert(unavailable:find("replacement ro:dmnt is unavailable", 1, true) and
       unavailable_services == 1 and unavailable_reads == 0,
       "missing RO reached a memory read")
local uncertain, active_services, _, sends, ctx = run(nil, true, false, true)
assert(uncertain:find("RO process transport/identity uncertain", 1, true) and
       active_services == 1 and sends == 1 and
       ctx.moaderUncertain and _G.__MOADER_FS_UNCERTAIN,
       "failed RO transport did not retain the owner before any replay")
local corrupt, corrupt_services, corrupt_reads = run(nil, false, true)
assert(corrupt:find("profile input differs", 1, true) and
       corrupt_services == 0 and corrupt_reads == 0,
       "changed SD profile reached native inspection")
print("Standalone Moader Lua source guards passed without native writes.")
