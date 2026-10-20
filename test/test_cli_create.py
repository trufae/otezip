"""Exercise archive creation with the command from issue #15 on every platform."""

import pathlib
import subprocess
import sys
import tempfile
import zipfile


def run(executable, directory, *args):
    result = subprocess.run(
        [str(executable), *args], cwd=directory, capture_output=True, timeout=30
    )
    if result.returncode != 0:
        raise AssertionError(
            f"otezip {args} exited with {result.returncode:#x}\n"
            f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
        )
    return result.stdout


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="otezip-create-") as tmp:
        directory = pathlib.Path(tmp)
        # Include CR/LF, NUL, Ctrl-Z and high bytes to catch Windows text-mode I/O.
        payload = (b"test texture\r\n\x00\x1a" + bytes(range(256))) * 256
        (directory / "test.ctex").write_bytes(payload)
        for options, method in [(["-z", "deflate"], zipfile.ZIP_DEFLATED),
                                ([], zipfile.ZIP_DEFLATED),
                                (["-z", "store"], zipfile.ZIP_STORED)]:
            # Keep the option after the filename, exactly as in the issue.
            run(executable, directory, "-c", "test.zip", "test.ctex", *options)
            with zipfile.ZipFile(directory / "test.zip") as archive:
                assert archive.namelist() == ["test.ctex"]
                assert archive.getinfo("test.ctex").compress_type == method
                assert archive.read("test.ctex") == payload
            assert b"test.ctex" in run(executable, directory, "-l", "test.zip")
            output = directory / f"out-{method}-{len(options)}"
            output.mkdir()
            run(executable, output, "-x", "../test.zip", "--verify-crc")
            assert (output / "test.ctex").read_bytes() == payload
    print("PASS: CLI archive creation, listing and extraction")


if __name__ == "__main__":
    main()
