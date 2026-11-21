-- Moader's on-console FS installer candidate for the pinned standalone Seedsow stage.
-- It must run once, after replacement RO starts and before kaiHEN bootstrap.
-- Never retry native operations on the same boot after an error.
local P = require("s2_prim")
local M = {}
local zero = string.rep
local sub = string.sub
local fmt = string.format

local function check(ok, message)
    assert(ok, "MOADER FS: " .. message)
end

local function live_stage(message)
    local line = "MOADER FS: " .. message
    if type(_G.moader_stage) == "function" then
        _G.moader_stage(line)
    elseif _G.n2h and type(_G.n2h.log) == "function" then
        _G.n2h.log(line)
    end
end

local function aligned(value, size)
    return value > 0 and value % size == 0
end

local function empty_catalogue(ctx, rt)
    -- Match the 64-byte wire header prepared by the PC installer. The
    -- current boot's randomized FS pointer and clock make a fresh epoch;
    -- every value stays within Lua's exact-integer range.
    local low = ctx.donorRow.vptr
    local high = ctx.launch.pid * 4294967296 + math.floor(rt.now_ms())
    check(low > 0 and low < 9007199254740992 and
        high > 0 and high < 9007199254740992,
        "cannot form a current-boot catalogue epoch")
    local header = "MOADER01" .. P.u32s(1) .. P.u32s(0) ..
        P.u64s(1) .. P.u64s(low) .. P.u64s(high) ..
        P.u32s(64) .. P.u32s(64) .. P.u32s(0) .. P.u32s(0) ..
        P.u64s(0)
    check(#header == 64, "initial catalogue header is not 64 bytes")
    return header
end

-- Fixed checksums bind every SD input to the independently rebuilt payload.
-- The host staging tool additionally checks SHA-256 and reads back the SD.
local INPUTS = {
    ["profile.lua"] = {22320, 0x9c4bf153},
    ["fs-abi.bin"] = {8752, 0x11b3ea45},
    ["context.bin"] = {86016, 0x7240bd94},
    ["runtime.bin"] = {106496, 0x7e64b290},
    ["gateway.bin"] = {816, 0xd223159b},
    ["runtime.log"] = {360, 0x78177ec1},
}
local function input_is_exact(name, bytes)
    local pinned = INPUTS[name]
    if #bytes ~= pinned[1] then return false end
    local a, b = 1, 0
    for i = 1, #bytes do
        a = (a + string.byte(bytes, i)) % 65521
        b = (b + a) % 65521
    end
    return b * 65536 + a == pinned[2]
end

local function read_profile(env)
    local sd = env.open_sd_card()
    local source = env.sd_read_file(sd, "/kaihen/moader-fs/profile.lua", 0x10000).str
    check(input_is_exact("profile.lua", source), "profile input differs")
    local chunk, syntax = loadstring(source, "@/kaihen/moader-fs/profile.lua")
    check(chunk ~= nil, "profile syntax: " .. tostring(syntax))
    local profile = chunk()
    check(type(profile) == "table" and profile.schema == 1 and
        profile.captureSha256 == "0f58beb0a35ca2dadaf6300a8bbf4432974edfeac405aba4b3a3aa01af7d9271" and
        profile.fsAbiProfile == 2 and profile.contextBytes == 86016 and
        profile.runtime.bytes == 106496 and profile.gateway.bytes == 816 and
        profile.context.bytes == profile.contextBytes and
        profile.fsAbi.bytes == 8752 and #profile.fsAbi.ranges == 36 and
        #profile.hooks == 19 and profile.lazyOffset == 480 and
        profile.independentRelocationCheck and
        profile.independentContextRelocationCheck,
        "profile is not the qualified Moader FS layout")
    local function file(name, expected)
        local data = env.sd_read_file(sd, "/kaihen/moader-fs/" .. name,
            math.max(expected, 1024)).str
        check(#data == expected and input_is_exact(name, data),
            name .. " input differs")
        return data
    end
    return profile, file("fs-abi.bin", profile.fsAbi.bytes),
        file("context.bin", profile.context.bytes),
        file("runtime.bin", profile.runtime.bytes),
        file("gateway.bin", profile.gateway.bytes),
        file("runtime.log", profile.reportBytes)
end

local function expected_at(profile, abi, offset, length)
    local cursor = 0
    for _, row in ipairs(profile.fsAbi.ranges) do
        if offset >= row.offset and offset + length <= row.offset + row.size then
            return sub(abi, cursor + offset - row.offset + 1,
                cursor + offset - row.offset + length)
        end
        cursor = cursor + row.size
    end
    error("MOADER FS: missing qualified code range at FS+0x" .. fmt("%x", offset))
end

local function qualify_code(read_code, profile, abi, base)
    local cursor = 0
    local index, cave_index = 1, 1
    return function()
        local row = profile.fsAbi.ranges[index]
        if row then
            check(type(row.offset) == "number" and type(row.size) == "number" and
                row.size > 0 and cursor + row.size <= #abi,
                "invalid ABI range")
            local expected = sub(abi, cursor + 1, cursor + row.size)
            local observed = read_code(base + row.offset, row.size)
            check(observed == expected,
                fmt("live FS function differs at FS+0x%x base=0x%x got=%s",
                    row.offset, base, P.hex(sub(observed or "", 1, 16))))
            cursor = cursor + row.size
            if index == 1 or index % 6 == 0 or index == #profile.fsAbi.ranges then
                live_stage(fmt("FS code verified %d/%d", index,
                    #profile.fsAbi.ranges))
            end
            index = index + 1
            return false
        end
        check(cursor == #abi, "ABI range extent changed")
        row = profile.fsAbi.padding[cave_index]
        if row then
            local bytes = read_code(base + row.offset, row.size)
            check(bytes == zero("\0", row.size),
                "FS gateway or control cave is already occupied")
            cave_index = cave_index + 1
            if cave_index > #profile.fsAbi.padding then
                live_stage("FS code and empty caves qualified")
            end
            return false
        end
        return true
    end
end

local function relocate(data, rows, baseline, live)
    for _, row in ipairs(rows) do
        check(type(row.offset) == "number" and row.offset >= 0 and
            row.offset % 8 == 0 and row.offset + 8 <= #data,
            "invalid relocation offset")
        local value = P.u64(data, row.offset)
        for name, actual in pairs(live) do
            local coefficient = row[name] or 0
            check(coefficient == 0 or coefficient == 1,
                "invalid relocation coefficient")
            if coefficient ~= 0 then
                local original = tonumber(baseline[name])
                check(original and original < 0x8000000000,
                    "invalid relocation baseline")
                value = value + coefficient * (actual - original)
            end
        end
        check(value >= 0 and value < 0x20000000000000 and value % 1 == 0,
            "relocated pointer outside exact Lua integer range")
        data = P.put_u64(data, row.offset, value)
    end
    return data
end

function M.begin_install(env)
    local rt, fs, ctx, slots = env.rt, env.fs, env.ctx, env.slots
    local read_fs, write_fs, compare_write_fs =
        env.read_fs, env.write_fs, env.compare_write_fs
    live_stage("reading pinned SD payload")
    local profile, abi, context_bytes, runtime_bytes, gateway_bytes,
        report_template = read_profile(env)
    live_stage("pinned SD payload qualified")
    -- The upstream capture window and domain-root vtable are not the text
    -- base. The live CodeFS donor vtable is a candidate anchor: two device
    -- receipts put it at the same offset used by twelve independently
    -- qualified SD filesystem vtables. Exact code/cave bytes gate all writes.
    check(ctx.donorRow and type(ctx.donorRow.vptr) == "number",
        "same-boot FS donor vtable is missing")
    local base = ctx.donorRow.vptr - 0x2b1e20
    check(aligned(base, 0x1000) and base < 0x8000000000,
        fmt("current FS base is not qualified: donor=0x%x base=0x%x",
            ctx.donorRow.vptr, base))
    live_stage(fmt("donor proposes FS base 0x%x", base))

    live_stage("opening app-space ldr:ro")
    local ro, ro_rc
    for attempt = 1, 20 do
        ro, ro_rc = rt.get_service_sm("ldr:ro")
        if ro and ro ~= 0 then break end
        if attempt < 20 then rt.wait_ms(25) end
    end
    check(ro and ro ~= 0,
        fmt("replacement app-space ldr:ro is unavailable (rc=0x%x)", ro_rc or 0))
    live_stage("app-space ldr:ro opened")
    env.track(ro, "moader-ldr-ro")
    local process = ctx.fsProcess and ctx.fsProcess.handle
    check(process and process ~= 0 and ctx.launch and ctx.launch.pid > 1,
        "same-boot FS process or RO launch proof is missing")
    ctx.moaderUncertain = true
    _G.__MOADER_FS_UNCERTAIN = true

    local pid
    local function request(command, words, receive_size)
        local input = {}
        for _, word in ipairs(words) do input[#input + 1] = P.u64s(word) end
        local options = {pid = true, copies = {process}}
        if receive_size then
            check(receive_size > 0 and receive_size <= 0x4000,
                "RO process read exceeds the bounded scratch buffer")
            options.b = {{addr = slots.data, size = receive_size, mode = 1}}
        end
        local rep = fs.send_packet(ro, P.packet(command, table.concat(input),
            options), slots.pkt)
        check(rep.transport == 0 and rep.pid == ctx.launch.pid and
            #rep.copies == 0 and #rep.moves == 0,
            "RO process transport/identity uncertain")
        return rep
    end
    local identity = request(0x524f0010, {0, base, 0, 0})
    check(identity.result == 0 and #identity.data >= 104,
        "FS process query failed")
    pid = P.u64(identity.data, 0)
    check(pid > 1 and P.u64(identity.data, 8) == 0x0400000000000000 and
        P.u64(identity.data, 16) == 0,
        "transferred process is not the current FS")

    local function info(address)
        local rep = request(0x524f0010, {pid, address, 0, 0})
        check(rep.result == 0 and #rep.data >= 104 and
            P.u64(rep.data, 0) == pid and
            P.u64(rep.data, 8) == 0x0400000000000000,
            "FS mapping query failed")
        return {address = P.u64(rep.data, 32), size = P.u64(rep.data, 40),
            kind = P.u32(rep.data, 48), attr = P.u32(rep.data, 52),
            perm = P.u32(rep.data, 56), free = P.u64(rep.data, 88)}
    end
    local function read_code(address, length)
        check(length > 0 and length <= 0x4000,
            "RO process read extent is invalid")
        local rep = request(0x524f0016, {pid, address, length, 0}, length)
        check(rep.result == 0 and #rep.data >= 56 and
            P.u64(rep.data, 32) == 0 and P.u64(rep.data, 40) == 0 and
            P.u64(rep.data, 48) == length,
            "RO process code read or alias retirement failed at 0x" ..
            fmt("%x", address))
        return rt.rd(slots.data, length)
    end
    live_stage("checking FS executable mapping through RO")
    local code = info(base + 0x1201b0)
    check((code.kind == 3 or code.kind == 4) and code.attr == 0 and
        (code.perm == 4 or code.perm == 5) and
        base + 0x1201b0 >= code.address and
        base + 0x1201c0 <= code.address + code.size,
        "notifier mapping is not qualified executable code")
    live_stage("FS executable mapping qualified")
    live_stage("checking 36 live FS code ranges")
    local qualify_next = qualify_code(read_code, profile, abi, base)

    local function finish_install()
    env.log(fmt("qualified 36 live FS ranges at 0x%x; all caves empty", base))

    local function patch(address, before, after)
        check(#before == 16 and #after == 16 and address % 4 == 0,
            "invalid code patch extent")
        local rep = request(0x524f0014,
            {pid, address, 16, 0, sub(before, 1, 8), sub(before, 9, 16),
                sub(after, 1, 8), sub(after, 9, 16)})
        check(rep.result == 0 and #rep.data >= 80 and
            P.u64(rep.data, 48) == 1 and P.u64(rep.data, 56) == 1 and
            P.u64(rep.data, 64) == 0,
            "code compare/write/readback failed at 0x" .. fmt("%x", address))
    end
    local function permission(address, value)
        local rep = request(0x524f0015, {pid, address, 4096, value})
        check(rep.result == 0 and #rep.data >= 88 and
            P.u64(rep.data, 32) == 0 and P.u64(rep.data, 40) == 0,
            "code permission transition failed")
    end
    local function map(source, target)
        local rep = request(0x524f0012, {pid, source, target, 4096})
        check(rep.result == 0 and #rep.data >= 64 and
            P.u64(rep.data, 24) == 0 and P.u64(rep.data, 32) == 0,
            "resident RX page mapping failed")
    end

    -- Failed or unanswered native operations retain the complete Seedsow
    -- owner. The caller must not retire its cache or replay this boot.
    local cave_offset, cave_width = 0x24d100, 1024
    local cave_previous = zero("\0", cave_width)
    permission(base + 0x24d000, 5)
    local notifier_original = expected_at(profile, abi, 0x1201b0, 16)
    check(read_fs(ctx.notifier.object + 8, 8) == P.u64s(ctx.notifier.B),
        "owned notifier backend pointer changed")

    local function stage(used)
        check(#used <= cave_width, "native call exceeds qualified cave")
        local next_bytes = used .. zero("\0", cave_width - #used)
        for offset = 0, cave_width - 16, 16 do
            local before = sub(cave_previous, offset + 1, offset + 16)
            local after = sub(next_bytes, offset + 1, offset + 16)
            if before ~= after then patch(base + cave_offset + offset, before, after) end
        end
        cave_previous = next_bytes
    end

    local function invoke(operation, arguments, variant)
        arguments = arguments or {}
        local kind = variant or ("call" .. #arguments)
        local group = cave_offset == 0x24d100 and profile.bridge.initial or
            profile.bridge.control
        local template = group[kind]
        check(template and template.bytes <= cave_width,
            "missing qualified native call template: " .. kind)
        local bytes = P.unhex(template.hex)
        check(#bytes == template.bytes, "native call template extent changed")
        local values = {owner = ctx.notifier.object,
            operation = operation, result = ctx.notifier.object + 8}
        for i, argument in ipairs(arguments) do values["arg" .. (i - 1)] = argument end
        for key, offset in pairs(template.fields) do
            check(values[key] ~= nil and offset + 8 <= #bytes,
                "missing native call literal: " .. key)
            bytes = P.put_u64(bytes, offset, values[key])
        end
        stage(bytes)
        local hook_word = 0x14000000 + (cave_offset - 0x1201b0) / 4
        local hook = P.u32s(hook_word) .. P.u32s(0xd503201f):rep(3)
        check(read_fs(ctx.notifier.object + 8, 8) == P.u64s(ctx.notifier.B),
            "notifier owner changed before native call")
        patch(base + 0x1201b0, notifier_original, hook)
        local rep = fs.send_packet(ctx.notifier.handle, P.packet(0, ""), slots.pkt2)
        check(rep.transport == 0 and rep.result == 0x6dfe and
            #rep.copies == 0 and #rep.moves == 0,
            "native call result is uncertain; reboot required")
        local result = P.u64(read_fs(ctx.notifier.object + 8, 8), 0)
        compare_write_fs(ctx.notifier.object + 8, P.u64s(result), P.u64s(ctx.notifier.B))
        patch(base + 0x1201b0, hook, notifier_original)
        return result
    end

    local function call_fs(offset, arguments, variant)
        check(read_code(base + offset, 16) == expected_at(profile, abi, offset, 16),
            "native call target changed at FS+0x" .. fmt("%x", offset))
        return invoke(base + offset, arguments, variant)
    end

    local function locate(handle, kind, ref_offset, vtable_offset, slots_expected)
        local record
        env.locate_by_clone(handle, function(found)
            local raw = read_fs(found.object, ref_offset + 4)
            local vptr = P.u64(raw, 0)
            check(vptr == base + vtable_offset,
                kind .. " vtable is not at the qualified FS image offset")
            -- The method table is read-only image data, not executable code.
            -- The RO bulk reader is reserved for exact instructions and caves.
            local methods = read_fs(vptr, 80)
            check(#methods == 80 and methods == read_fs(vptr, 80),
                kind .. " method table changed between reads")
            for slot, offset in pairs(slots_expected) do
                check(P.u64(methods, slot * 8) == base + offset,
                    kind .. " method table differs")
            end
            record = {object = found.object, vptr = vptr,
                clone_ref = P.u32(raw, ref_offset), ref_offset = ref_offset}
        end)
        local baseline = P.u32(read_fs(record.object + ref_offset, 4), 0)
        check(record.clone_ref == baseline + 1 and baseline >= 1 and
            baseline <= 16, kind .. " reference ownership changed")
        record.baseline = baseline
        return record
    end

    local sd = env.open_sd_card()
    local function reset_file(path, content)
        local file = fs.ipc_open_file(sd, path, 3, 0x301)
        if not file or file == 0 then
            local created, transport = fs.ipc_create_file(sd, path, #content)
            check(transport == 0 and created == 0,
                "cannot create required runtime metadata: " .. path)
            file = fs.ipc_open_file(sd, path, 3, 0x301)
        end
        check(file and file ~= 0, "cannot open required runtime metadata: " .. path)
        local resized = fs.send_packet(file, P.packet(3, P.u64s(#content)), slots.pkt)
        check(resized.transport == 0 and resized.result == 0,
            "cannot size runtime metadata: " .. path)
        for offset = 0, #content - 1, 0x4000 do
            local part = sub(content, offset + 1, offset + 0x4000)
            rt.wr(slots.data, part)
            local written = fs.send_packet(file,
                P.packet(1, P.u32s(1) .. P.u32s(0) ..
                    P.u64s(offset) .. P.u64s(#part),
                    {a = {{addr = slots.data, size = #part, mode = 1}}}), slots.pkt)
            check(written.transport == 0 and written.result == 0,
                "cannot reset runtime metadata: " .. path)
        end
        rt.close_handle(file)
        check(env.sd_read_file(sd, path, math.max(#content, 1024)).str == content,
            "runtime metadata readback differs: " .. path)
    end
    fs.ipc_create_directory(sd, "/moader")
    fs.ipc_create_directory(sd, "/moader/runtime")
    for _, path in ipairs({"/moader", "/moader/runtime"}) do
        local entry = fs.ipc_get_entry_type(sd, path, 0x301)
        check(entry.transport == 0 and entry.result == 0 and entry.type == 0,
            "Moader runtime metadata directory is unavailable: " .. path)
    end
    reset_file("/moader/runtime/catalogue.bin", empty_catalogue(ctx, rt))
    reset_file("/moader/runtime/ack.bin", zero("\0", 64))
    reset_file("/moader/runtime/log0.bin", zero("\0", 65616))
    reset_file("/moader/runtime/log1.bin", zero("\0", 65616))
    reset_file("/moader/runtime/runtime.log", report_template)
    local committed, commit_transport = fs.ipc_commit(sd)
    check(commit_transport == 0 and committed == 0,
        "runtime metadata commit failed")
    local report_file, file_error = fs.ipc_open_file(sd,
        "/moader/runtime/runtime.log", 7, 0x301)
    check(report_file and report_file ~= 0,
        "required Moader runtime.log is absent: " .. tostring(file_error))
    check(fs.ipc_file_get_size(report_file) == profile.reportBytes,
        "required Moader runtime.log size changed")
    local log = locate(report_file, "runtime report", 0x20, 0x2b2080,
        {[0] = 0x1036f0, [1] = 0x104e10, [4] = 0x104ef0,
         [5] = 0x1050a0, [6] = 0x1053d0, [7] = 0x105410})
    local filesystem = locate(sd, "SD filesystem", 0x28, 0x2b1e20,
        {[0] = 0x4b8c0, [1] = 0x103390})
    -- A live FS global can be briefly empty during RO replacement. Accept
    -- only two consecutive identical, source-qualified pointer observations.
    local registry_global, stable, samples = nil, nil, {}
    for attempt = 1, 6 do
        local observed = P.u64(read_fs(base + 0x11e12d0, 8), 0)
        samples[#samples + 1] = fmt("0x%x", observed)
        if observed >= 0x100000000 and observed + 16 < 0x8000000000 then
            if observed == stable then
                registry_global = observed
                break
            end
            stable = observed
        else
            stable = nil
        end
        if attempt < 6 then rt.wait_ms(25) end
    end
    check(registry_global ~= nil,
        fmt("FS program registry is unavailable: base=0x%x samples=%s",
            base, table.concat(samples, ",")))
    local registry = registry_global + 16

    local context = call_fs(0x694c0, {profile.contextBytes}, "allocate")
    check(context >= 0x100000000 and context < 0x8000000000,
        "resident context allocation failed")
    local context_info = info(context)
    check(context_info.perm == 3 and context_info.attr == 0 and
        context >= context_info.address and
        context + profile.contextBytes <= context_info.address + context_info.size,
        "resident context is not backed by sufficient RW memory")
    local live = {fs = base, context = context,
        filesystem = filesystem.object, logFile = log.object,
        registry = registry}
    local prepared_context = relocate(context_bytes,
        profile.context.relocations, profile.baseline, live)
    for offset = 0, #prepared_context - 1, 0x4000 do
        local part = sub(prepared_context, offset + 1, offset + 0x4000)
        compare_write_fs(context + offset, zero("\0", #part), part)
    end
    env.log(fmt("resident context at 0x%x; report and SD source owned", context))

    local output = context + 152
    check(call_fs(0x215620, {base + 0x11ee960, output,
        profile.runtime.bytes}) == 0,
        "resident executable allocation failed")
    local source = P.u64(read_fs(output, 8), 0)
    local source_info = info(source)
    check(source >= 0x100000000 and source < 0x8000000000 and
        source_info.kind == 5 and source_info.perm == 3 and
        source_info.attr == 0 and source + profile.runtime.bytes <=
        source_info.address + source_info.size,
        "resident executable source is not Normal/RW memory")
    local runtime = source_info.free
    local target_info = info(runtime)
    check(aligned(runtime, 4096) and target_info.kind == 0 and
        runtime >= target_info.address and
        runtime + profile.runtime.bytes <= target_info.address + target_info.size,
        "no qualified free RX address for resident executable")
    live.runtime = runtime
    local prepared_runtime = relocate(runtime_bytes,
        profile.runtime.relocations, profile.baseline, live)
    for offset = 0, #prepared_runtime - 1, 0x4000 do
        local part = sub(prepared_runtime, offset + 1, offset + 0x4000)
        local address = source + offset
        compare_write_fs(address, read_fs(address, #part), part)
        for page = 0, #part - 1, 4096 do
            map(address + page, runtime + offset + page)
        end
    end
    check(invoke(0, {runtime, profile.runtime.bytes}, "publish") == 0,
        "resident executable publication failed")
    env.log(fmt("resident executable mapped %d RX pages at 0x%x",
        profile.runtime.bytes / 4096, runtime))

    local function call_runtime(name, arguments)
        local offset = profile.entries[name]
        check(type(offset) == "number" and offset >= 0 and
            offset + 16 <= profile.runtime.bytes,
            "missing resident entry: " .. name)
        local mapping = info(runtime + offset)
        check(mapping.perm == 5 and mapping.attr == 0,
            "resident entry is not RX")
        return invoke(runtime + offset, arguments or {})
    end
    check(P.u32(read_fs(filesystem.object + 0x28, 4), 0) == filesystem.baseline,
        "SD reference baseline changed")
    check(call_runtime("lfs_initialize", {}) == 0,
        "resident SD and report retention failed")
    check(P.u32(read_fs(filesystem.object + 0x28, 4), 0) == filesystem.baseline + 1 and
        P.u32(read_fs(log.object + 0x20, 4), 0) == log.baseline + 1,
        "resident source references did not retain exactly once")
    check(P.u32(read_fs(context + profile.lazyOffset + 132, 4), 0) == 0,
        "resident report initialization failed")
    rt.close_handle(report_file)
    env.log("resident retained SD/report references exactly once")

    stage("")
    cave_offset, cave_width = 0x24d800, 256
    cave_previous = zero("\0", cave_width)
    local prepared_gateway = relocate(gateway_bytes,
        profile.gateway.relocations, profile.baseline, live)
    local gateway = prepared_gateway .. zero("\0", 1024 - #prepared_gateway)
    for offset = 0, #gateway - 16, 16 do
        local after = sub(gateway, offset + 1, offset + 16)
        if after ~= zero("\0", 16) then
            patch(base + 0x24d100 + offset, zero("\0", 16), after)
        end
    end
    for _, hook in ipairs(profile.hooks) do
        local original, replacement = P.unhex(hook.originalHex),
            P.unhex(hook.replacementHex)
        check(#original == 16 and #replacement == 16 and
            read_code(base + hook.offset, 16) == original,
            "resident hook preimage differs: " .. hook.name)
        patch(base + hook.offset, original, replacement)
    end
    check(call_runtime("lfs_activate", {}) == 0,
        "resident hook activation failed")
    -- Each read_fs call validates its retained descriptor with one IStorage
    -- read, then reads the requested bytes with another. Both pass through
    -- lfs_read; each payload snapshot sees its own active call. Two
    -- consecutive snapshots therefore advance the call counter by two.
    local before, after = read_fs(context, 104), read_fs(context, 104)
    local before_calls, after_calls = P.u64(before, 16), P.u64(after, 16)
    local before_errors, after_errors = P.u64(before, 88), P.u64(after, 88)
    local before_active, after_active = P.u64(before, 96), P.u64(after, 96)
    local report_errors = P.u32(read_fs(context + profile.lazyOffset + 132, 4), 0)
    check(before_errors == 0 and after_errors == 0 and
        before_active == 1 and after_active == 1 and
        after_calls == before_calls + 2 and report_errors == 0,
        fmt("resident runtime counters or report failed: calls=%d/%d active=%d/%d errors=%d/%d report=%d",
            before_calls, after_calls, before_active, after_active,
            before_errors, after_errors, report_errors))
    stage("")
    ctx.moaderFsVerified = true
    env.log(fmt("resident FS %d hooks active; self-counted probes and report verified",
        #profile.hooks))
    return {fsBase = base, fsPid = pid, context = context,
        runtime = runtime, hooks = #profile.hooks}
    end

    local phase = "qualify"
    return {step = function()
        if phase == "qualify" then
            if qualify_next() then
                phase = "install"
                live_stage("installing Moader FS resident")
            end
            return false
        end
        check(phase == "install", "installer step repeated after completion")
        local receipt = finish_install()
        phase = "done"
        return true, receipt
    end}
end

function M.install(env)
    local runner = M.begin_install(env)
    while true do
        local done, receipt = runner.step()
        if done then return receipt end
    end
end

return M
