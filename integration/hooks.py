# SPDX-License-Identifier: GPL-3.0-or-later
"""Small integration hooks for Shader Wallpaper 4.2.4; no vendored host files."""
import re

BEGIN = "    // BEGIN NEXUS_CLASSIC_PLASMA\n"
END = "    // END NEXUS_CLASSIC_PLASMA\n"
LEGACY_LOADER = '''    // Nexus Classic has a shared, read-only texture of ambient and click
    // pulses. Keep its helper scoped to this user-owned shader and to the
    // desktop; other shaders and greeter surfaces retain their own inputs.
    Loader {
        id: nexusBridge
        objectName: "nexusClassicBridgeLoader"
        active: !shaderSystem.restrictedMode
            && ShaderLibrarySingleton.toRelativeShaderPath(shaderSystem._shaderPath)
                === "Shaders/Retro/NexusClassic/main.frag"
        source: active ? ShaderLibrarySingleton.userFileUrl("Shaders/Retro/NexusClassic/interaction/NexusBridge.qml") : ""
        onLoaded: {
            item.engine = shaderEngine
            item.mouseEnabled = Qt.binding(function() { return shaderSystem._allowMouse })
            item.shared = Qt.binding(function() { return shaderSystem._allowExperimentalScreens })
        }
    }

'''
MOUSE_BEFORE = r'    if (/\biMouse\b/.test(allCode)) m.mouseEnabled = true;' + '\n'
MOUSE_AFTER = r'    if (/\biMouse\b/.test(allCode) || /^[ \t]*\/\/[ \t]*@mouse\b/im.test(allCode)) m.mouseEnabled = true;' + '\n'
ANCHOR = "    // Mouse tracking area, for hover and presses that no item above takes.\n"


def _replace_block(text, body):
    pattern = re.escape(BEGIN) + r".*?" + re.escape(END)
    if re.search(pattern, text, re.S):
        return re.sub(pattern, lambda _: BEGIN + body + END, text, count=1, flags=re.S)
    return None


def patch_shader_system(text):
    updated = _replace_block(text, LEGACY_LOADER)
    if updated is not None:
        return updated
    if LEGACY_LOADER in text:
        return text.replace(LEGACY_LOADER, BEGIN + LEGACY_LOADER + END, 1)
    if text.count(ANCHOR) != 1:
        raise ValueError("Unrecognized ShaderSystem.qml; no files were changed")
    return text.replace(ANCHOR, BEGIN + LEGACY_LOADER + END + ANCHOR, 1)


def patch_shader_apply(text):
    updated = _replace_block(text, MOUSE_AFTER)
    if updated is not None:
        return updated
    for original in (MOUSE_BEFORE, MOUSE_AFTER):
        if text.count(original) == 1:
            return text.replace(original, BEGIN + MOUSE_AFTER + END, 1)
    raise ValueError("Unrecognized ShaderApply.js; no files were changed")


def patched_files(plugin_root):
    ui = plugin_root / "contents/ui"
    return {
        ui / "ShaderSystem.qml": patch_shader_system((ui / "ShaderSystem.qml").read_text()),
        ui / "ShaderApply.js": patch_shader_apply((ui / "ShaderApply.js").read_text()),
    }
