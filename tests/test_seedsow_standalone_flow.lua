-- Host-only protocol/memory simulation of the exact-capture installer path.
-- Native calls are modeled as receipts; this is not a device-success claim.
local capture_path, payload_dir, upstream_lua, root = ...
assert(capture_path and payload_dir and upstream_lua and root)
package.path = upstream_lua .. "/?.lua;" .. package.path
loadstring = loadstring or load
local P = require("s2_prim")
local function file(path)
    local h = assert(io.open(path, "rb"))
    local bytes = assert(h:read("*a"))
    h:close()
    return bytes
end
local capture = file(capture_path)
local profile = assert(loadfile(payload_dir .. "/profile.lua"))()
local installer = assert(loadfile(root .. "/standalone/s2_moader.lua"))()
local base, context, source, runtime = 0x4000000000, 0x6000000000,
    0x6200000000, 0x7000000000
local filesystem, report, registry = 0x6100001000, 0x6100002000,
    0x6100003000
local notifier, backend = 0x6100004000, 0x6100005000
local image, files, handles = {}, {
    ["/moader/runtime/catalogue.bin"] = string.rep("X", 64),
    ["/moader/runtime/ack.bin"] = string.rep("X", 64),
}, {}
local next_handle, data_slot, mapped_pages, invocations, patches = 100, "", 0, 0, 0
local function read(address, length)
    local out = {}
    for i = 0, length - 1 do
        local at = address + i
        local byte = image[at]
        if not byte and at >= base and at < base + #capture then
            byte = capture:byte(at - base + 1)
        end
        out[i + 1] = string.char(byte or 0)
    end
    return table.concat(out)
end
local function write(address, bytes)
    for i = 1, #bytes do image[address + i - 1] = bytes:byte(i) end
end
local function q(address, value) write(address, P.u64s(value)) end
local function d(address, value) write(address, P.u32s(value)) end
q(base + 0x11e12d0, registry - 16)
q(notifier + 8, backend)
q(filesystem, 0x6100010000)
q(report, 0x6100011000)
d(filesystem + 0x28, 2)
d(report + 0x20, 1)
for slot, offset in pairs({[0] = 0x4b8c0, [1] = 0x103390}) do
    q(0x6100010000 + slot * 8, base + offset)
end
for slot, offset in pairs({[0] = 0x1036f0, [1] = 0x104e10,
    [4] = 0x104ef0, [5] = 0x1050a0, [6] = 0x1053d0,
    [7] = 0x105410}) do
    q(0x6100011000 + slot * 8, base + offset)
end
local function reply(length, values)
    local bytes = string.rep("\0", length)
    for offset, value in pairs(values) do bytes = P.put_u64(bytes, offset, value) end
    return {transport = 0, result = 0, pid = 218, data = bytes,
        copies = {}, moves = {}}
end
local function mapping(address)
    local start, size, kind, permission
    if address >= base and address < base + #capture then
        start, size, kind, permission = base, #capture, 3, 5
    elseif address >= context and address < context + 0x20000 then
        start, size, kind, permission = context, 0x20000, 5, 3
    elseif address >= source and address < source + 0x20000 then
        start, size, kind, permission = source, 0x20000, 5, 3
    elseif address >= runtime and address < runtime + 0x200000 then
        start, size = runtime, 0x200000
        kind, permission = mapped_pages > 0 and 3 or 0, mapped_pages > 0 and 5 or 0
    else
        start, size, kind, permission = address, 0x1000, 5, 3
    end
    return reply(104, {[0] = 941, [8] = 0x0400000000000000,
        [32] = start, [40] = size, [48] = kind, [56] = permission,
        [88] = runtime})
end
local fs = {}
function fs.send_packet(handle, packet)
    local start = P.domain_data_start(packet)
    local command = P.u32(packet, start + 8)
    local input = start + 16
    if handle == 0x44 then
        local pid = P.u64(packet, input)
        assert(pid == 0 or pid == 941)
        local address = P.u64(packet, input + 8)
        if command == 0x524f0010 then return mapping(address) end
        if command == 0x524f0016 then
            local length = P.u64(packet, input + 16)
            assert(length > 0 and length <= 0x4000 and
                math.floor(P.u32(packet, 0) / 0x1000000) % 16 == 1,
                "bounded RO process read lost its receive descriptor")
            data_slot = read(address, length)
            return reply(56, {[32] = 0, [40] = 0, [48] = length})
        end
        if command == 0x524f0014 then
            local before = packet:sub(input + 33, input + 48)
            local after = packet:sub(input + 49, input + 64)
            assert(read(address, 16) == before, "code patch preimage changed")
            write(address, after)
            patches = patches + 1
            return reply(80, {[48] = 1, [56] = 1})
        end
        if command == 0x524f0015 then return reply(88, {}) end
        if command == 0x524f0012 then
            mapped_pages = mapped_pages + 1
            return reply(64, {})
        end
        error("unexpected RO command " .. command)
    end
    if handle == 0x46 then
        assert(command == 0)
        invocations = invocations + 1
        local result = invocations == 1 and context or 0
        if invocations == 2 then q(context + 152, source) end
        if invocations == 4 then
            d(filesystem + 0x28, 3)
            d(report + 0x20, 2)
        end
        q(notifier + 8, result)
        return {transport = 0, result = 0x6dfe, data = "",
            copies = {}, moves = {}}
    end
    local path = assert(handles[handle], "unknown file handle")
    if command == 3 then
        local size = P.u64(packet, input)
        files[path] = files[path]:sub(1, size) ..
            string.rep("\0", math.max(0, size - #files[path]))
    elseif command == 1 then
        local offset, size = P.u64(packet, input + 8),
            P.u64(packet, input + 16)
        assert(#data_slot == size)
        local original = files[path]
        files[path] = original:sub(1, offset) .. data_slot ..
            original:sub(offset + size + 1)
    else error("unexpected IFile command " .. command) end
    return {transport = 0, result = 0, data = "",
        copies = {}, moves = {}}
end
function fs.ipc_open_file(_, path)
    if files[path] == nil then return 0, 0x202 end
    next_handle = next_handle + 1
    handles[next_handle] = path
    return next_handle, 0
end
function fs.ipc_create_file(_, path, size)
    assert(files[path] == nil)
    files[path] = string.rep("\0", size)
    return 0, 0
end
function fs.ipc_create_directory() return 0, 0 end
function fs.ipc_get_entry_type() return {transport = 0, result = 0, type = 0} end
function fs.ipc_commit() return 0, 0 end
function fs.ipc_file_get_size(handle) return #files[assert(handles[handle])] end
local ctx = {layout = {captureStart = base + 0x10000000,
    rootVptr = base + 0x2aabcd0},
    donorRow = {vptr = base + 0x2b1e20},
    notifier = {object = notifier, B = backend, handle = 0x46},
    fsProcess = {handle = 0x45}, launch = {pid = 218}}
local env = {
    rt = {get_service_sm = function(name)
        assert(name == "ro:dmnt")
        return 0x44
    end, wr = function(_, bytes) data_slot = bytes end,
    rd = function(_, length) return data_slot:sub(1, length) end,
    close_handle = function(handle) handles[handle] = nil end},
    fs = fs, ctx = ctx, slots = {pkt = 1, pkt2 = 2, data = 3},
    open_sd_card = function() return 0x47 end,
    sd_read_file = function(_, path)
        if path:sub(1, 18) == "/kaihen/moader-fs/" then
            return {str = file(payload_dir .. "/" .. path:sub(19))}
        end
        return {str = assert(files[path], "missing readback " .. path)}
    end,
    read_fs = read, write_fs = write,
    compare_write_fs = function(address, before, after)
        assert(read(address, #before) == before,
            "data compare/write preimage changed")
        write(address, after)
        assert(read(address, #after) == after)
    end,
    locate_by_clone = function(handle, inspect)
        local object, offset, baseline
        if handle == 0x47 then
            object, offset, baseline = filesystem, 0x28, 2
        else
            assert(handles[handle] == "/moader/runtime/runtime.log")
            object, offset, baseline = report, 0x20, 1
        end
        d(object + offset, baseline + 1)
        inspect({object = object})
        d(object + offset, baseline)
    end,
    track = function(handle) assert(handle == 0x44) end,
    log = function() end,
}
local receipt = installer.install(env)
assert(receipt.fsBase == base and receipt.fsPid == 941 and
       receipt.context == context and receipt.runtime == runtime and
       receipt.hooks == 19 and ctx.moaderFsVerified)
assert(mapped_pages == 26 and invocations == 5 and patches >= 19,
    string.format("flow count mismatch: maps=%d calls=%d patches=%d",
        mapped_pages, invocations, patches))
assert(read(notifier + 8, 8) == P.u64s(backend))
assert(read(base + 0x1201b0, 16) == capture:sub(0x1201b1, 0x1201c0))
for _, row in ipairs(profile.hooks) do
    assert(read(base + row.offset, 16) == P.unhex(row.replacementHex))
end
assert(files["/moader/runtime/catalogue.bin"] == string.rep("\0", 64) and
       files["/moader/runtime/ack.bin"] == string.rep("\0", 64))
print("Standalone Moader FS install flow simulated from exact capture; device proof pending.")
