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
    archive('game-source-spoof', [('romfs/file', b'payload'), ('.game-source', b'2')])
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

    def raw_deflate_archive(name, packed, expanded, crc=None, declared_size=None):
        # Construct the compressed stream independently of the ZIP writer so
        # exact decoder buffer boundaries remain reproducible across zlib builds.
        entry = b'romfs/boundary.bin'
        crc = zlib.crc32(expanded) if crc is None else crc
        size = len(expanded) if declared_size is None else declared_size
        local = (struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, 0, 8, 0, 0,
                             crc, len(packed), size, len(entry), 0) + entry + packed)
        central = (struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 20, 20, 0, 8, 0, 0,
                               crc, len(packed), size, len(entry), 0, 0, 0, 0, 0, 0) + entry)
        end = struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, 1, 1, len(central), len(local), 0)
        (fixtures / (name + '.zip')).write_bytes(local + central + end)

    compressor = zlib.compressobj(wbits=-15)
    prefix = compressor.compress(b'A' * 15) + compressor.flush(zlib.Z_SYNC_FLUSH)
    assert len(prefix) == 10
    payload = b'B' * (32768 - len(prefix) - 5)
    stored = struct.pack('<HH', len(payload), len(payload) ^ 0xffff) + payload
    chunk = prefix + b'\x00' + stored
    expanded_chunk = b'A' * 15 + payload
    assert len(chunk) == len(expanded_chunk) == 32768
    final = b'\x01\x01\x00\xfe\xffZ'
    packed, expanded = chunk + final, expanded_chunk + b'Z'
    variants = {
        'boundary': (packed, expanded),
        'boundary-repeated': (chunk * 4 + final, expanded_chunk * 4 + b'Z'),
        'boundary-empty-final': (chunk + b'\x01\x00\x00\xff\xff', expanded_chunk),
        'boundary-final': (prefix + b'\x01' + stored, expanded_chunk),
    }
    for name, (compressed, expected) in variants.items():
        assert zlib.decompress(compressed, -15) == expected
        raw_deflate_archive(name, compressed, expected)
        with zipfile.ZipFile(fixtures / (name + '.zip')) as z:
            assert z.testzip() is None and z.read('romfs/boundary.bin') == expected
        (fixtures / (name + '.bin')).write_bytes(expected)
    raw_deflate_archive('boundary-truncated', packed[:-1], expanded)
    raw_deflate_archive('boundary-no-end', chunk, expanded_chunk)
    raw_deflate_archive('boundary-trailing', packed + b'junk', expanded)
    raw_deflate_archive('boundary-trailing-unread', variants['boundary-final'][0] + b'junk', expanded_chunk)
    raw_deflate_archive('boundary-corrupt', chunk + b'\x07', expanded)
    raw_deflate_archive('boundary-crc', packed, expanded, crc=zlib.crc32(expanded) ^ 1)
    raw_deflate_archive('boundary-underdeclared', packed, expanded, declared_size=len(expanded) - 1)
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
