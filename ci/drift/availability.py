# SPDX-License-Identifier: GPL-3.0-or-later
"""Fully download and verify every shipped acquisition input, without installing."""
import io
import json
import lzma
from pathlib import PurePosixPath
import re
import tempfile
import tomllib
from urllib.parse import quote

from common import ROOT, IntegrityError, arguments, download, envelope, fetch, host, profiles, result, summarize, verify_stream, write
from provenance import identity
import homebrew
import ubuntu
import winlibs
from pgp import signed_digest

GNU_URL = 'https://ftp.gnu.org/gnu/gcc/gcc-15.2.0/gcc-15.2.0.tar.gz'
RECIPE_URL = 'https://img.atcoder.jp/file/language-update/2025-10/017-23_gcc_14-2-0.toml'
MSYS = 'https://repo.msys2.org/mingw/mingw64/'


def plan(selected):
    tasks = []
    def add(profile, source, url, method, **options):
        tasks.append(dict(profile=profile, source=source, url=url, method=method, **options))
    for name in selected:
        profile = tomllib.loads((ROOT / 'data/judges' / (name + '.toml')).read_text())
        provenances = list(dict.fromkeys(identity(profile, key) for key in ('snapshot_compiler', 'build')))
        for compiler in provenances:
            if compiler == 'none':
                continue
            if compiler.startswith('package=msys2:'):
                from mingw import FILE, package_filename
                files = [l for l in (ROOT / 'ci/profiles/mingw-packages.txt').read_text().splitlines() if l and not l.startswith('#')]
                if len(set(files)) != len(files) or any(not FILE.fullmatch(f) for f in files) or package_filename(compiler) not in files:
                    raise ValueError('invalid exact MSYS2 closure lock')
                for file in files:
                    add(name, file, MSYS + file, 'msys2')
            elif compiler.startswith('package=ubuntu-'):
                release, major, revision = ubuntu.parse(compiler)
                codename, timestamp = ubuntu.snapshot(release)
                base = f'https://snapshot.ubuntu.com/ubuntu/{timestamp}/'
                for package in ubuntu.packages(major):
                    add(name, package + '=' + revision, base, 'ubuntu', codename=codename, package=package, revision=revision)
            elif compiler.startswith('image='):
                image = compiler.removeprefix('image=docker.io/')
                repository, digest = image.split('@')
                add(name, compiler, 'https://registry-1.docker.io/v2/' + repository, 'image', repository=repository, digest=digest)
            elif compiler.startswith('homebrew='):
                if compiler != homebrew.PROVENANCE:
                    raise ValueError('unsupported Homebrew pin')
                add(name, compiler, 'https://ghcr.io/v2/homebrew/core/gcc/blobs/sha256:' + homebrew.DIGEST, 'homebrew')
            elif compiler == 'recipe=' + RECIPE_URL:
                add(name, 'atcoder-recipe', RECIPE_URL, 'digest', digest='sha256:' + (ROOT / 'ci/profiles/atcoder-recipe.sha256').read_text().strip())
                add(name, 'gcc-15.2.0.tar.gz', GNU_URL, 'digest', digest='sha512:' + (ROOT / 'ci/profiles/atcoder-source.sha512').read_text().split()[0])
            elif compiler.startswith('winlibs='):
                if compiler != winlibs.PROVENANCE:
                    raise ValueError('unsupported WinLibs pin')
                add(name, compiler, winlibs.URL, 'winlibs')
            else:
                raise ValueError('unsupported acquisition: ' + compiler)
    # QOJ's compiler packages also rely on this digest-pinned acquisition base.
    # Include it even though it is not the compiler identity in the TOML.
    from matrix import matrix
    for row in matrix(','.join(selected))['include']:
        if row['acquisition'] == 'ubuntu' and 'container' in row:
            digest = row['container'].split('@')[1]
            add(row['profile'], 'container=' + row['container'],
                'https://registry-1.docker.io/v2/library/ubuntu', 'image', repository='library/ubuntu', digest=digest)
    return tasks


def expected(selected):
    return {(t['profile'], t['source']): (host(t['url']), 'availability') for t in plan(selected)}


def deb822(data):
    for paragraph in data.decode('utf-8').strip().split('\n\n'):
        fields = {}
        key = None
        for line in paragraph.splitlines():
            if line.startswith((' ', '\t')) and key:
                fields[key] += '\n' + line
            else:
                key, sep, value = line.partition(':')
                if not sep or key in fields:
                    raise ValueError('invalid or duplicate package index field')
                fields[key] = value.strip()
        yield fields


def relative(path):
    p = PurePosixPath(path)
    if not path or p.is_absolute() or '..' in p.parts or not re.fullmatch(r'[A-Za-z0-9+._/~-]+', path):
        raise ValueError('unsafe repository path')
    return path


def ubuntu_index(base, codename, cache):
    key = (base, codename)
    if key not in cache:
        records = []
        for suite in (codename, codename + '-updates', codename + '-security'):
            release, _ = fetch(base + 'dists/' + suite + '/Release')
            sections = list(deb822(release))
            hashes = sections[0]['SHA256'].splitlines()
            path = 'main/binary-amd64/Packages.xz'
            matches = [line.split() for line in hashes if line.split() and line.split()[-1] == path]
            if len(matches) != 1:
                raise ValueError('Release needs one SHA256 package index entry')
            digest, size, _ = matches[0]
            data, _ = fetch(base + 'dists/' + suite + '/' + path)
            verify_stream(io.BytesIO(data), 'sha256:' + digest, size=int(size))
            # Main indexes are small; bound decompression to avoid accepting a
            # metadata compression bomb. Packages themselves are streamed.
            unpacker = lzma.LZMADecompressor()
            unpacked = unpacker.decompress(data, max_length=128 * 1024 * 1024)
            if not unpacker.eof:
                raise ValueError('package index exceeds decompression budget')
            records.extend(deb822(unpacked))
        cache[key] = records
    return cache[key]


def ubuntu_package(task, cache):
    candidates = [p for p in ubuntu_index(task['url'], task['codename'], cache)
                  if p.get('Package') == task['package'] and p.get('Version') == task['revision'] and p.get('Architecture') in ('amd64', 'all')]
    unique = {(p['Filename'], p['SHA256'], p['Size']) for p in candidates}
    if len(unique) != 1:
        raise ValueError('exact Ubuntu package absent or ambiguous at recorded snapshot')
    path, digest, size = unique.pop()
    download(task['url'] + quote(relative(path), safe='/+~'), 'sha256:' + digest, size=int(size))


def image(task):
    """Registry HTTP API: digest-pinned index, unique amd64 child, config + all layers."""
    repository = task['repository']
    token_bytes, _ = fetch('https://auth.docker.io/token?service=registry.docker.io&scope=repository:' + repository + ':pull')
    headers = {'Authorization': 'Bearer ' + json.loads(token_bytes)['token'],
               'Accept': ', '.join(('application/vnd.oci.image.index.v1+json', 'application/vnd.docker.distribution.manifest.list.v2+json',
                                   'application/vnd.oci.image.manifest.v1+json', 'application/vnd.docker.distribution.manifest.v2+json'))}
    def manifest(digest, size=None):
        data, _ = fetch(task['url'] + '/manifests/' + digest, headers=headers)
        verify_stream(io.BytesIO(data), digest, size=size)
        parsed = json.loads(data)
        if parsed.get('schemaVersion') != 2:
            raise ValueError('unsupported registry schema')
        return parsed
    index = manifest(task['digest'])
    if 'manifests' not in index:
        raise ValueError('pinned image must identify a multi-platform index')
    children = [m for m in index['manifests'] if m.get('platform', {}).get('os') == 'linux' and m.get('platform', {}).get('architecture') == 'amd64']
    if len(children) != 1:
        raise ValueError('image needs exactly one linux/amd64 manifest')
    child = manifest(children[0]['digest'], children[0]['size'])
    for blob in [child['config'], *child['layers']]:
        digest = blob['digest']
        if not re.fullmatch(r'sha256:[0-9a-f]{64}', digest):
            raise ValueError('invalid registry blob digest')
        download(task['url'] + '/blobs/' + digest, digest, size=blob['size'], headers=headers)


def check(task, cache):
    method = task['method']
    if method == 'digest':
        download(task['url'], task['digest'])
    elif method == 'msys2':
        keyring = json.loads((ROOT / 'ci/drift/msys2-keys.json').read_text())
        keys = {fingerprint: (int(n), e) for fingerprint, (n, e) in keyring.items()}
        signature, _ = fetch(task['url'] + '.sig', limit=8192)
        digest, suffix = signed_digest(signature, keys)
        download(task['url'], digest, suffix=suffix)
    elif method == 'ubuntu':
        ubuntu_package(task, cache)
    elif method == 'image':
        image(task)
    elif method in ('homebrew', 'winlibs'):
        # Reuse the installer download implementation, but never install or unpack.
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            from pathlib import Path
            destination = Path(directory) / 'blob'
            if method == 'homebrew':
                homebrew.download(destination, homebrew.DIGEST)
            else:
                winlibs.download(destination)
    else:
        raise ValueError('unknown artifact method')


def run(selected):
    rows, cache = [], {}
    # Identical pins shared by profiles are downloaded once per complete run.
    checked = {}
    for task in plan(selected):
        key = json.dumps({k: v for k, v in task.items() if k != 'profile'}, sort_keys=True)
        if key not in checked:
            try:
                check(task, cache)
                checked[key] = ('verified', 'full download and digest verified')
            except (IntegrityError, homebrew.DigestError, winlibs.DigestError) as error:
                checked[key] = ('integrity alert', str(error))
            except Exception as error:
                checked[key] = ('unavailable', str(error))
        status, detail = checked[key]
        rows.append(result(task['profile'], task['source'], host(task['url']), status, detail))
    return envelope('availability', selected, rows)


def build_parser():
    return arguments(__doc__)


if __name__ == '__main__':
    args = build_parser().parse_args()
    data = run(profiles(args.profiles))
    write(args.output, data)
    summarize(data)
