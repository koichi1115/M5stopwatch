#!/usr/bin/env python3
"""Fail when tracked source contains common committed-secret signatures."""

from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SELF = Path(__file__).resolve()
TOKEN_PATTERNS = (
    ("private key", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----")),
    ("GitHub token", re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})\b")),
    ("AWS access key", re.compile(r"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b")),
    ("Google API key", re.compile(r"\bAIza[0-9A-Za-z_-]{35}\b")),
    ("OpenAI key", re.compile(r"\bsk-(?:proj-)?[A-Za-z0-9_-]{20,}\b")),
    ("Slack token", re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{20,}\b")),
    ("Stripe live key", re.compile(r"\b[rs]k_live_[A-Za-z0-9]{16,}\b")),
    ("Bot token", re.compile(r"\b\d{8,12}:[A-Za-z0-9_-]{30,}\b")),
)
ASSIGNMENT = re.compile(
    r"""(?ix)
    \b(?:api[_-]?key|access[_-]?token|auth[_-]?token|bot[_-]?token|
        client[_-]?secret|private[_-]?key)\b
    \s*(?:=|:)\s*
    ["']([^"']{8,})["']
    """
)
PLACEHOLDER = re.compile(r"(?i)^(?:your_|example|placeholder|changeme|not[_-]?set)")


def tracked_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return [
        ROOT / entry.decode("utf-8")
        for entry in result.stdout.split(b"\0")
        if entry
    ]


def main() -> int:
    findings: list[str] = []
    for path in tracked_files():
        if path.resolve() == SELF or not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue

        for line_number, line in enumerate(text.splitlines(), start=1):
            for label, pattern in TOKEN_PATTERNS:
                if pattern.search(line):
                    findings.append(f"{path.relative_to(ROOT)}:{line_number}: {label}")
            assignment = ASSIGNMENT.search(line)
            if assignment and not PLACEHOLDER.match(assignment.group(1)):
                findings.append(
                    f"{path.relative_to(ROOT)}:{line_number}: credential-like assignment"
                )

    if findings:
        print("Secret scan failed:", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        return 1

    print(f"Secret scan passed: checked {len(tracked_files())} tracked files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
