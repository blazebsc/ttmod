#!/bin/sh
# Build the native Mods-button test mod LOCALLY (output contains game-derived
# bytes: never commit it, never distribute it).
# Requires: dotnet SDK, TelltaleToolKit checkout (MIT) WITH the documented
# 1-line writer fix (name-stream byte length), legitimate MCSM1 installation.
# Usage: TOOLKIT_DIR=/path/to/TelltaleToolKit TTK_DATA=/path/to/ttk/data \
#        GAME_DIR="/path/to/Minecraft - Story Mode" \
#        sh tools/build_menu_button.sh <outdir>
# Output: <outdir>/ttmod-menu-test/{manifest.json,files/archives/MCSM_pc_Menu_data.ttarch2}
set -eu
OUT="${1:?usage: build_menu_button.sh <outdir>}"
: "${TOOLKIT_DIR:?set TOOLKIT_DIR}"
: "${GAME_DIR:?set GAME_DIR}"
: "${TTK_DATA:=$TOOLKIT_DIR/data}"
T="$(dirname "$0")/.."
ARCH="$GAME_DIR/archives/MCSM_pc_Menu_data.ttarch2"
WORK=/tmp/opencode/modbtn_build
rm -rf "$WORK"
mkdir -p "$WORK/src" "$OUT/ttmod-menu-test/files/archives"
cat > "$WORK/src/tool.csproj" <<EOF
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework><Nullable>disable</Nullable></PropertyGroup>
  <ItemGroup><ProjectReference Include="$TOOLKIT_DIR/src/TelltaleToolKit/TelltaleToolKit.csproj" /></ItemGroup>
</Project>
EOF
cat > "$WORK/src/Program.cs" <<'EOF'
using TelltaleToolKit;
using TelltaleToolKit.IO.Archives;
using TelltaleToolKit.IO.Archives.Formats;
Toolkit.Initialize(new Toolkit.Configuration { DataFolder = System.Environment.GetEnvironmentVariable("TTK_DATA") });
var ws = Toolkit.Instance.CreateWorkspace("MCSM", gameProfile: "Minecraft: Story Mode");
string cmd = args[0];
if (cmd == "raw") {
    var ctx0 = ws.LoadArchive(args[1], "m0", 1000);
    using var s0 = ctx0.ExtractFile(args[2]);
    using var ms0 = new System.IO.MemoryStream(); s0.CopyTo(ms0);
    System.IO.File.WriteAllBytes(args[3], ms0.ToArray());
    System.Console.WriteLine($"raw ok");
} else if (cmd == "dec") {
    var ctx = ws.LoadArchive(args[1], "m", 1000);
    using var s = ctx.ExtractFile(args[2]);
    using var ms = new System.IO.MemoryStream(); s.CopyTo(ms);
    var all = ms.ToArray();
    var body = new byte[all.Length - 4];
    System.Array.Copy(all, 4, body, 0, body.Length);
    new TelltaleToolKit.Encryption.Blowfish(ws.Profile.BlowfishKey, 7).Decipher(body, body.Length);
    var dec = new byte[body.Length + 4];
    System.Text.Encoding.ASCII.GetBytes("\x1bLua").CopyTo(dec, 0);
    System.Array.Copy(body, 0, dec, 4, body.Length);
    System.IO.File.WriteAllBytes(args[3], dec);
    System.Console.WriteLine($"dec ok {dec.Length}");
} else if (cmd == "enc") {
    var dec = System.IO.File.ReadAllBytes(args[1]);
    var body = new byte[dec.Length - 4];
    System.Array.Copy(dec, 4, body, 0, body.Length);
    new TelltaleToolKit.Encryption.Blowfish(ws.Profile.BlowfishKey, 7).Encipher(body, body.Length);
    var enc = new byte[body.Length + 4];
    System.Text.Encoding.ASCII.GetBytes("\x1bLEn").CopyTo(enc, 0);
    System.Array.Copy(body, 0, enc, 4, body.Length);
    System.IO.File.WriteAllBytes(args[2], enc);
    System.Console.WriteLine($"enc ok {enc.Length}");
} else if (cmd == "extractall") {
    var archX = ws.LoadArchive(args[1]);
    archX.ExtractAll(args[2]);
    System.Console.WriteLine($"extracted to {args[2]}");
} else if (cmd == "rebuild") {
    var arch = ws.LoadArchive(args[1]);
    var opt = new ArchiveWriteOptions { TTArchiveVersion = arch.Info.Version, Compression = TelltaleToolKit.IO.Compression.Mode.Deflate, ChunkSize = arch.Info.ChunkSize, BlowfishKey = ws.Profile.BlowfishKey };
    Archive.CreateFromFolder<TTArchive2>(args[2], args[3], opt);
    var ws2 = Toolkit.Instance.CreateWorkspace("M2", gameProfile: "Minecraft: Story Mode");
    var a2 = ws2.LoadArchive(args[3]);
    int n = 0; foreach (var e in a2.GetAllEntries()) n++;
    System.Console.WriteLine($"rebuilt entries={n}");
}
EOF
export TTK_DATA
dotnet build "$WORK/src/tool.csproj" -v q --nologo > "$WORK/build.log" 2>&1 || { tail -n 5 "$WORK/build.log"; exit 1; }
TOOL="dotnet $WORK/src/bin/Debug/net8.0/tool.dll"
$TOOL dec "$ARCH" Menu_Main.lua "$WORK/menu.dec.lua"
python3 "$T/tools/apply_mods_button.py" "$WORK/menu.dec.lua" "$WORK/menu.mods.dec.lua"
$TOOL enc "$WORK/menu.mods.dec.lua" "$WORK/Menu_Main.lua.enc"
# sanity: unmodified round-trip must be byte-identical to the original entry
$TOOL raw "$ARCH" Menu_Main.lua "$WORK/menu.raw.lua"
$TOOL dec "$ARCH" Menu_Main.lua "$WORK/menu.dec2.lua"
$TOOL enc "$WORK/menu.dec2.lua" "$WORK/roundtrip.lua.enc"
cmp "$WORK/roundtrip.lua.enc" "$WORK/menu.raw.lua" || { echo "ROUNDTRIP FAIL"; exit 1; }
echo "roundtrip: byte-identical"
mkdir -p "$WORK/extracted"
$TOOL extractall "$ARCH" "$WORK/extracted"
cp "$WORK/Menu_Main.lua.enc" "$WORK/extracted/Menu_Main.lua"
$TOOL rebuild "$ARCH" "$WORK/extracted" "$WORK/Menu_data.mods.ttarch2"
# verify: reopen + extract modified entry + compare
$TOOL dec "$WORK/Menu_data.mods.ttarch2" Menu_Main.lua "$WORK/verify.dec.lua"
cmp "$WORK/verify.dec.lua" "$WORK/menu.mods.dec.lua" || { echo "VERIFY FAIL"; exit 1; }
echo "verify: rebuilt archive contains exact modified script"
cp "$WORK/Menu_data.mods.ttarch2" "$OUT/ttmod-menu-test/files/archives/MCSM_pc_Menu_data.ttarch2"
cat > "$OUT/ttmod-menu-test/manifest.json" <<'EOF2'
{
    "id": "ttmod.menu.test",
    "name": "TTMod menu button prototype (local test)",
    "version": "0.1.0",
    "api": 1,
    "games": ["minecraft-story-mode:s1"],
    "files": {
        "archives/MCSM_pc_Menu_data.ttarch2": "files/archives/MCSM_pc_Menu_data.ttarch2"
    }
}
EOF2
echo "mod built at $OUT/ttmod-menu-test"
