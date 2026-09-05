#!/usr/bin/env python3
"""Compile and run dependency-free host tests."""

from pathlib import Path
import os
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    compiler = os.environ.get("CXX", "c++")
    with tempfile.TemporaryDirectory(prefix="m5stopwatch-tests-") as temp_dir:
        binary = Path(temp_dir) / "test_marks"
        subprocess.run(
            [
                compiler,
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                f"-I{ROOT / 'src'}",
                str(ROOT / "tests" / "test_marks.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
            cwd=ROOT,
        )
        subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()
