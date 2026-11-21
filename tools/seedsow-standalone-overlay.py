#!/usr/bin/env python3
"""Build isolated Moader A and upstream Seedsow B save entries."""
from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / 'build/seedsow-standalone-upstream'

FRAME_RO_RUNNER = '''    -- Each completed native stage returns to the launcher for a screen redraw.
    local stages = {
        {"checking SD package", verify_and_relocate_sd},
        {"arming FS endpoint", arm_endpoint},
        {"checking FS and Loader layout", derive_layout_and_loader},
        {"opening FS notifier", open_notifier},
        {"opening PM sessions", open_pm_sessions},
        {"opening CodeFS donor", copy_loader_and_open_codefs},
        {"preparing donor workers", prepare_workers_and_donor},
        {"binding server sessions", bind_sessions_and_gate_event},
        {"starting replacement RO", launch_and_activate_ro},
        {"opening Moader FS adapter", start_bootstrap_nro},
        {"checking and installing Moader FS", function()
            local done, receipt = ctx.moaderFsRunner.step()
            if not done then return false end
            ctx.moaderFs = receipt
        end},
        {"opening kaiHEN Loader", finish_bootstrap_nro},
        {"mapping kaiHEN bootstrap", launch_bootstrap_nro},
        {"starting kaiHEN worker", start_bootstrap_worker},
        {"waiting for kaiHEN startup", poll_bootstrap_nro},
    }
    local index, announced, failed, completed = 1, false, nil, false
    local function retire()
        pcall(function()
            if not ctx.moaderUncertain and ctx.candidateHandle and ctx.candidateHandle ~= 0 then
                fs.ipc_storage_invalidate(ctx.candidateHandle, 0, 0x8000)
                local rbuf = arena_alloc(0x1000)
                rt.wr(rbuf, str_rep(str_char(0x42), 16))
                fs.ipc_storage_write(ctx.candidateHandle, 0, rbuf, 16)
                local sm = fs.open_mount("save")
                if sm and sm.handle ~= 0 then
                    local cr, tx = fs.ipc_commit(sm.handle)
                    rt.close_handle(sm.handle)
                    kh_log(str_format("RETIRE: cache invalidated + alias neutralised tx=0x%x rc=0x%x", tx or 0, cr or 0))
                end
            end
        end)
        if not ctx.henStartup and not ctx.moaderUncertain then
            for i = #ctx.handles, 1, -1 do pcall(rt.close_handle, ctx.handles[i].handle) end
            _G.__KH_TRACKED = nil
            if _G.__KH then _G.__KH.sd = nil end
        end
    end
    return {step = function()
        assert(not completed, "KH: frame runner repeated after completion")
        if failed then
            live_stage("stopped; reboot before another attempt")
            retire()
            completed = true
            return true, nil, failed
        end
        if index <= #stages then
            local stage = stages[index]
            if not announced then
                live_stage(stage[1])
                announced = true
                return false
            end
            local ok, result = pcall(stage[2])
            if not ok then
                failed = tostring(result)
                live_stage("failed in " .. stage[1] .. ": " .. failed)
                return false
            end
            if result ~= false then
                index = index + 1
                announced = false
            end
            return false
        end
        if not announced then
            live_stage("waiting for kaiHEN's first package read")
            announced = true
            return false
        end
        local retire_wait_ms = (ctx.henStartup and ctx.henStartup.gateRestoreResult == 0xea01) and 8000 or 2000
        kh_log(str_format("=== HEN IS RUNNING === waiting %ds before retiring cache alias...", retire_wait_ms / 1000))
        rt.wait_ms(retire_wait_ms)
        retire()
        kh_log("=== READY! Close Seedsow Lullaby and hold R while launching a game ===")
        completed = true
        return true, ctx.henStartup
    end}
'''


def upstream_module():
    spec = importlib.util.spec_from_file_location(
        'moader_standalone', ROOT / 'tools/seedsow-standalone.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def replace_once(text: str, before: str, after: str, label: str) -> str:
    if text.count(before) != 1:
        raise ValueError('Pinned upstream Lua anchor differs: ' + label)
    return text.replace(before, after, 1)


def wire_bytes(text: str, name: str) -> bytes:
    if len(text.encode()) > 32000:
        raise ValueError(name + ' exceeds the pinned upstream source cap')
    if len(text.encode()) > 28600:
        text = '\n'.join('' if re.match(r'^\s*--', line) else line
                         for line in text.split('\n'))
    if len(text.encode()) > 28600:
        text = '\n'.join(re.sub(r'^\s+', '', line)
                         for line in text.split('\n'))
    data = text.encode()
    if len(data) > 28600:
        raise ValueError(name + ' exceeds the stock launcher wire cap')
    return data


def build(source: Path, output: Path) -> dict[str, Path]:
    upstream = upstream_module()
    source = upstream.qualify_source(source, upstream.profile())
    destination = output.absolute()
    if destination.exists():
        raise ValueError('Overlay output already exists: ' + str(destination))
    if destination.parent.resolve() != destination.parent:
        raise ValueError('Overlay parent must be canonical')
    original = source / 'console-only/lua'
    kh = (original / 's2_kh_ro.lua').read_text()
    kh = replace_once(kh,
        'function R.run_ro_and_bootstrap(M, env)\n',
        'function R.begin_ro_and_bootstrap(M, env)\n',
        'frame-driven RO stage')
    kh = replace_once(kh,
        '    local copy_loader_and_open_codefs = env.copy_loader_and_open_codefs\n',
        '    local copy_loader_and_open_codefs = env.copy_loader_and_open_codefs\n'
        '    local function live_stage(label)\n'
        '        local line = "MOADER KH: " .. label\n'
        '        if type(_G.moader_stage) == "function" then\n'
        '            _G.moader_stage(line)\n'
        '        elseif _G.n2h and type(_G.n2h.log) == "function" then\n'
        '            _G.n2h.log(line)\n'
        '        end\n'
        '    end\n', 'live KH stage marker')
    kh = replace_once(kh,
        '    local read_fs               = env.read_fs\n',
        '    local read_fs               = env.read_fs\n'
        '    local write_fs              = env.write_fs\n'
        '    local locate_by_clone       = env.locate_by_clone\n', 'Moader environment')
    kh = replace_once(kh,
        '    local function run_bootstrap_nro()\n'
        '        local ro_h = rt.get_service_sm("ldr:ro")\n',
        '    local ro_h\n'
        '    local function start_bootstrap_nro()\n'
        '        ro_h = rt.get_service_sm("ldr:ro")\n',
        'split bootstrap before Moader FS')
    kh = replace_once(kh,
        '        ctx.fsProcess = export_handle(0xFFFF8001, "fs-process")\n',
        '''        ctx.fsProcess = export_handle(0xFFFF8001, "fs-process")
        live_stage("loading Moader FS adapter")
        local moader_source = sd_read_file(open_sd_card(),
            "/kaihen/moader-fs/s2_moader.lua", 0x40000).str
        local moader_chunk, moader_syntax = loadstring(moader_source,
            "@/kaihen/moader-fs/s2_moader.lua")
        assert(moader_chunk, "KH: Moader FS installer syntax: " .. tostring(moader_syntax))
        local moader_module = moader_chunk()
        assert(type(moader_module) == "table" and type(moader_module.begin_install) == "function",
            "KH: Moader FS installer is missing")
        ctx.moaderFsRunner = moader_module.begin_install({rt=rt, fs=fs, ctx=ctx, slots=slots,
            arena_alloc=arena_alloc, track=track, open_sd_card=open_sd_card,
            sd_read_file=sd_read_file, read_fs=read_fs, write_fs=write_fs,
            compare_write_fs=compare_write_fs, locate_by_clone=locate_by_clone,
            log=kh_log})
    end
    local function finish_bootstrap_nro()
        assert(ctx.moaderFsVerified, "KH: Moader FS runtime was not verified")
        live_stage("Moader FS adapter verified")
''', 'Moader FS stage')
    kh = replace_once(kh,
        '        local sdh = open_sd_card()\n'
        '        local bs_file = sd_read_file(sdh, M.SD_MANIFEST.bootstrap.path, 0x100000)\n',
        '    end\n'
        '    local bootstrap_image\n'
        '    local function launch_bootstrap_nro()\n'
        '        local sdh = open_sd_card()\n'
        '        local bs_file = sd_read_file(sdh, M.SD_MANIFEST.bootstrap.path, 0x100000)\n',
        'bootstrap service and image frame boundary')
    kh = replace_once(kh,
        '        local mapped_entry = mapped_base + 0x80\n\n'
        '        local args_str = M.build_bootstrap_args({\n',
        '''        local mapped_entry = mapped_base + 0x80
        bootstrap_image = {entry=mapped_entry, offset=adapter_off, row=lrow,
            readiness=bs_readiness, domain=loader_dom}
    end
    local function start_bootstrap_worker()
        local mapped_entry, adapter_off = bootstrap_image.entry, bootstrap_image.offset
        local lrow, bs_readiness = bootstrap_image.row, bootstrap_image.readiness
        local loader_dom = bootstrap_image.domain
        local args_str = M.build_bootstrap_args({
''', 'bootstrap image and worker frame boundary')
    bootstrap_wait_start = '        local bs_thread = rt.rd_u64(th_slot)\n'
    bootstrap_wait_end = '        ctx.henStartup = rec\n        _G.__KH.stages.complete = true\n    end\n'
    if kh.count(bootstrap_wait_start) != 1 or kh.count(bootstrap_wait_end) != 1:
        raise ValueError('Pinned upstream Lua anchor differs: bootstrap wait')
    kh = (kh[:kh.index(bootstrap_wait_start)] + '''        -- The host's proven startup route polls the output record and never joins.
        -- Reading the thread pointer or joining can block while the worker drives FS.
        ctx.bootstrapArgsAddress = args_addr
        ctx.bootstrapDeadline = rt.now_ms() + 120000
    end

    local bootstrap_operations = {
        "begin", "park Loader", "launch", "mount package", "exchange",
        "restore Loader", "PM worker settled", "start HEN", "connect", "complete",
        "prepare package", "game config", "game handles", "game handles",
        "game start", "read policy", "write package", "plugin scan",
        "plugin package", "plugin lookup", "plugin handoff", "plugin ready",
        "wait PM worker"
    }
    local function poll_bootstrap_nro()
        local now = rt.now_ms()
        if now < (ctx.bootstrapNextPoll or 0) then return false end
        ctx.bootstrapNextPoll = now + 1000
        local out24 = rt.rd(ctx.bootstrapArgsAddress + 408, 24)
        local op, res = P.u32(out24, 0), P.u32(out24, 4)
        if op ~= ctx.bootstrapLastOperation then
            ctx.bootstrapLastOperation = op
            live_stage(str_format("bootstrap %s (%d)", bootstrap_operations[op] or "step", op))
        end
        if op ~= 10 and res == 0 then
            assert(now < ctx.bootstrapDeadline,
                str_format("KH: bootstrap timed out at operation %d", op))
            return false
        end
        -- The module publishes Complete or its error before exiting. The SDK
        -- join remains unsafe even after that publication on this build.
        rt.wait_ms(500)
        out24 = rt.rd(ctx.bootstrapArgsAddress + 408, 24)
        local rec = {
            operation            = P.u32(out24, 0),
            result               = P.u32(out24, 4),
            pid                  = P.u64(out24, 8),
            gateRestoreResult    = P.u32(out24, 16),
            channelRestoreResult = P.u32(out24, 20),
        }
        kh_log(str_format("bootstrap record: op=%d result=0x%x pid=%d gateRestore=0x%x chanRestore=0x%x",
            rec.operation, rec.result, rec.pid, rec.gateRestoreResult, rec.channelRestoreResult))
        assert(rec.operation == 10 and rec.result == 0 and
            (rec.gateRestoreResult == 0 or rec.gateRestoreResult == 0xea01) and rec.channelRestoreResult == 0,
            str_format("KH: bootstrap failed op=%d rc=0x%x", rec.operation, rec.result))
        ctx.henStartup = rec
        _G.__KH.stages.complete = true
        ctx.moaderUncertain = false
        _G.__MOADER_FS_UNCERTAIN = false
    end
''' + kh[kh.index(bootstrap_wait_end) + len(bootstrap_wait_end):])
    start = '    -- Execute Stage C pipeline with Stage D post-HEN alias retirement in `finally`\n'
    end = '\nend\n\nreturn R\n'
    if kh.count(start) != 1 or kh.count(end) != 1:
        raise ValueError('Pinned upstream Lua anchor differs: frame-driven KH pipeline')
    kh = kh[:kh.index(start)] + FRAME_RO_RUNNER + kh[kh.index(end):]

    stage = (original / 's2_kaihen.lua').read_text()
    stage = replace_once(stage,
        '        write_fs_unverified         = write_fs_unverified,\n',
        '        write_fs_unverified         = write_fs_unverified,\n'
        '        write_fs                    = write_fs,\n', 'FS writer handoff')
    stage = replace_once(stage,
        '        export_handle               = export_handle,\n',
        '        export_handle               = export_handle,\n'
        '        locate_by_clone             = locate_by_clone,\n',
        'owned source locator handoff')
    stage = replace_once(stage,
        'function M.run_kaihen(rt, fs, log_fn)\n',
        'function M.begin_kaihen(rt, fs, log_fn)\n',
        'frame-driven kaiHEN entry')
    stage = replace_once(stage,
        '    return require("s2_kh_ro").run_ro_and_bootstrap(M, {\n',
        '    return require("s2_kh_ro").begin_ro_and_bootstrap(M, {\n',
        'frame-driven RO handoff')
    stage = replace_once(stage,
        '\nreturn M\n',
        '''
function M.run_kaihen(rt, fs, log_fn)
    local runner = M.begin_kaihen(rt, fs, log_fn)
    while true do
        local done, receipt, failure = runner.step()
        if done then
            if failure then error(failure) end
            return receipt
        end
    end
end

return M
''', 'synchronous fallback')

    main = (original / 's2_main.lua').read_text()
    main = replace_once(main,
        '    local t_attempt0, t_r0, t_p0 = 0, 0, 0\n',
        '    local t_attempt0, t_r0, t_p0 = 0, 0, 0\n'
        '    local kh_runner, t_ro0 = nil, 0\n',
        'frame-driven kaiHEN state')
    main = replace_once(main,
        '    local printed_idx = 0\n    local screen_lines = 0\n',
        '''    local printed_idx = 0
    local screen_lines = 0
    _G.moader_stage = function(line)
        if screen_lines >= 18 and _G.n2h_clear then
            _G.n2h_clear()
            screen_lines = 0
            default_log("Moader standalone: current boot")
            screen_lines = 1
        end
        default_log(line)
        screen_lines = screen_lines + 1
    end
''', 'bounded live screen buffer')
    main = replace_once(main,
        '            if not ln:find("^Wave %d") then\n',
        '            if not ln:find("^Wave %d") and\n'
        '                (not ln:find("^KH:") or ln:find("READY") or\n'
        '                 ln:find("HEN IS RUNNING")) then\n',
        'broad screen messages')
    main = replace_once(main,
        '            elseif phase == "kaihen" then\n'
        '                local t_ro0 = rt.now_ms()\n'
        '                local kaihen_mod = load_kaihen_module(rt, fs, buf_log)\n',
        '            elseif phase == "kaihen" then\n'
        '                if not kh_runner then\n'
        '                    t_ro0 = rt.now_ms()\n'
        '                    default_log("MOADER KH: entering Stage C")\n'
        '                    local kaihen_mod = load_kaihen_module(rt, fs, buf_log)\n'
        '                    kh_runner = kaihen_mod.begin_kaihen(rt, fs, buf_log)\n'
        '                    default_log("MOADER KH: Stage C ready")\n'
        '                    return\n'
        '                end\n',
        'frame-driven A path entry')
    main = replace_once(main,
        '''                local ok_kh, startup_or_err = pcall(kaihen_mod.run_kaihen, rt, fs, buf_log)
                local ro_ms = math.floor((rt.now_ms() - t_ro0) + 0.5)
''',
        '''                local done, startup_or_err, failure = kh_runner.step()
                flush_new_lines()
                if not done then return end
                local ro_ms = math.floor((rt.now_ms() - t_ro0) + 0.5)
''', 'frame-driven A path step')
    main = replace_once(main,
        '''                if not ok_kh then
                    buf_log("KAIHEN ERROR: " .. tostring(startup_or_err))
                end
''',
        '''                if failure then
                    buf_log("KAIHEN ERROR: " .. tostring(failure))
                end
''', 'frame-driven A path failure')
    main = replace_once(main,
        '        if opts.sdLog ~= false and rt and fs then\n',
        '        if opts.sdLog ~= false and rt and fs and not _G.__MOADER_FS_UNCERTAIN then\n',
        'uncertain log transport')

    b_entry = (original / 'exploita2.lua').read_text()
    b_entry = replace_once(b_entry,
        '-- exploita2.lua — Slot 1 (Button A) Launcher Entry for Semi-Untethered fsp-srv + kaiHEN',
        '-- exploitb2.lua — Slot 2 (Button B), pinned upstream Seedsow chain',
        'B entry identity')
    b_entry = replace_once(b_entry,
        '-- Loaded automatically when pressing \'A\' on the Artemis launcher screen.',
        '-- Loaded automatically when pressing \'B\' on the Artemis launcher screen.',
        'B entry button')
    b_entry = replace_once(b_entry,
        '        dofile("save:/s2_main.lua")\n',
        '        package.loaded["s2_kh_ro"] = dofile("save:/s2_b_kh_ro.lua")\n'
        '        package.loaded["s2_kaihen"] = dofile("save:/s2_b_kaihen.lua")\n'
        '        dofile("save:/s2_b_main.lua")\n',
        'B entry module isolation')
    b_entry = replace_once(b_entry, 'exploita2 error:', 'exploitb2 error:',
                           'B entry error')

    inject = (original / 'inject2.lua').read_text()
    inject = replace_once(inject,
        ' A : Run semi-untethered kaiHEN (exploita2.lua)',
        ' A : Run Moader standalone (exploita2.lua)', 'A menu label')
    inject = replace_once(inject, ' B : Slot 2 (exploitb.lua)',
        ' B : Upstream Seedsow Lua (shared SD kaiHEN)', 'B menu label')

    destination.mkdir(parents=True)
    files = {'s2_kh_ro.lua': kh, 's2_kaihen.lua': stage, 's2_main.lua': main,
             's2_b_kh_ro.lua': (original / 's2_kh_ro.lua').read_text(),
             's2_b_kaihen.lua': (original / 's2_kaihen.lua').read_text(),
             's2_b_main.lua': (original / 's2_main.lua').read_text(),
             'exploitb2.lua': b_entry, 'inject2.lua': inject}
    for name, text in files.items():
        data = wire_bytes(text, name)
        target = destination / name
        target.write_bytes(data)
        subprocess.run(['luac', '-p', str(target)], check=True, timeout=10)
    return {name: destination / name for name in files}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    files = build(args.source, args.output)
    for name, file in files.items():
        print(name, file.stat().st_size, file)


if __name__ == '__main__':
    main()
