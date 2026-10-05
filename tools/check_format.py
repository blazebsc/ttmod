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
    """Resolve the diff base, or None if we should check the whole tree.

    A base that does not resolve (force-push, first push to a new branch,
    typo) used to yield an empty diff and a silent PASS - the gate could not
    fail, which is the one thing a gate must never do. Fall back to
    merge-base(HEAD, origin/master); if even that fails, check EVERYTHING.
    """
    if len(sys.argv) > 1:
        given = sys.argv[1].strip()
        if given and resolves(given):
            return given
        print(f"check_format: base {given!r} does not resolve, checking whole tree")
        return None
    r = sh("git", "merge-base", "HEAD", "origin/master")
    if r.returncode == 0 and r.stdout.strip():
        return r.stdout.strip()
    return None


def resolves(rev):
    return sh("git", "cat-file", "-e", f"{rev}^{{commit}}").returncode == 0


def line_count(path):
    try:
        with open(os.path.join(_ROOT, path), encoding="utf-8") as f:
            return sum(1 for _ in f)
    except OSError:
        return 0


def changed_hunks(base):
    """Yield (path, [(start, count)]) of added lines vs base.

    Untracked C++ files are yielded whole: new files must be fully clean
    (CI's diff would otherwise pass vacuously on exactly the files that
    need the check most). base=None (unresolvable) also checks the whole
    tracked tree - a gate that cannot fail is not a gate.
    """
    if base is None:
        r = sh("git", "ls-files", "--cached", "--exclude-standard")
        for line in r.stdout.splitlines():
            p = line.strip()
            if p.endswith(SUFFIXES) and not SKIP_RE.match(p):
                size = line_count(p)
                if size > 0:
                    yield p, [(1, size)]
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
        if p.endswith(SUFFIXES) and not SKIP_RE.match(p):
            size = line_count(p)
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
