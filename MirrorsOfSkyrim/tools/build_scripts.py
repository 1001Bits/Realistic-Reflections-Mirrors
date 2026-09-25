"""Compile the four add-on scripts with Caprica and Skyrim/SKSE/SkyUI imports."""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import struct
import tempfile

PEX_MAGIC = b"\xFA\x57\xC0\xDE"
PEX_HEADER_SIZE = 16
CANONICAL_SOURCE = b"MOSStandingMirrorItem.psc"
CANONICAL_USER = b"MirrorsOfSkyrim"
CANONICAL_MACHINE = b"reproducible-build"
SCRIPTS = ('MirrorsOfSkyrimMCM', 'MirrorsOfSkyrimNative', 'MOSStandingMirrorItem', 'MOSStandingMirrorNative')

def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def read_header_string(data: bytes, offset: int) -> tuple[bytes, int]:
    require(len(data) - offset >= 2, "truncated PEX header string length")
    length = struct.unpack_from(">H", data, offset)[0]
    offset += 2
    end = offset + length
    require(end <= len(data), "truncated PEX header string")
    return data[offset:end], end


def encode_header_string(value: bytes) -> bytes:
    require(len(value) <= 0xFFFF, "PEX header string is too long")
    return struct.pack(">H", len(value)) + value


def canonicalize_pex(
    data: bytes,
    canonical_source: bytes = CANONICAL_SOURCE,
    required_tokens: tuple[bytes, ...] = (
        b"MOSStandingMirrorItem",
        b"MOSStandingMirrorNative",
        b"OnEquipped",
    ),
) -> bytes:
    require(len(data) >= PEX_HEADER_SIZE, "PEX is shorter than its header")
    require(data[:4] == PEX_MAGIC, "unexpected PEX magic")
    require(data[4:8] == b"\x03\x02\x00\x01", "not a Skyrim PEX 3.2 header")

    offset = PEX_HEADER_SIZE
    for _ in range(3):
        _, offset = read_header_string(data, offset)

    canonical = b"".join(
        (
            data[:8],
            b"\0" * 8,
            encode_header_string(canonical_source),
            encode_header_string(CANONICAL_USER),
            encode_header_string(CANONICAL_MACHINE),
            data[offset:],
        )
    )
    folded = canonical.lower()
    for token in required_tokens:
        require(token.lower() in folded, f"compiled PEX is missing {token!r}")
    return canonical


def validate_native_placement_signature(data: bytes) -> None:
    """Check the emitted native declaration, including its VM parameter type.

    Caprica emits the declared native after its generated state helpers. Pin
    that complete final function record so a PEX with an old object parameter
    cannot pass merely because it contains the expected function-name string.
    """
    require(data[:8] == PEX_MAGIC + b"\x03\x02\x00\x01", "not a Skyrim PEX")
    offset = PEX_HEADER_SIZE
    for _ in range(3):
        _, offset = read_header_string(data, offset)
    require(len(data) - offset >= 2, "truncated PEX string-table count")
    count = struct.unpack_from(">H", data, offset)[0]
    offset += 2
    strings: list[bytes] = []
    for _ in range(count):
        value, offset = read_header_string(data, offset)
        strings.append(value.lower())

    def index(value: bytes) -> int:
        require(value in strings, f"native placement PEX is missing {value!r}")
        return strings.index(value)

    expected = struct.pack(
        ">HHHIBHHHHH",
        index(b"beginplacement"), index(b"bool"), index(b""),
        0, 3, 1,  # no user flags; global + native; one parameter
        index(b"aiinventoryitemformid"), index(b"int"),
        0, 0,  # no locals or bytecode in a native declaration
    )
    require(data.endswith(expected),
            "expected Bool BeginPlacement(Int) Global Native")


def compile_canonical(compiler: Path, source: Path, imports: list[Path], flags: Path,
                      script_name: str, required_tokens: tuple[bytes, ...]) -> bytes:
    with tempfile.TemporaryDirectory(prefix="rr-mcm-pex-") as directory:
        output_dir = Path(directory)
        command = [
            str(compiler), source.name, "-g", "skyrim",
            *[arg for directory in imports for arg in ("-i", str(directory))],
            "-i", str(source.parent),
            "-f", str(flags), "-o", str(output_dir),
            "--strict", "--all-warnings-as-errors", "--release", "--final",
            "--ignorecwd",
        ]
        import subprocess
        completed = subprocess.run(command, cwd=source.parent, check=False,
                                   text=True, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        print(completed.stdout, end="")
        require(completed.returncode == 0,
                     f"Caprica failed with {completed.returncode}")
        compiled = output_dir / f"{script_name}.pex"
        require(compiled.is_file(), f"Caprica did not produce {script_name}.pex")
        return canonicalize_pex(
            compiled.read_bytes(), source.name.encode("ascii"), required_tokens)

def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--imports', type=Path, action='append', required=True)
    parser.add_argument('--flags', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    compiler, flags = args.compiler.resolve(), args.flags.resolve()
    imports = [p.resolve() for p in args.imports]
    require(compiler.is_file() and flags.is_file() and all(p.is_dir() for p in imports), 'Compiler, flags and import directories must exist')
    outputs = {}
    for name in SCRIPTS:
        source = root / 'scripts/Source' / (name + '.psc')
        before = source.read_bytes()
        tokens = (name.encode('ascii'),)
        if name.endswith('Native'):
            tokens += tuple(re.findall(rb'(?im)^\w*\s*Function\s+(\w+)\([^\n]*Global Native', before))
        first = compile_canonical(compiler, source, imports, flags, name, tokens)
        second = compile_canonical(compiler, source, imports, flags, name, tokens)
        require(first == second and source.read_bytes() == before, name + ': reproducibility failure')
        if name == 'MOSStandingMirrorNative':
            validate_native_placement_signature(first)
        outputs[name + '.pex'] = first
    args.output.mkdir(parents=True, exist_ok=True)
    for name, raw in outputs.items():
        path = args.output / name
        path.write_bytes(raw)
        require(path.read_bytes() == raw, 'Readback mismatch: ' + name)
        print(str(path))


if __name__ == '__main__':
    main()
