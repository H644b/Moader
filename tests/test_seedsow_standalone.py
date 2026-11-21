#!/usr/bin/env python3
"""Exercise standalone source pin and SD writer without a console or native run."""

from pathlib import Path
import importlib.util
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
loader = importlib.util.spec_from_file_location(
    'moader_seedsow_standalone', ROOT / 'tools/seedsow-standalone.py')
standalone = importlib.util.module_from_spec(loader)
loader.loader.exec_module(standalone)
overlay_loader = importlib.util.spec_from_file_location(
    'moader_seedsow_standalone_overlay', ROOT / 'tools/seedsow-standalone-overlay.py')
standalone_overlay = importlib.util.module_from_spec(overlay_loader)
overlay_loader.loader.exec_module(standalone_overlay)


def refused(operation):
    try:
        operation()
    except ValueError:
        return
    raise AssertionError('Expected standalone preparation to refuse this input')


def main():
    standalone.qualify_upload_receipt('commit_rc=0x0 asb=none\nUpload complete.\n', 0)
    refused(lambda: standalone.qualify_upload_receipt('Upload complete.\n', 0))
    refused(lambda: standalone.qualify_upload_receipt('commit_rc=0x203\n', 0))
    refused(lambda: standalone.qualify_upload_receipt('commit_rc=0x0\n', 1))
    expected_save = {'inject2.lua': 4122, 'exploitb2.lua': 866}
    probe = ('status: ok\noutput:\n'
             'MOADER_SAVE\tinject2.lua\tvisible\t4122\n'
             'MOADER_SAVE\texploitb2.lua\tvisible\t866\n')
    assert standalone.qualify_save_probe(probe, expected_save) == {
        'inject2.lua': ('visible', 4122),
        'exploitb2.lua': ('visible', 866)}
    refused(lambda: standalone.qualify_save_probe(
        probe.replace('visible\t866', 'missing\t-1'), expected_save))
    refused(lambda: standalone.qualify_save_probe(
        probe.replace('MOADER_SAVE\texploitb2.lua\tvisible\t866\n', ''), expected_save))
    if len(sys.argv) == 2:
        source = Path(sys.argv[1])
        spec = standalone.profile()
        standalone.qualify_source(source, spec)
        lua = source / 'console-only/lua/s2_kaihen.lua'
        original = lua.read_bytes()
        try:
            lua.write_bytes(original + b'\n-- changed\n')
            refused(lambda: standalone.qualify_source(source, spec))
        finally:
            lua.write_bytes(original)
        standalone.qualify_source(source, spec)
        with tempfile.TemporaryDirectory(prefix='moader-overlay-test-') as overlay_dir:
            uploader = standalone.prepare_upload_tree(source, Path(overlay_dir), 'node')
            assert uploader.is_file()
            prepared = uploader.parent / 'lua'
            for name in ('s2_kh_ro.lua', 's2_kaihen.lua', 's2_main.lua'):
                assert (prepared / name).read_bytes() != (source / 'console-only/lua' / name).read_bytes()
            for original, stock in (('s2_kh_ro.lua', 's2_b_kh_ro.lua'),
                                    ('s2_kaihen.lua', 's2_b_kaihen.lua'),
                                    ('s2_main.lua', 's2_b_main.lua')):
                assert (prepared / stock).read_bytes() == standalone_overlay.wire_bytes(
                    (source / 'console-only/lua' / original).read_text(), stock)
            b_entry = (prepared / 'exploitb2.lua').read_text()
            assert 'package.loaded["s2_kaihen"] = dofile("save:/s2_b_kaihen.lua")' in b_entry
            assert 'dofile("save:/s2_b_main.lua")' in b_entry
            assert "['exploitb2.lua', 'exploitb.lua']" in uploader.read_text()

    with tempfile.TemporaryDirectory(prefix='moader-standalone-test-') as temp:
        sd = Path(temp).resolve() / 'sd'
        sd.mkdir()
        files = {'kaihen/bin/hen/main': b'hen', 'switch/Moader.nro': b'app'}
        standalone.stage_sd(sd, files)
        standalone.verify_sd(sd, files)
        (sd / 'switch/Moader.nro').write_bytes(b'wrong')
        refused(lambda: standalone.verify_sd(sd, files))
        outside = Path(temp) / 'outside'
        outside.mkdir()
        (sd / 'switch/Moader.nro').unlink()
        (sd / 'switch').rename(sd / 'switch-old')
        (sd / 'switch').symlink_to(outside, target_is_directory=True)
        refused(lambda: standalone.stage_sd(sd, files))
        assert not (outside / 'Moader.nro').exists()
    print('Seedsow standalone source pin and SD writer checks passed.')


if __name__ == '__main__':
    main()
