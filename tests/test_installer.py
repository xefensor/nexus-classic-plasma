# SPDX-License-Identifier: Apache-2.0
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import install
from hooks import ANCHOR, LEGACY_LOADER, MOUSE_BEFORE, patch_shader_system, patch_shader_apply


class InstallerTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.data, self.state = self.root / "data", self.root / "state"
        self.plugin = self.data / "plasma/wallpapers" / install.PLUGIN_ID
        self.ui = self.plugin / "contents/ui"
        self.ui.mkdir(parents=True)
        (self.plugin / "metadata.json").write_text(json.dumps({"KPlugin": {"Id": install.PLUGIN_ID, "Version": "4.2.4"}}))
        self.original_system = "Item {\n" + ANCHOR + "}\n"
        self.original_apply = "function detectInputs(m, allCode) {\n" + MOUSE_BEFORE + "}\n"
        (self.ui / "ShaderSystem.qml").write_text(self.original_system)
        (self.ui / "ShaderApply.js").write_text(self.original_apply)
        self.module = self.root / "libnexusinput.so"
        self.module.write_bytes(b"module fixture; the real module is built separately")
        self.target = self.data / install.RELATIVE_PACKAGE

    def test_install_update_and_uninstall_restore_previous_package(self):
        self.target.mkdir(parents=True)
        (self.target / "personal-change.txt").write_text("keep this")
        install.install(self.plugin, self.data, self.state, self.module)
        manifest = json.loads((self.target / "manifest.json").read_text())
        self.assertEqual(manifest["channels"]["iChannel0"], (self.target / "assets/pyramid_background.png").as_uri())
        self.assertTrue((self.target / "interaction/libnexusinput.so").is_file())
        install.install(self.plugin, self.data, self.state, self.module)
        self.assertEqual((self.ui / "ShaderSystem.qml").read_text().count("id: nexusBridge"), 1)
        install.uninstall(self.state)
        self.assertEqual((self.ui / "ShaderSystem.qml").read_text(), self.original_system)
        self.assertEqual((self.ui / "ShaderApply.js").read_text(), self.original_apply)
        self.assertEqual((self.target / "personal-change.txt").read_text(), "keep this")

    def test_dry_run_does_not_write(self):
        install.install(self.plugin, self.data, self.state, None, dry_run=True)
        self.assertFalse(self.target.exists())
        self.assertFalse(self.state.exists())
        self.assertEqual((self.ui / "ShaderSystem.qml").read_text(), self.original_system)

    def test_uninstall_preserves_later_user_edits(self):
        install.install(self.plugin, self.data, self.state, self.module)
        path = self.ui / "ShaderApply.js"
        path.write_text(path.read_text() + "// later user edit\n")
        with self.assertRaisesRegex(ValueError, "Installed file changed"):
            install.uninstall(self.state)
        self.assertIn("later user edit", path.read_text())
        self.assertTrue(self.target.exists())

    def test_failure_rolls_back_package_and_first_hook(self):
        original_write = install.atomic_write
        failed = False

        def fail_once(path, text):
            nonlocal failed
            if path.name == "ShaderApply.js" and not failed:
                failed = True
                raise OSError("simulated write failure")
            original_write(path, text)

        with patch.object(install, "atomic_write", side_effect=fail_once):
            with self.assertRaisesRegex(OSError, "simulated write failure"):
                install.install(self.plugin, self.data, self.state, self.module)
        self.assertFalse(self.target.exists())
        self.assertEqual((self.ui / "ShaderSystem.qml").read_text(), self.original_system)
        self.assertEqual((self.ui / "ShaderApply.js").read_text(), self.original_apply)

    def test_unknown_host_fails_before_writing(self):
        (self.ui / "ShaderSystem.qml").write_text("unknown host version")
        with self.assertRaisesRegex(ValueError, "Unrecognized"):
            install.install(self.plugin, self.data, self.state, self.module)
        self.assertFalse(self.target.exists())

    def test_existing_unmarked_hooks_are_adopted_once(self):
        text = "Item {\n" + LEGACY_LOADER + ANCHOR + "}\n"
        updated = patch_shader_system(text)
        self.assertEqual(updated.count("id: nexusBridge"), 1)
        self.assertEqual(patch_shader_system(updated), updated)
        mouse = patch_shader_apply(self.original_apply)
        self.assertEqual(patch_shader_apply(mouse), mouse)


if __name__ == "__main__":
    unittest.main()
