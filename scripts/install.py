#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build and install Nexus Classic for the current user, with rollback backups."""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "integration"))
from hooks import patched_files  # noqa: E402

PLUGIN_ID = "online.knowmad.shaderwallpaper"
SUPPORTED_VERSION = "4.2.4"
RELATIVE_PACKAGE = Path("shaderwallpaper/Shaders/Retro/NexusClassic")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def atomic_write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    mode = path.stat().st_mode & 0o777 if path.exists() else 0o644
    fd, temporary = tempfile.mkstemp(prefix=".nexus-", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as stream:
            stream.write(text)
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def validate_plugin(plugin):
    metadata = json.loads((plugin / "metadata.json").read_text())["KPlugin"]
    if metadata["Id"] != PLUGIN_ID or metadata["Version"] != SUPPORTED_VERSION:
        raise ValueError(f"This installer supports Shader Wallpaper {SUPPORTED_VERSION}; found {metadata}")
    if not os.access(plugin, os.W_OK):
        raise ValueError("Shader Wallpaper must be installed in a writable user directory")
    return patched_files(plugin)


def build_module(build_dir):
    subprocess.run(["cmake", "-S", str(ROOT / "package/NexusClassic/interaction"),
        "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF"], check=True)
    subprocess.run(["cmake", "--build", str(build_dir), "--parallel", "4"], check=True)
    module = build_dir / "qml/libnexusinput.so"
    if not module.is_file():
        raise ValueError("The Nexus input module was not produced")
    return module


def stage_package(destination, final_destination, module):
    shutil.copytree(ROOT / "package/NexusClassic", destination,
        ignore=shutil.ignore_patterns("*.so", "__pycache__", "*.pyc"))
    shutil.copy2(module, destination / "interaction/libnexusinput.so")
    path = destination / "manifest.json"
    manifest = json.loads(path.read_text())
    for channel, name in enumerate(["pyramid_background.png", "pulse.png", "glow.png"]):
        manifest["channels"][f"iChannel{channel}"] = (final_destination / "assets" / name).as_uri()
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")


def package_files(package):
    return sorted(p for p in package.rglob("*") if p.is_file()
        and "__pycache__" not in p.parts and p.suffix != ".pyc")


def verify_managed(record):
    for name, expected in record["files"].items():
        path = Path(name)
        if not path.is_file() or digest(path) != expected:
            raise ValueError(f"Installed file changed: {path}. Preserve or merge it before updating/uninstalling.")
    extra = set(map(str, package_files(Path(record["target"])))) - set(record["files"])
    if extra:
        raise ValueError(f"Additional files exist in the installed package: {sorted(extra)}")


def install(plugin, data_home, state_home, module, dry_run=False):
    updates = validate_plugin(plugin)
    target = data_home / RELATIVE_PACKAGE
    state = state_home / "nexus-classic-plasma"
    record_path = state / "install.json"
    previous = json.loads(record_path.read_text()) if record_path.exists() else None
    if previous:
        if Path(previous["target"]) != target or Path(previous["plugin"]) != plugin:
            raise ValueError("This installation record belongs to a different package or plugin directory")
        verify_managed(previous)
    if dry_run:
        print(f"Would install {target} and update two user-local Shader Wallpaper files")
        return
    if not module or not module.is_file():
        raise ValueError("A built Nexus input module is required")
    target.parent.mkdir(parents=True, exist_ok=True)
    original = {path: path.read_text() for path in updates}
    if previous:
        backup = Path(previous["backup"])
        had_package = previous["had_package"]
    else:
        backup = state / "backups" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
        backup.mkdir(parents=True)
        had_package = target.exists()
        for path in updates:
            shutil.copy2(path, backup / path.name)
        if had_package:
            shutil.copytree(target, backup / "NexusClassic")
    with tempfile.TemporaryDirectory(prefix=".nexus-stage-", dir=target.parent) as directory:
        temporary = Path(directory)
        staged, old = temporary / "NexusClassic", temporary / "previous"
        stage_package(staged, target, module)
        if target.exists():
            target.rename(old)
        try:
            staged.rename(target)
            for path, text in updates.items():
                atomic_write(path, text)
            managed = package_files(target) + list(updates)
            record = {"target": str(target), "plugin": str(plugin), "backup": str(backup),
                "had_package": had_package, "files": {str(p): digest(p) for p in managed}}
            atomic_write(record_path, json.dumps(record, indent=2) + "\n")
        except Exception:
            if target.exists():
                shutil.rmtree(target)
            if old.exists():
                old.rename(target)
            for path, text in original.items():
                atomic_write(path, text)
            raise
    print(f"Installed: {target}\nBackup: {backup}")
    print("Log out and back in once to load the integration. Then choose Nexus Classic in Retro.")
    print("For one continuous canvas, enable Advanced > Experimental > Span all screens on both monitors.")


def uninstall(state_home, dry_run=False):
    record_path = state_home / "nexus-classic-plasma/install.json"
    record = json.loads(record_path.read_text())
    verify_managed(record)
    if dry_run:
        print(f"Would restore the backup at {record['backup']}")
        return
    backup, target, plugin = Path(record["backup"]), Path(record["target"]), Path(record["plugin"])
    originals = {plugin / "contents/ui" / name: (backup / name).read_text()
        for name in ["ShaderSystem.qml", "ShaderApply.js"]}
    # Validate the complete backup before modifying the installation.
    if record["had_package"] and not (backup / "NexusClassic").is_dir():
        raise ValueError("The original Nexus package backup is missing")
    shutil.rmtree(target)
    if record["had_package"]:
        shutil.copytree(backup / "NexusClassic", target)
    for path, text in originals.items():
        atomic_write(path, text)
    record_path.unlink()
    print(f"Restored the previous installation. Backup retained: {backup}")
    print("Log out and back in to unload the helper from Plasma.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uninstall", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--data-home", type=Path, default=Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")))
    parser.add_argument("--cache-home", type=Path, default=Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")))
    parser.add_argument("--state-home", type=Path, default=Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")))
    parser.add_argument("--plugin-root", type=Path)
    args = parser.parse_args()
    data, state = args.data_home.expanduser().resolve(), args.state_home.expanduser().resolve()
    plugin = (args.plugin_root or data / "plasma/wallpapers" / PLUGIN_ID).expanduser().resolve()
    if args.uninstall:
        uninstall(state, args.dry_run)
        return
    validate_plugin(plugin)  # Fail before building or writing files.
    module = None if args.dry_run else build_module(args.cache_home.expanduser().resolve() / "nexus-classic-plasma/build")
    install(plugin, data, state, module, args.dry_run)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
