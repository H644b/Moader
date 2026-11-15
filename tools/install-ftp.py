#!/usr/bin/env python3
"""Install the normal NRO and stage its plugin; never patch a running process."""
from pathlib import Path
import argparse
import datetime
import ftplib
import hashlib
import io
import ipaddress
import json
import os
import secrets

ROOT = Path(__file__).resolve().parent.parent
MAX_ARTIFACT = 32 * 1024 * 1024


def valid_code(token):
    return len(token) == 6 and all(48 <= digit <= 57 for digit in token)


def private_write(path, data):
    """Publish local pairing material with private permissions from creation."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.pending-' + secrets.token_hex(6))
    try:
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


class Installer:
    def __init__(self, host, port=1337, root=ROOT, ftp_factory=ftplib.FTP):
        self.host = str(ipaddress.IPv4Address(host))
        if not 1 <= port <= 65535:
            raise ValueError('FTP port must be between 1 and 65535')
        self.port, self.root, self.ftp_factory = port, Path(root), ftp_factory
        self.stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
        self.folder = self.root / 'evidence/ftp'
        self.journal = self.folder / ('install-' + self.stamp + '.json')
        self.record = {'host': self.host, 'port': self.port, 'started': self.stamp,
                       'files': [], 'complete': False,
                       'runningProcessesPatched': False, 'pluginStarted': False}

    def save(self):
        self.folder.mkdir(parents=True, exist_ok=True)
        temporary = self.journal.with_suffix('.json.pending')
        temporary.write_text(json.dumps(self.record, indent=2) + '\n')
        os.replace(temporary, self.journal)

    def connect(self, directory):
        ftp = self.ftp_factory()
        try:
            ftp.connect(self.host, self.port, timeout=15)
            ftp.login('anonymous', 'moader')
            ftp.cwd(directory)
            if ftp.pwd().rstrip('/') != directory.rstrip('/'):
                raise RuntimeError('FTP did not select the requested SD directory')
            return ftp
        except BaseException:
            ftp.close()
            raise

    @staticmethod
    def listing(ftp):
        lines = []
        ftp.retrlines('LIST', lines.append)
        return {line.split(maxsplit=8)[-1] for line in lines}

    @staticmethod
    def retrieve(ftp, name, limit):
        data = bytearray()

        def received(part):
            if len(data) + len(part) > limit:
                raise RuntimeError('FTP readback exceeded its expected bound')
            data.extend(part)
        ftp.retrbinary('RETR ' + name, received)
        return bytes(data)

    def ensure_directory(self, parent, name):
        ftp = self.connect(parent)
        try:
            if name not in self.listing(ftp):
                ftp.mkd(name)
        finally:
            ftp.close()
        # Verify the exact final directory even when it already existed.
        self.connect(parent.rstrip('/') + '/' + name).close()

    def install_bytes(self, data, directory, name, source=None, private=False):
        expected = hashlib.sha256(data).hexdigest()
        # A six-digit code's digest is reversible by enumeration. Neither the
        # journal nor remote temporary filenames include that digest or code.
        suffix = secrets.token_hex(6) if private else expected[:12]
        temporary = name + '.pending-' + suffix
        item = {'target': directory + '/' + name, 'bytes': len(data), 'verified': False,
                'temporary': directory + '/' + temporary, 'stagedVerified': False}
        if source is not None:
            item['source'] = str(source)
        if not private:
            item['sha256'] = expected
        self.record['files'].append(item)
        self.save()
        ftp = self.connect(directory)
        try:
            existing = self.listing(ftp)
            if name in existing:
                previous = self.retrieve(ftp, name, 128 if private else MAX_ARTIFACT)
                backup_suffix = self.stamp if private else hashlib.sha256(previous).hexdigest()[:16]
                backup = self.folder / (name + '-' + backup_suffix + '.previous')
                if private:
                    private_write(backup, previous)
                else:
                    backup.write_bytes(previous)
                item['previousBackup'] = str(backup)
                self.save()
            ftp.storbinary('STOR ' + temporary, io.BytesIO(data))
            if self.retrieve(ftp, temporary, len(data)) != data:
                raise RuntimeError('Staged FTP bytes differ: ' + name)
            item['stagedVerified'] = True
            self.save()
            try:
                if name in existing:
                    # FTPVita/Horizon cannot replace an existing rename target.
                    backup_name = name + '.previous-' + self.stamp
                    ftp.rename(name, backup_name)
                    item['remoteBackup'] = directory + '/' + backup_name
                    self.save()
                ftp.rename(temporary, name)
            except ftplib.all_errors as error:
                # Retain compatibility with FTP servers without rename support.
                # The complete old bytes are already backed up locally and the
                # staged bytes have passed a full readback. Journal the fallback
                # before touching the final path; never report a partial install.
                item['renameFallback'] = repr(error)
                self.save()
                ftp.close()
                ftp = self.connect(directory)
                ftp.storbinary('STOR ' + name, io.BytesIO(data))
            if self.retrieve(ftp, name, len(data)) != data:
                raise RuntimeError('Final FTP bytes differ: ' + name)
            item['verified'] = True
            self.save()
            if temporary in self.listing(ftp):
                ftp.voidcmd('DELE ' + temporary)
            print('Verified', item['target'], len(data), 'bytes')
        finally:
            ftp.close()

    def pairing_code(self):
        ftp = self.connect('/sdmc:/moader')
        try:
            previous = self.retrieve(ftp, 'pairing-code', 128) if 'pairing-code' in self.listing(ftp) else b''
        finally:
            ftp.close()
        # Accept the optional final LF used by native getline. Migrate other
        # whitespace/extra lines so local and native codes cannot diverge.
        token = previous[:-1] if previous.endswith(b'\n') else previous
        if not valid_code(token):
            token = ('%06d' % secrets.randbelow(1000000)).encode('ascii')
            self.install_bytes(token, '/sdmc:/moader', 'pairing-code', private=True)
            self.record['pairingCodeSeeded'] = True
        private_write(self.root / 'state/devices' / self.host / 'pairing-code', token)
        self.record['pairingCodeAvailableLocally'] = True
        self.save()

    def run(self):
        self.save()
        try:
            # Validate both artifacts before the first FTP connection or write.
            artifacts = [(self.root / 'dist/Moader.nro', '/sdmc:/switch', 'Moader.nro'),
                         (self.root / 'dist/moader.nso', '/sdmc:/kaihen/plugins', 'moader.nso')]
            payloads = []
            for source, directory, name in artifacts:
                if not 0 < source.stat().st_size <= MAX_ARTIFACT:
                    raise RuntimeError('Artifact must be nonempty and at most 32 MiB: ' + str(source))
                payloads.append((source.read_bytes(), directory, name, source))
            self.ensure_directory('/sdmc:/kaihen', 'plugins')
            self.ensure_directory('/sdmc:', 'moader')
            for data, directory, name, source in payloads:
                self.install_bytes(data, directory, name, source)
            self.pairing_code()
            self.record['complete'] = True
            self.record['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
            self.save()
            print('Normal app installed. Plugin staged for a later kaiHEN bootstrap; no FS acquisition performed.')
        except BaseException as error:
            self.record['error'] = repr(error)
            self.save()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=str)
    parser.add_argument('port', type=int, nargs='?', default=1337)
    args = parser.parse_args()
    Installer(args.host, args.port).run()


if __name__ == '__main__':
    main()
