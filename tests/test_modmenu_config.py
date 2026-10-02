#!/usr/bin/env python3
"""Validate modmenu-core + demo-config example files (JSON shape only)."""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return json.load(f)


fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)


mm = load("examples/modmenu-core/manifest.json")
check(mm["id"] == "modmenu.core", "modmenu id")
check(mm["plugin"] == "plugins/modmenu.dll", "modmenu plugin path")
check(mm.get("arch") == "x86", "modmenu arch")
check("minecraft-story-mode:s1" in mm["games"], "modmenu games")

dc = load("examples/demo-config/manifest.json")
check(dc["id"] == "demo.config", "demo id")
check(dc["plugin"] == "plugins/demo.dll", "demo plugin path")
check(dc.get("arch") == "x86", "demo arch")

cfg = load("examples/demo-config/config-example.json")
check(isinstance(cfg["greeting"], str), "greeting is string")
check(isinstance(cfg["level"], int), "level is int")
check(isinstance(cfg["fancy"], bool), "fancy is bool")

for src in ("examples/modmenu-core/plugin.cpp", "examples/demo-config/plugin.cpp"):
    with open(os.path.join(ROOT, src), encoding="utf-8") as f:
        body = f.read()
    check("ttmod_plugin_init" in body, src + " exports init")
    check("host->log" in body, src + " logs via host")

if fails:
    print("FAIL:", *fails, sep="\n  ")
    sys.exit(1)
print("modmenu-config: all checks passed")
