"""Exercise the shipping guard DLL in isolated Windows hosts, never Skyrim."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import sys
import tempfile


def main() -> None:
    host, dll, work = map(Path, sys.argv[1:])
    work.mkdir(parents=True, exist_ok=True)
    cases = ('present', 'missing', 'master-directory', 'orphan',
             'chain-first', 'chain-middle', 'chain-last', 'chain-only', 'fail-import')
    with tempfile.TemporaryDirectory(prefix='guard-', dir=work) as tmp:
        for case in cases:
            root = Path(tmp) / case
            data = root / 'Data'
            data.mkdir(parents=True)
            exe = root / host.name
            shutil.copy2(host, exe)
            for name in ('A.esp', 'Z.esp', 'plugins.txt'):
                (data / name).write_bytes(b'# fixture\r\n*MirrorsOfSkyrim.esp\r\n')
            if case != 'orphan':
                (data / 'MirrorsOfSkyrim.esp').write_bytes(b'TES4 fixture unchanged')
            if case == 'present':
                (data / 'RealisticReflectionsMirrors.esm').write_bytes(b'TES4 present')
            elif case == 'master-directory':
                (data / 'RealisticReflectionsMirrors.esm').mkdir()
            before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in data.iterdir() if p.is_file()}
            result = subprocess.run([str(exe.resolve()), str(dll.resolve()), case],
                                    cwd=work, capture_output=True, text=True, timeout=15)
            print(f'{case}: exit={result.returncode} {result.stdout.strip()} {result.stderr.strip()}')
            expected = 126 if case == 'fail-import' else 0
            assert result.returncode == expected, result
            assert ('CLOSE_STARTUP' if case == 'fail-import' else 'PASS') in result.stdout
            after = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in data.iterdir() if p.is_file()}
            assert after == before, 'guard changed installed files or load order'
    print(f'{len(cases)}/{len(cases)} actual-DLL scenarios passed')


if __name__ == '__main__':
    main()
