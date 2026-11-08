#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the wallpaper integration tests in an isolated copy of the host plugin."""
import argparse
import contextlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from install import ROOT, PLUGIN_ID, stage_package, validate_plugin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    data = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    parser.add_argument("--plugin-root", type=Path, default=data / "plasma/wallpapers" / PLUGIN_ID)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--keep-fixture", action="store_true")
    # Some distributions keep the Qt 5 runner under the unversioned name.
    candidates = [shutil.which("qmltestrunner6"), "/usr/lib64/qt6/bin/qmltestrunner",
        "/usr/lib/qt6/bin/qmltestrunner", shutil.which("qmltestrunner")]
    default_runner = next((p for p in candidates if p and Path(p).is_file()), "qmltestrunner")
    parser.add_argument("--runner", default=default_runner)
    args = parser.parse_args()
    plugin = args.plugin_root.expanduser().resolve()
    updates = validate_plugin(plugin)
    module = args.build_dir.resolve() / "qml/libnexusinput.so"
    if not module.is_file():
        raise SystemExit("Build the input module first (see README)")
    artifacts = args.build_dir.resolve() / "qml-artifacts"
    artifacts.mkdir(parents=True, exist_ok=True)
    fixture = contextlib.nullcontext(tempfile.mkdtemp(prefix="fixture-", dir=artifacts)) if args.keep_fixture else tempfile.TemporaryDirectory(prefix="nexus-classic-tests-")
    with fixture as directory:
        temporary = Path(directory)
        data_home = temporary / "data"
        host = data_home / "plasma/wallpapers" / PLUGIN_ID
        shutil.copytree(plugin, host)
        for path, content in updates.items():
            (host / path.relative_to(plugin)).write_text(content)
        package = data_home / "shaderwallpaper/Shaders/Retro/NexusClassic"
        package.parent.mkdir(parents=True)
        stage_package(package, package, module)
        tests = temporary / "tests"
        tests.mkdir()
        replacements = {
            "@HOST_UI@": (host / "contents/ui").as_uri(),
            "@PACKAGE@": package.as_uri(),
            "@ARTIFACT_DIR@": str(artifacts),
            "@CACHE_TEST@": (temporary / "cached-shader.frag").as_uri(),
        }
        for source in (ROOT / "package/NexusClassic/interaction/tests").glob("*.qml.in"):
            content = source.read_text()
            for key, value in replacements.items():
                content = content.replace(key, value)
            (tests / source.name.removesuffix(".in")).write_text(content)
        environment = dict(os.environ)
        for name in ["DATA", "CACHE", "CONFIG", "STATE"]:
            location = data_home if name == "DATA" else temporary / name.lower()
            location.mkdir(parents=True, exist_ok=True)
            environment[f"XDG_{name}_HOME"] = str(location)
        environment.setdefault("QT_QUICK_BACKEND", "rhi")
        environment.setdefault("QSG_RHI_BACKEND", "opengl")
        environment.setdefault("QSG_RENDER_LOOP", "basic")
        result = subprocess.run([args.runner, "-input", str(tests), "-o", str(artifacts / "results.txt") + ",txt"], env=environment)
        print((artifacts / "results.txt").read_text())
        raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
