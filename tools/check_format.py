#!/usr/bin/env python3
"""Fail iff lines changed vs BASE violate .clang-format.

Usage: python3 tools/check_format.py [base]
  base defaults to merge-base(HEAD, origin/master) so only the current
  work is checked; the pre-existing tree is grandfathered (it predates
  the style file - see .clang-format). Pass `HEAD~1` locally for the
  last commit, or a tag for releases.

Exits 1 listing offending file:line ranges; exits 0 silently otherwise.
Requires clang-format on PATH (CI installs it; also used by hand via
`clang-format -i <file>`).
"""
import os
import re
import subprocess
import sys

SUFFIXES = (".cpp", ".hpp", ".h", ".c")
# Any build*/ or release output, vendored code, packaging areas.
SKIP_RE = re.compile(r"^(build[^/]*/|release/|third_party/|preserve/)")
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sh(*args):
    return subprocess.run(args, capture_output=True, text=True, cwd=_ROOT)


def base_rev():
    if len(sys.argv) > 1:
        return sys.argv[1]
    r = sh("git", "merge-base", "HEAD", "origin/master")
    if r.returncode == 0 and r.stdout.strip():
        return r.stdout.strip()
    return None


def changed_hunks(base):
    """Yield (path, [(start, count)]) of added lines vs base.

    Untracked C++ files are yielded whole: new files must be fully clean
    (CI's diff would otherwise pass vacuously on exactly the files that
    need the check most).
    """
    if base is None:
        return
    diff = sh("git", "diff", "-U0", base, "HEAD").stdout.splitlines()
    path, hunks = None, []
    for line in diff:
        m = re.match(r"\+\+\+ b/(.*)", line)
        if m:
            if path is not None:
                yield path, hunks
            path, hunks = m.group(1), []
        m = re.match(r"@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", line)
        if m and path is not None:
            hunks.append((int(m.group(1)), int(m.group(2) or 1)))
    if path is not None:
        yield path, hunks
    # Untracked C++ sources: check whole file (line 1 to end).
    r = sh("git", "ls-files", "--others", "--exclude-standard")
    for line in r.stdout.splitlines():
        p = line.strip()
        if not p.endswith(SUFFIXES):
            continue
        if SKIP_RE.match(p):
            continue
        size = 0
        try:
            with open(os.path.join(_ROOT, p), encoding="utf-8") as f:
                size = sum(1 for _ in f)
        except OSError:
            continue
        if size > 0:
            yield p, [(1, size)]


def main():
    base = base_rev()
    failures = 0
    checked = 0
    for path, hunks in changed_hunks(base):
        if not path.endswith(SUFFIXES):
            continue
        if SKIP_RE.match(path):
            continue
        ranges = [(s, s + c - 1) for s, c in hunks if c > 0]
        if not ranges:
            continue
        args = ["clang-format", "--dry-run", "--Werror"]
        for s, e in ranges:
            args.append(f"--lines={s}:{e}")
        args.append(path)
        r = subprocess.run(args, capture_output=True, text=True, cwd=_ROOT)
        if r.returncode != 0:
            failures += 1
            print(f"--- {path} ---")
            print(r.stderr.strip())
        checked += 1
    print(f"check_format: {checked} file(s) checked vs {base or 'nothing'}")
    sys.exit(1 if failures else 0)


main()
