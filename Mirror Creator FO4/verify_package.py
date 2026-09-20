"""Verify the published Creators archive, extracted payload and recorded hashes.

Reads files only. It does not run the Creator, CK helper or any executable.
"""
from pathlib import Path, PurePosixPath
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parent
ARCHIVE = 'Realistic Reflections - Mirrors 1.0 - Creators.zip'
SHA256 = '557990f4d8d1b911a066f3b696018af6eead96d3b8821278f29359ceed85dd1d'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check(condition, message):
    if not condition:
        raise ValueError(message)


def verify():
    check(digest((ROOT/ARCHIVE).read_bytes()) == SHA256, 'Archive SHA256 mismatch')
    with zipfile.ZipFile(ROOT/ARCHIVE) as archive:
        check(archive.testzip() is None, 'Archive CRC check failed')
        check(len(archive.namelist()) == 129, 'Unexpected archive file count')
        for entry in archive.infolist():
            name = PurePosixPath(entry.filename)
            check(not name.is_absolute() and '..' not in name.parts and
                  '\\' not in entry.filename and ':' not in entry.filename,
                  'Invalid archive path')
            check((ROOT/name).read_bytes() == archive.read(entry),
                  'Extracted file differs: '+entry.filename)
    for prefix in ('', 'Mirror Creator/'):
        manifest = json.loads((ROOT/prefix/'package.json').read_text(encoding='utf-8'))
        for entry in manifest['files']:
            data = (ROOT/prefix/entry['path']).read_bytes()
            check(len(data) == entry['bytes'] and digest(data) == entry['sha256'],
                  'Manifest mismatch: '+prefix+entry['path'])
    for line in (ROOT/'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines():
        expected, name = line.split(' *', 1)
        check(digest((ROOT/name).read_bytes()) == expected, 'Checksum mismatch: '+name)
    print('PASS: all 129 extracted payload files exactly match the Creators ZIP.')
    print('PASS: archive SHA256, CRCs, both package manifests and SHA256SUMS.txt.')


if __name__ == '__main__':
    verify()
