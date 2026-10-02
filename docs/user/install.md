# Install TTMod (normal users)

## 1. Requirements
- A legitimate Minecraft: Story Mode Season 1 PC install (Steam-era build).
  Only the builds in `docs/games/compatibility.md` are tested - other builds
  safely idle with a log message instead of loading mods.
- Windows (Wine 8+ works on Linux; native Windows untested by this team yet -
  please report results).

## 2. Install the framework (once)
Copy these two files next to `MinecraftStoryMode.exe`:

```text
dinput8.dll
ttmod_framework.dll
```

That is the whole installation. Nothing is patched; remove the two DLLs to
uninstall the framework itself.

## 3. First launch
Launch the game normally. TTMod creates `mods/`, `config/`, `logs/` and the
internal `ttmod/` directory, writes `logs/ttmod.log`, and the game starts as
usual. Check the first lines of the log:

```text
TTMod framework v0.11.0 starting
Profile: mcsm1_pc_x86 status=supported
[TTMod] Valid mods: 0 ...
```

## 4. Install a mod
Drop a `.ttmod` file (or an unpacked mod folder with `manifest.json`) into
`mods/` and launch the game. No commands needed.

## 5. Uninstall everything
1. Delete `dinput8.dll` + `ttmod_framework.dll` (framework gone).
2. Optionally delete `mods/`, `config/`, `logs/`, `ttmod/` (your mods/state).
3. Verify the game directory contains only original files again.
4. The game was never patched: archives/exe are byte-identical (check MD5 if
   you like).
