#!/usr/bin/env python3
"""Stage a pinned, allowlisted SOURCE preview locally. Never installs, flashes or publishes."""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import tarfile

MANIFEST = 'release/source-files.json'
RESERVED = {MANIFEST, 'release/preview-status.json', 'release/inventory.json'}
LICENSES = {'MIT', 'Apache-2.0', 'BSD-3-Clause', 'mixed-notices'}
EXTENSIONS = {'.mjs', '.ts', '.py', '.sh', '.h', '.c', '.cpp', '.md', '.json', '.css', '.html', '.csv', '.txt', '.lock', '.defaults', '.overlay', '.plist', '.swift', '.cmake', '.yml', '.inc', '.svg'}


def relative_path(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_./-]+', value):
        raise ValueError('Invalid source path')
    p = PurePosixPath(value)
    if p.is_absolute() or str(p) != value or any(x in ('', '.', '..') for x in value.split('/')):
        raise ValueError('Noncanonical source path')
    if p.suffix not in EXTENSIONS and p.name not in {'LICENSE', 'COPYING'}:
        raise ValueError('Unsupported source file type')
    return p


def read_source(root, relative):
    """Open every component without following symlinks; never walk outside root."""
    p = relative_path(relative)
    directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    fd = None
    try:
        for part in p.parts[:-1]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory)
            os.close(directory)
            directory = child
        fd = os.open(p.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or before.st_uid != os.getuid() or before.st_size > 4*1024*1024:
            raise ValueError('Unsafe source file')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(4*1024*1024+1)
        after = os.fstat(fd)
        if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns) or len(data) != before.st_size:
            raise ValueError('Source changed during read')
        return data
    finally:
        if fd is not None:
            os.close(fd)
        os.close(directory)


def privacy_check(data):
    text = data.decode('utf-8')  # Binary artifacts are not admitted.
    # Heuristics supplement (never replace) independent release review. Do not
    # print matching text: a failed check may have found an actual secret.
    patterns = [r'/(?:Users|home)/[A-Za-z0-9_.-]+/', r'/(?:private/)?var/folders/', r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',
                r'\b(?:ghp_|github_pat_|xox[baprs]-)[A-Za-z0-9_-]{16,}', r'next-features-[a-z0-9_]+',
                r'(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b']
    if '\0' in text or any(re.search(pattern, text) for pattern in patterns):
        raise ValueError('Source privacy check failed (matching content withheld)')


def stage(root, destination):
    root = Path(root).absolute()
    destination = Path(destination).absolute()
    if destination.resolve().is_relative_to(root.resolve()):
        raise ValueError('Destination must be outside source tree')
    manifest_bytes = read_source(root, MANIFEST)
    manifest = json.loads(manifest_bytes)
    if set(manifest) != {'version', 'publicationApproved', 'files'} or manifest['publicationApproved'] is not False:
        raise ValueError('Only unpublished candidate manifests are accepted')
    version = manifest['version']
    if not isinstance(version, str) or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+-dev\.[0-9]+', version):
        raise ValueError('Developer-preview version required')
    entries = manifest['files']
    if not isinstance(entries, list) or not 1 <= len(entries) <= 512:
        raise ValueError('Invalid source inventory')
    content = {}; seen = set(); total = 0
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {'path', 'sha256', 'license'} or entry['license'] not in LICENSES:
            raise ValueError('Missing source provenance classification')
        name = str(relative_path(entry['path']))
        if name.casefold() in {p.casefold() for p in RESERVED} or name.casefold() in seen:
            raise ValueError('Duplicate or reserved source path')
        seen.add(name.casefold())
        data = read_source(root, name)
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('Source pin mismatch: '+name)
        privacy_check(data)
        total += len(data)
        if total > 32*1024*1024:
            raise ValueError('Source inventory exceeds bound')
        content[name] = data
    if not {'LICENSE', 'THIRD_PARTY_NOTICES.md', 'README.md'} <= content.keys():
        raise ValueError('Required release notices missing')
    if 'package.json' in content:
        package = json.loads(content['package.json'])
        if not isinstance(package, dict) or package.get('version') != version:
            raise ValueError('Package version does not match source manifest')
    if 'release/tests.json' in content:
        suites = json.loads(content['release/tests.json'])
        if not isinstance(suites, dict) or set(suites) != {'node', 'python'} or any(not isinstance(paths, list) or any(not isinstance(p, str) or p not in content for p in paths) for paths in suites.values()):
            raise ValueError('Test inventory is not closed over staged source')
    privacy_check(manifest_bytes)
    content[MANIFEST] = manifest_bytes
    content['release/preview-status.json'] = (json.dumps({'version': version, 'publicationApproved': False, 'firmwareBinaries': False, 'publicInstaller': False, 'hardwareQualified': False}, indent=2)+'\n').encode()
    inventory = [{'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'mode': '0644'} for name, data in sorted(content.items())]
    content['release/inventory.json'] = (json.dumps(inventory, indent=2)+'\n').encode()
    destination.mkdir(mode=0o700)  # Never overwrite a prior candidate.
    source = destination/'source'
    source.mkdir(mode=0o700)
    for name, data in sorted(content.items()):
        target = source/name
        target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        with target.open('xb') as out:
            out.write(data)
        target.chmod(0o644)
        if target.read_bytes() != data:
            raise ValueError('Staged source mismatch')
    observed = {str(p.relative_to(source)) for p in source.rglob('*') if p.is_file()}
    if observed != set(content):
        raise ValueError('Unexpected staged entries')
    # Archive the already validated bytes, not a recursive walk of the checkout.
    archive = destination/f'pi-watcher-pet-{version}-source.tar.gz'
    with archive.open('xb') as output, gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0) as compressed, tarfile.open(fileobj=compressed, mode='w|') as tar:
        for name, data in sorted(content.items()):
            info = tarfile.TarInfo(f'pi-watcher-pet-{version}/'+name)
            info.size = len(data); info.mode = 0o644; info.mtime = 0
            tar.addfile(info, io.BytesIO(data))
    archive.chmod(0o600)
    result = {'version': version, 'files': len(content), 'archive': archive.name, 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(), 'publicationApproved': False}
    (destination/'receipt.json').write_text(json.dumps(result, indent=2)+'\n')
    (destination/'receipt.json').chmod(0o600)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(stage(args.root, args.output)))
    except Exception as error:
        # Paths/content in arbitrary exceptions may be private. Report type only.
        print('Source staging failed: '+type(error).__name__+'. No publication attempted.')
        raise SystemExit(1)
