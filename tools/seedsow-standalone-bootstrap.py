#!/usr/bin/env python3
"""Build the source-qualified Seedsow standalone bootstrap without SDK thread join."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
RELEASE_SHA256 = '4f18cb99ac810855de180bc6d4f5ec8d32fc40e9e2512e6cd23a33e2c79c8d57'
PREVIOUS_SHA256 = '0c18d72bb15043f67fa554d8390a385cfc283a1257a7b3e2116e1ae28a054738'
STANDALONE_SHA256 = 'f3f4305d53d5e3da9dac4b054acdcdc3c2addb436d813b2e08c2a9f32f1733ea'
PREVIOUS_ADAPTER_SHA256 = '8fec5d13f5c4bec04a83daa3bc4ccf07f687fdacbef1bbf05457bfa12231b281'
ADAPTER_SHA256 = '244b57ab97fd9fa160e522f1387d64747d4b5999f1a28f71a1429135d01dfce2'


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def replace_once(source: str, before: str, after: str, label: str) -> str:
    if source.count(before) != 1:
        raise ValueError('Pinned bootstrap source differs: ' + label)
    return source.replace(before, after, 1)


def patch_source(bootstrap: Path) -> None:
    main = bootstrap / 'source/main.cpp'
    text = main.read_text()
    text = replace_once(text,
        '''static void _launchRestoreAndJoin(LaunchState* args, BootstrapGate& gate,
                                  BootstrapMemory& memory, ounce::CompilerTlsScope& tls,
                                  bool require_complete) {''',
        '''static void _launchRestoreAndJoin(LaunchState* args, BootstrapGate& gate,
                                  BootstrapMemory& memory, bool require_complete) {''',
        'restore signature')
    text = replace_once(text,
        '''    if (args->thread) {
        args->mark(BootstrapOperation_Join);
        tls.suspend();
        u64 returned = 0;
        args->input.pthread_join(args->thread, &returned, 0);
        tls.resume();
    }
''',
        '''    if (args->thread) {
        // The SDK thread sets completed before returning. pthread_join can
        // block in pthread_exit after that store, so wait for the result cell
        // and retain the mapped bootstrap image until the app exits.
        // Before the wait, publish the Loader exchange and restoration state
        // through the existing output fields. process_id is diagnostic until
        // a successful start replaces it with the launched process ID.
        if (args->report) {
            __atomic_store_n(&args->report->process_id,
                (static_cast<u64>(args->mounts) << 1) |
                    (args->exchanged ? 1u : 0u), __ATOMIC_RELEASE);
            __atomic_store_n(&args->report->gate_restore_result,
                args->gate_restore_result, __ATOMIC_RELEASE);
            __atomic_store_n(&args->report->channel_restore_result,
                args->channel_restore_result, __ATOMIC_RELEASE);
        }
        args->mark(BootstrapOperation_WaitPm);
        while (!__atomic_load_n(&args->pm.completed, __ATOMIC_ACQUIRE)) {
            svcSleepThread(1000000);
        }
        args->mark(BootstrapOperation_Join);
    }
''', 'PM worker join')
    text = replace_once(text,
        '    _launchRestoreAndJoin(args, gate, memory, tls, plugin);',
        '    _launchRestoreAndJoin(args, gate, memory, plugin);',
        'restore call')
    main.write_text(text)

    header = bootstrap / 'source/startup.hpp'
    text = header.read_text()
    text = replace_once(text, '    BootstrapOperation_PluginReady = 22,\n',
                        '    BootstrapOperation_PluginReady = 22,\n'
                        '    BootstrapOperation_WaitPm = 23,\n', 'progress operation')
    header.write_text(text)


def build(output: Path, sdk: Path) -> dict:
    output = output.absolute()
    if output.exists():
        raise ValueError('Standalone bootstrap output already exists: ' + str(output))
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.parent.resolve() != output.parent:
        raise ValueError('Standalone bootstrap parent must be canonical')
    sdk = sdk.resolve(strict=True)
    compiler = sdk / 'devkitA64/bin/aarch64-none-elf-g++'
    library = sdk / 'libounce/lib/libounce_core.a'
    if not compiler.is_file() or not library.is_file():
        raise ValueError('Pinned devkitA64 and libounce are required')
    with tempfile.TemporaryDirectory(prefix='moader-standalone-bootstrap-',
                                     dir=output.parent) as temporary:
        source = Path(temporary) / 'kaihen'
        shutil.copytree(ROOT / 'runtime/kaihen', source,
                        ignore=shutil.ignore_patterns('build', '*.o', '*.d',
                                                      '*.elf', '*.map', '*.bin'))
        for item in source.rglob('*'):
            if item.is_symlink():
                raise ValueError('Standalone bootstrap source contains a symlink')
        bootstrap = source / 'bootstrap'
        command = ['make', '-C', str(bootstrap), '-j4',
                   'DEVKITPRO=' + str(sdk), 'LIBOUNCE=' + str(sdk / 'libounce')]
        subprocess.run(command, check=True, timeout=180,
                       stdout=subprocess.DEVNULL)
        original = (bootstrap / 'build/bootstrap.bin').read_bytes()
        if sha(original) != RELEASE_SHA256:
            raise ValueError('Native source/toolchain did not reproduce the release bootstrap')
        patch_source(bootstrap)
        subprocess.run(['make', '-C', str(bootstrap), 'clean'], check=True,
                       timeout=30, stdout=subprocess.DEVNULL)
        subprocess.run(command, check=True, timeout=180,
                       stdout=subprocess.DEVNULL)
        candidate = (bootstrap / 'build/bootstrap.bin').read_bytes()
        if sha(candidate) != STANDALONE_SHA256:
            raise ValueError('Standalone bootstrap bytes differ from the reviewed candidate: '
                             + sha(candidate))
        if sha((ROOT / 'standalone/s2_moader.lua').read_bytes()) != ADAPTER_SHA256:
            raise ValueError('Standalone FS adapter differs from the reviewed candidate')
        output.mkdir()
        (output / 'bootstrap.bin').write_bytes(candidate)
        proof = {'releaseBootstrapSha256': sha(original),
                 'standaloneBootstrapSha256': sha(candidate),
                 'adapterSha256': ADAPTER_SHA256,
                 'bytes': len(candidate),
                 'patch': 'wait for PM completion and publish Loader restoration state',
                 'scope': 'source-qualified build; one Profile B Demo same-boot FS/HEN result recorded'}
        (output / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
        return proof


def stage_candidate(output: Path, sd_path: Path, payload_dir: Path) -> dict:
    candidate = (output / 'bootstrap.bin').read_bytes()
    proof = json.loads((output / 'proof.json').read_text())
    if (sha(candidate) != STANDALONE_SHA256 or
            proof.get('releaseBootstrapSha256') != RELEASE_SHA256 or
            proof.get('standaloneBootstrapSha256') != STANDALONE_SHA256 or
            proof.get('adapterSha256') != ADAPTER_SHA256 or
            proof.get('bytes') != len(candidate)):
        raise ValueError('Standalone bootstrap build proof differs')

    module = importlib.util.spec_from_file_location(
        'moader_standalone_stager', ROOT / 'tools/seedsow-standalone.py')
    standalone = importlib.util.module_from_spec(module)
    module.loader.exec_module(standalone)
    files, _ = standalone.moader_sd_files()
    payload, _ = standalone.standalone_payload_files(payload_dir)
    files.update(payload)
    bootstrap_paths = ('kaihen/bootstrap.bin', 'kaihen/bin/bootstrap.bin')
    adapter_path = 'kaihen/moader-fs/s2_moader.lua'
    paths = (*bootstrap_paths, adapter_path)
    if any(sha(files[path]) != RELEASE_SHA256 for path in bootstrap_paths):
        raise ValueError('Pinned SD bundle does not contain the release bootstrap')
    if sha(files[adapter_path]) != ADAPTER_SHA256:
        raise ValueError('Pinned SD adapter differs from the reviewed candidate')
    root = standalone.sd_root(sd_path)
    standalone.verify_sd(root, {name: data for name, data in files.items()
                                if name not in paths})
    current_hashes = {path: sha(standalone.checked_target(root, path).read_bytes())
                      for path in paths}
    if [current_hashes[path] for path in bootstrap_paths] not in (
            [RELEASE_SHA256] * 2, [PREVIOUS_SHA256] * 2,
            [STANDALONE_SHA256] * 2):
        raise ValueError('SD bootstrap copies have different or unknown bytes')
    if current_hashes[adapter_path] not in (
            PREVIOUS_ADAPTER_SHA256, ADAPTER_SHA256):
        raise ValueError('SD adapter has unknown bytes')
    staged = {path: candidate for path in bootstrap_paths}
    staged[adapter_path] = files[adapter_path]
    standalone.stage_sd(root, staged)
    files.update(staged)
    standalone.verify_sd(root, files)
    return {'sdRoot': str(root), 'changedPaths': list(paths),
            'previousBootstrapSha256': current_hashes[bootstrap_paths[0]],
            'previousAdapterSha256': current_hashes[adapter_path],
            'bootstrapReadbackSha256': STANDALONE_SHA256,
            'adapterReadbackSha256': ADAPTER_SHA256,
            'verifiedFileCount': len(files), 'scope': 'SD only; no FS/HEN run'}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'build/standalone-bootstrap')
    parser.add_argument('--sdk', type=Path, default=ROOT / 'vendor/devkitpro')
    parser.add_argument('--stage-sd', type=Path,
                        help='replace both bootstrap copies and the adapter on a matched mounted SD card')
    parser.add_argument('--payload-dir', type=Path,
                        help='source-qualified FS payload, required with --stage-sd')
    options = parser.parse_args()
    if options.stage_sd is not None:
        if options.payload_dir is None:
            parser.error('--payload-dir is required with --stage-sd')
        result = stage_candidate(options.output, options.stage_sd,
                                 options.payload_dir)
    else:
        result = build(options.output, options.sdk)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
