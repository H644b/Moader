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
        '    local read_fs               = env.read_fs\n',
        '    local read_fs               = env.read_fs\n'
        '    local write_fs              = env.write_fs\n'
        '    local locate_by_clone       = env.locate_by_clone\n', 'Moader environment')
    kh = replace_once(kh,
        '        ctx.fsProcess = export_handle(0xFFFF8001, "fs-process")\n',
        '''        ctx.fsProcess = export_handle(0xFFFF8001, "fs-process")
        local moader_source = sd_read_file(open_sd_card(),
            "/kaihen/moader-fs/s2_moader.lua", 0x40000).str
        local moader_chunk, moader_syntax = loadstring(moader_source,
            "@/kaihen/moader-fs/s2_moader.lua")
        assert(moader_chunk, "KH: Moader FS installer syntax: " .. tostring(moader_syntax))
        local moader_module = moader_chunk()
        assert(type(moader_module) == "table" and type(moader_module.install) == "function",
            "KH: Moader FS installer is missing")
        ctx.moaderFs = moader_module.install({rt=rt, fs=fs, ctx=ctx, slots=slots,
            arena_alloc=arena_alloc, track=track, open_sd_card=open_sd_card,
            sd_read_file=sd_read_file, read_fs=read_fs, write_fs=write_fs,
            compare_write_fs=compare_write_fs, locate_by_clone=locate_by_clone,
            log=kh_log})
        assert(ctx.moaderFsVerified, "KH: Moader FS runtime was not verified")
''', 'Moader FS stage')
    kh = replace_once(kh,
        '        run_bootstrap_nro()\n',
        '        run_bootstrap_nro()\n'
        '        ctx.moaderUncertain = false\n'
        '        _G.__MOADER_FS_UNCERTAIN = false\n', 'HEN handoff completion')
    kh = replace_once(kh,
        '    pcall(function()\n        if ctx.candidateHandle and ctx.candidateHandle ~= 0 then\n',
        '    pcall(function()\n        if not ctx.moaderUncertain and ctx.candidateHandle and ctx.candidateHandle ~= 0 then\n',
        'uncertain cache retirement')
    kh = replace_once(kh,
        '    if not ctx.henStartup then\n',
        '    if not ctx.henStartup and not ctx.moaderUncertain then\n',
        'uncertain handle cleanup')

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

    main = (original / 's2_main.lua').read_text()
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
