"""Run the Lua proofs on stock Lua 5.1 and 5.2, wherever those interpreters live.

Why this module exists: the proofs were once hardcoded to `nix-shell -p lua5_2`,
which existed on the author's machine and nowhere else, so every Lua test
failed on CI in 0.1s. Interpreters now come from the system:

  * system packages:  `lua5.1` / `lua5.2`      (Debian, Ubuntu, Arch, CI runners)

The proofs must run on BOTH versions: the game behaves as 5.1 (no setmetatable,
`_G` is nil) while stock 5.2 has both, so 5.2 alone cannot see those bugs. The
harness nils `_G` itself; see tests/test_menumods_ui.py.
"""
import os
import shutil
import subprocess
import tempfile

VERSIONS = ("5.1", "5.2")

_PROBED = {}


def _system_binary(version):
    """A system lua of exactly `version`, or None."""
    if version in _PROBED:
        return _PROBED[version]
    names = [
        f"lua{version}",                      # lua5.1  (Debian/Ubuntu/Arch)
        f"lua{version.replace('.', '')}",     # lua51
    ]
    found = None
    for name in names:
        path = shutil.which(name)
        if not path:
            continue
        try:
            out = subprocess.run([path, "-v"], capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.SubprocessError):
            continue
        if f"lua {version}" in (out.stdout + out.stderr).lower():
            found = [path]
            break
    _PROBED[version] = found
    return found


def _argv(version, script_path):
    """Build the argv that runs `script_path` under `version`."""
    sysbin = _system_binary(version)
    if sysbin:
        return sysbin + [script_path]
    raise RuntimeError(
        f"no Lua {version} found (looked for lua{version} on PATH). "
        f"Install it, e.g. `sudo pacman -S lua{version.replace('.', '')}` "
        f"or `sudo apt-get install lua{version}`."
    )


def run(version, source, timeout=300):
    """Run `source` as a Lua script under `version`. Returns CompletedProcess."""
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as f:
        f.write(source)
        path = f.name
    try:
        return subprocess.run(
            _argv(version, path), capture_output=True, text=True, timeout=timeout
        )
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def run_file(version, path, timeout=300):
    """Run an existing .lua file under `version`."""
    return subprocess.run(
        _argv(version, path), capture_output=True, text=True, timeout=timeout
    )


def share_path(name):
    """A writable path for small cross-test artefacts.

    CI runners have no /tmp/opencode (the author's sandbox path), so tests must
    never hardcode it. Honours TTMOD_TEST_TMP, else the system temp dir.
    """
    base = os.environ.get("TTMOD_TEST_TMP") or tempfile.gettempdir()
    os.makedirs(base, exist_ok=True)
    return os.path.join(base, name)
