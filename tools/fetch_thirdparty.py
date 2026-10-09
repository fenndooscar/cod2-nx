"""Fetches the third-party sources of the SDK into sdk/thirdparty.

sdk/thirdparty only carries the files this port changed (see THIRD_PARTY_NOTICES.md). Everything else comes from the
ReXGlue SDK release this port is based on and its submodules. This script clones that release with its submodules
into a temporary folder and copies every file that sdk/thirdparty does not have yet; files already there are kept.

Usage: python tools/fetch_thirdparty.py
Needs git.
"""
import os
import shutil
import stat
import subprocess
import sys
import tempfile

UPSTREAM = 'https://github.com/rexglue/rexglue-sdk.git'
COMMIT = 'c94f5ebdcb3c9d1a460ca48e04f9758448f8d518'  # v0.10.0


def run(*args, cwd=None):
    print('>', ' '.join(args))
    subprocess.run(args, cwd=cwd, check=True)


def remove_readonly(func, path, _):
    os.chmod(path, stat.S_IWRITE)
    func(path)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = os.path.join(root, 'sdk', 'thirdparty')
    work = tempfile.mkdtemp(prefix='rexglue-sdk-')
    try:
        run('git', 'init', '-q', work)
        run('git', 'remote', 'add', 'origin', UPSTREAM, cwd=work)
        run('git', 'fetch', '-q', '--depth', '1', 'origin', COMMIT, cwd=work)
        run('git', 'checkout', '-q', 'FETCH_HEAD', cwd=work)
        run('git', 'submodule', 'update', '--init', '--recursive', cwd=work)
        source = os.path.join(work, 'thirdparty')
        copied = kept = 0
        for dirpath, dirnames, filenames in os.walk(source):
            dirnames[:] = [d for d in dirnames if d != '.git']
            for name in filenames:
                if name == '.git':
                    continue
                src = os.path.join(dirpath, name)
                dst = os.path.join(target, os.path.relpath(src, source))
                if os.path.exists(dst):
                    kept += 1
                    continue
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
                copied += 1

        # sdk/thirdparty/CMakeLists.txt refuses to configure unless every vendored library has a .git entry, the
        # way a real submodule checkout does. We copy the files without their history, so leave an empty marker in
        # each folder that was a submodule upstream. Without it, step 1 of docs/building.md breaks the build.
        markers = 0
        for name in os.listdir(source):
            if not os.path.exists(os.path.join(source, name, '.git')):
                continue
            marker = os.path.join(target, name, '.git')
            if not os.path.exists(marker):
                os.makedirs(marker, exist_ok=True)
                markers += 1

        # Git on Windows checks out symbolic links as small text files holding the path they point at
        # (libmspack/cabextract/mspack/* are links to ../libmspack/mspack/*). A C compiler then reads the path as
        # source code and fails, so replace every such file with a real copy of what it points at.
        links = 0
        for dirpath, dirnames, filenames in os.walk(target):
            dirnames[:] = [d for d in dirnames if d != '.git']
            for name in filenames:
                path = os.path.join(dirpath, name)
                try:
                    if not (0 < os.path.getsize(path) < 200):
                        continue
                    with open(path, 'r', encoding='utf-8', errors='strict') as f:
                        content = f.read()
                except (OSError, UnicodeDecodeError):
                    continue
                if '\n' in content or not content.startswith(('../', './')):
                    continue
                pointed = os.path.normpath(os.path.join(dirpath, content))
                if not os.path.isfile(pointed):
                    continue
                shutil.copyfile(pointed, path)
                links += 1

        print(f'{copied} files copied into sdk/thirdparty, {kept} files of this port kept, '
              f'{markers} submodule markers written, {links} links materialised')
    finally:
        if sys.version_info >= (3, 12):
            shutil.rmtree(work, onexc=remove_readonly)
        else:
            shutil.rmtree(work, onerror=remove_readonly)


if __name__ == '__main__':
    main()
