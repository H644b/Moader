#!/usr/bin/env python3
import pathlib, subprocess, tempfile, zipfile, struct, zlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build/host'
BUILD.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='moader-core-') as temp:
    work = pathlib.Path(temp).resolve()
    fixtures = work / 'fixtures'
    fixtures.mkdir()
    def archive(name, items):
        with zipfile.ZipFile(fixtures / (name + '.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
            for key, data in items: z.writestr(key, data)
    archive('good', [('romfs/Model/sample.bin', b'example texture'), ('romfs/config.txt', b'configuration')])
    archive('wrapped', [('Example/romfs/Model/other.bin', b'other')])
    archive('exefs', [('exefs/main', b'NSO0 test data; no executable is run')])
    archive('traversal', [('romfs/../../escape', b'bad')])
    archive('case-collision', [('romfs/a.bin', b'one'), ('romfs/A.bin', b'two')])
    archive('metadata-only', [('mod.meta', b'no mod payload')])
    link = zipfile.ZipInfo('romfs/link')
    link.create_system = 3
    link.external_attr = 0o120777 << 16
    with zipfile.ZipFile(fixtures / 'symlink.zip', 'w') as z: z.writestr(link, '../../escape')
    archive('bad-crc', [('romfs/file', b'crc')])
    data = bytearray((fixtures / 'bad-crc.zip').read_bytes())
    pos = data.index(b'PK\x01\x02')
    struct.pack_into('<I', data, pos + 16, 123456)
    (fixtures / 'bad-crc.zip').write_bytes(data)
    archive('underdeclared', [('romfs/file', b'x' * 200000)])
    data = bytearray((fixtures / 'underdeclared.zip').read_bytes())
    pos = data.index(b'PK\x01\x02')
    struct.pack_into('<I', data, pos + 24, 4)
    (fixtures / 'underdeclared.zip').write_bytes(data)
    binary = BUILD / 'core-test'
    subprocess.run(['clang++', '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(ROOT / 'include'),
                    str(ROOT / 'tests/core_test.cpp'), str(ROOT / 'source/model.cpp'),
                    str(ROOT / 'source/library.cpp'), str(ROOT / 'source/zip.cpp'), '-lz', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(fixtures), str(work / 'library')], check=True)

    native_binary = BUILD / 'core-test-native-publication'
    subprocess.run(['clang++', '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-DMOADER_TEST_NONREPLACING_RENAME', '-I' + str(ROOT / 'include'),
                    str(ROOT / 'tests/core_test.cpp'), str(ROOT / 'source/model.cpp'),
                    str(ROOT / 'source/library.cpp'), str(ROOT / 'source/zip.cpp'), '-lz', '-o', str(native_binary)], check=True)
    subprocess.run([str(native_binary), str(fixtures), str(work / 'native-library')], check=True)
