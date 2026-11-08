"""Render the actual Plasma GLSL wrapper and check spanning by crop comparison."""
from __future__ import annotations

import ctypes
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

import numpy as np
from PIL import Image
from PySide6.QtCore import Qt
from PySide6.QtGui import (
    QGuiApplication, QImage, QOffscreenSurface, QOpenGLContext,
    QSurfaceFormat, QVector2D, QVector3D, QVector4D,
)
from PySide6.QtOpenGL import (
    QOpenGLFramebufferObject, QOpenGLShader, QOpenGLShaderProgram,
    QOpenGLTexture, QOpenGLVertexArrayObject,
)

PACKAGE = Path(__file__).resolve().parents[2]
REPOSITORY = PACKAGE.parents[1]
OUTPUT = Path(os.environ.get('NEXUS_OUTPUT', REPOSITORY / 'build/preview'))
STATES = Path(os.environ.get('NEXUS_STATES', REPOSITORY / 'build/states'))
DATA_HOME = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
ENGINE = Path(os.environ.get('NEXUS_ENGINE_SOURCE', DATA_HOME / 'plasma/wallpapers/online.knowmad.shaderwallpaper/contents/source/src/core/shaderengine.cpp'))
WRAPPER = ENGINE.read_text()
APP = QGuiApplication(['nexus-classic-preview'])
GL = ctypes.CDLL('libGL.so.1')
GL.glViewport.argtypes = [ctypes.c_int] * 4
GL.glDrawArrays.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_int]
GL.glGetError.restype = ctypes.c_uint
GL.glGetString.restype = ctypes.c_char_p


def constant(name):
    return re.search(r'static const char \*' + name + r' = R"\((.*?)\)";', WRAPPER, re.S)[1]


def flip(image):
    return image.flipped(Qt.Vertical) if hasattr(image, 'flipped') else image.mirrored(False, True)


class Renderer:
    def __init__(self, gles=False):
        surface_format = QSurfaceFormat()
        surface_format.setVersion(3, 0 if gles else 3)
        if gles:
            surface_format.setRenderableType(QSurfaceFormat.OpenGLES)
        else:
            surface_format.setProfile(QSurfaceFormat.CoreProfile)
        self.context = QOpenGLContext()
        self.context.setFormat(surface_format)
        if not self.context.create():
            raise RuntimeError('OpenGL context creation failed')
        self.surface = QOffscreenSurface()
        self.surface.setFormat(self.context.format())
        self.surface.create()
        if not self.context.makeCurrent(self.surface):
            raise RuntimeError('Cannot make OpenGL context current')
        version = constant('glslVersionGles' if self.context.isOpenGLES() else 'glslVersionDesktop')
        precision = constant('glslPrecisionGles') if self.context.isOpenGLES() else ''
        header = version + precision + constant('shadertoyDefines')
        vertex = header + '''
            out vec2 fragCoord;
            out vec2 qt_TexCoord0;
            uniform vec3 iResolution;
            void main() {
                vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                qt_TexCoord0 = p;
                fragCoord = p * iResolution.xy;
                gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
            }
        '''
        fragment = header + constant('shaderHeader') + '\n#line 1 0\n' + (PACKAGE / 'main.frag').read_text() + constant('shaderFooter')
        self.program = QOpenGLShaderProgram()
        for shader_type, code in [(QOpenGLShader.Vertex, vertex), (QOpenGLShader.Fragment, fragment)]:
            if not self.program.addShaderFromSourceCode(shader_type, code):
                raise RuntimeError(self.program.log())
        if not self.program.link():
            raise RuntimeError(self.program.log())
        self.program.bind()
        self.vao = QOpenGLVertexArrayObject()
        self.vao.create()
        self.vao.bind()
        self.textures = []
        for i, name in enumerate(['pyramid_background.png', 'pulse.png', 'glow.png']):
            image = flip(QImage(str(PACKAGE / 'assets' / name)))
            texture = QOpenGLTexture(image)
            texture.setWrapMode(QOpenGLTexture.Repeat)
            texture.setMinificationFilter(QOpenGLTexture.LinearMipMapLinear)
            texture.setMagnificationFilter(QOpenGLTexture.Linear)
            texture.bind(i)
            self.uniform('iChannel' + str(i), i)
            self.textures.append(texture)
        self.uniform('iDitherAmount', 0.0)
        self.kind = 'GLES' if self.context.isOpenGLES() else 'OpenGL'
        print(f'{self.kind}: shader compiled and linked; {GL.glGetString(0x1F01).decode()}', flush=True)

    def set_state(self, path):
        self.context.makeCurrent(self.surface)
        if len(self.textures) == 4:
            self.textures.pop().destroy()
        image = flip(QImage(str(path)))
        if image.isNull():
            raise RuntimeError('Missing model state image: ' + str(path))
        texture = QOpenGLTexture(image)
        texture.setWrapMode(QOpenGLTexture.Repeat)
        texture.setMinificationFilter(QOpenGLTexture.LinearMipMapLinear)
        texture.setMagnificationFilter(QOpenGLTexture.Linear)
        self.textures.append(texture)
        self.program.bind()
        self.uniform('iChannel3', 3)
        self.uniform('iChannelResolution[3]', QVector3D(image.width(), image.height(), image.width() / image.height()))

    def uniform(self, name, value):
        self.program.setUniformValue(self.program.uniformLocation(name), value)

    def render(self, width, height, seconds=18.25, desktop=None, offset=(0, 0)):
        self.context.makeCurrent(self.surface)
        while GL.glGetError():
            pass
        self.program.bind()
        self.vao.bind()
        frame = QOpenGLFramebufferObject(width, height)
        if not frame.isValid():
            raise RuntimeError('Framebuffer is invalid')
        frame.bind()
        GL.glViewport(0, 0, width, height)
        # FBO allocation uses the current texture unit. Restore samplers
        # afterwards so the glow slot cannot sample the output attachment.
        for i, texture in enumerate(self.textures):
            texture.bind(i)
        self.uniform('iResolution', QVector3D(width, height, width / height))
        self.uniform('iDesktopResolution', QVector2D(*(desktop or (width, height))))
        self.uniform('iScreenOffset', QVector2D(*offset))
        self.uniform('iDate', QVector4D(2026, 10, 8, seconds))
        GL.glDrawArrays(0x0004, 0, 3)
        GL.glFinish()
        error = GL.glGetError()
        if error:
            raise RuntimeError(f'GL error: {error:#x}')
        image = frame.toImage().convertToFormat(QImage.Format_RGBA8888)
        array = np.frombuffer(image.constBits(), np.uint8).reshape(height, image.bytesPerLine() // 4, 4)[:, :width].copy()
        frame.release()
        return array


def save(array, name):
    Image.fromarray(array).save(OUTPUT / name)


def check_crop(renderer, width, height, desktop, offset, name):
    full_width, full_height = int(desktop[0]), int(desktop[1])
    full = renderer.render(full_width, full_height, desktop=desktop)
    part = renderer.render(width, height, desktop=desktop, offset=offset)
    x, y = map(int, offset)
    reference = full[full_height-y-height:full_height-y, x:x+width]
    difference = np.abs(part.astype(np.int16) - reference.astype(np.int16))
    result = {'max_channel_error': int(difference.max()), 'mean_channel_error': float(difference.mean())}
    print(name, result, flush=True)
    if difference.max() > 3 or difference.mean() > 0.05:
        raise RuntimeError(f'{name}: spanning differs from the same global canvas')
    return result


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    if '--gles-only' in sys.argv:
        gles = Renderer(gles=True)
        gles.set_state(STATES / 'state-038.png')
        save(gles.render(1678, 512), 'nexus-classic-gles.png')
        print('GLES: compiled and rendered successfully', flush=True)
        return
    gl = Renderer(gles='--gles' in sys.argv)
    gl.set_state(STATES / 'state-038.png')
    save(gl.render(2560, 1440, desktop=(4720.5, 1440), offset=(2160, 0)), 'nexus-classic-preview.png')
    save(gl.render(1678, 512), 'nexus-classic-wide.png')
    checks = {}
    checks['left_1080p'] = check_crop(gl, 1920, 1080, (4196, 1280), (0, 200), 'Left monitor')
    checks['right_1440p_112_5_percent'] = check_crop(gl, 2560, 1440, (4720.5, 1440), (2160, 0), 'Right monitor, fractional DPI')
    gl.set_state(STATES / 'click-before.png')
    before = gl.render(1000, 400, 0.25)
    gl.set_state(STATES / 'click-after.png')
    after = gl.render(1000, 400, 0.25)
    save(after, 'nexus-classic-click.png')
    # The AOSP four-way burst: source (497,197), 300 px/sec, age .25.
    # All four outgoing heads must add substantial light to the same canvas.
    delta = after[:, :, :3].astype(float) - before[:, :, :3].astype(float)
    for name, x, y in [('right', 572, 197), ('left', 422, 197), ('down', 497, 122), ('up', 497, 272)]:
        brightness = float(delta[400-y-2:400-y+2, x-2:x+2].max(axis=2).mean())
        checks['click_' + name + '_added_light'] = brightness
        if brightness < 80:
            raise RuntimeError('Missing rendered click direction: ' + name)
    gl.set_state(STATES / 'midnight.png')
    a = gl.render(1000, 400, 86399.995)
    b = gl.render(1000, 400, 0.005)
    difference = np.abs(a.astype(np.int16) - b.astype(np.int16))
    checks['midnight_mean_error'] = float(difference.mean())
    if difference.mean() > 3.0:
        raise RuntimeError('Midnight causes a visible animation jump')
    # A real encoded preview, rendered at fixed times from the same shader.
    path = OUTPUT / 'nexus-classic-preview.mp4'
    command = ['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgba',
               '-s', '1680x512', '-r', '30', '-i', '-', '-an', '-c:v', 'libx264',
               '-preset', 'fast', '-crf', '20', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(path)]
    process = subprocess.Popen(command, stdin=subprocess.PIPE)
    start = time.monotonic()
    for frame in json.loads((STATES / 'frames.json').read_text()):
        gl.set_state(STATES / frame['file'])
        process.stdin.write(gl.render(1680, 512, frame['seconds']).tobytes())
    process.stdin.close()
    if process.wait() != 0:
        raise RuntimeError('ffmpeg preview encoding failed')
    print(f'Encoded eight-second preview in {time.monotonic() - start:.1f}s', flush=True)
    (OUTPUT / 'checks.json').write_text(json.dumps(checks, indent=2) + '\n')


if __name__ == '__main__':
    main()
