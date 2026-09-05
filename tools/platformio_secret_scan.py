"""PlatformIO pre-build adapter for the standalone secret scanner."""

Import("env")

from pathlib import Path
import sys

sys.path.insert(0, str(Path.cwd() / "tools"))

from secret_scan import main

if main() != 0:
    env.Exit(1)
