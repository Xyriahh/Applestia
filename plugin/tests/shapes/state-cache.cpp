// Execute the actual CStateGuard, extracted by run.py, against software EGL
// and models of the matching Hyprland shader/scissor/viewport/cap caches.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <array>
#include <cassert>
#include <iostream>
#include <stdexcept>

constexpr int W = 64, H = 48;
using Box = std::array<GLint, 4>;

struct CachedOpenGL {
    GLuint currentProgram = 0;
    Box lastScissor{}, lastViewport{};
    bool blend = false, scissors = false, stencil = false;
    unsigned programBinds = 0, scissorChanges = 0;

    void useShader(GLuint program) {
        if (currentProgram == program) return;
        glUseProgram(program);
        currentProgram = program;
        ++programBinds;
    }
    void setViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
        const Box box{x, y, w, h};
        if (lastViewport == box) return;
        glViewport(x, y, w, h);
        lastViewport = box;
    }
    void setCapStatus(GLenum cap, bool enabled) {
        bool &cached = cap == GL_BLEND ? blend : cap == GL_SCISSOR_TEST ? scissors : stencil;
        if (cached == enabled) return;
        if (enabled) glEnable(cap); else glDisable(cap);
        cached = enabled;
    }
    void scissor(GLint x, GLint y, GLsizei w, GLsizei h, bool transform) {
        assert(!transform);
        const Box box{x, y, w, h};
        if (lastScissor != box) {
            glScissor(x, y, w, h);
            lastScissor = box;
            ++scissorChanges;
        }
        setCapStatus(GL_SCISSOR_TEST, true);
    }
    void scissor(const Box &box) { scissor(box[0], box[1], box[2], box[3], false); }
} cachedGL;
auto *g_pHyprOpenGL = &cachedGL;
struct CachedRenderer {
    void blend(bool enabled) {
        cachedGL.setCapStatus(GL_BLEND, enabled);
        if (enabled) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
} cachedRenderer;
auto *g_pHyprRenderer = &cachedRenderer;

#include "state-guard-under-test.hpp"

GLuint makeProgram() {
    const char *vertex = "#version 300 es\nlayout(location=0) in vec2 pos; void main(){gl_Position=vec4(pos*2.0-1.0,0,1);}";
    const char *fragment = "#version 300 es\nprecision highp float; uniform vec4 color; out vec4 fragColor; void main(){fragColor=color;}";
    auto compile = [](GLenum type, const char *source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok); assert(ok);
        return shader;
    };
    const GLuint v = compile(GL_VERTEX_SHADER, vertex), f = compile(GL_FRAGMENT_SHADER, fragment);
    const GLuint program = glCreateProgram();
    glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
    GLint ok; glGetProgramiv(program, GL_LINK_STATUS, &ok); assert(ok);
    glDeleteShader(v); glDeleteShader(f);
    return program;
}
void checkProgram(GLuint expected) {
    GLint actual; glGetIntegerv(GL_CURRENT_PROGRAM, &actual);
    assert(static_cast<GLuint>(actual) == expected && cachedGL.currentProgram == expected);
}
void checkScissor(const Box &expected, bool enabled) {
    Box actual; glGetIntegerv(GL_SCISSOR_BOX, actual.data());
    assert(actual == expected && cachedGL.lastScissor == expected);
    assert(bool(glIsEnabled(GL_SCISSOR_TEST)) == enabled && cachedGL.scissors == enabled);
}
void checkCachedDraw(GLuint shader, const Box &clip) {
    // Re-selecting the final stage shader must be a genuine safe cache hit.
    const auto binds = cachedGL.programBinds;
    cachedGL.useShader(shader);
    assert(cachedGL.programBinds == binds);
    checkProgram(shader);
    cachedGL.setViewport(0, 0, W, H);
    cachedGL.setCapStatus(GL_BLEND, false);
    cachedGL.setCapStatus(GL_SCISSOR_TEST, false);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    cachedGL.scissor(clip);
    checkScissor(clip, true);
    glUniform4f(glGetUniformLocation(shader, "color"), 1, 0, 0, 1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    std::array<GLubyte, 4> inside{}, outside{};
    glReadPixels(clip[0] + 1, clip[1] + 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, inside.data());
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outside.data());
    assert((inside == std::array<GLubyte, 4>{255, 0, 0, 255}));
    assert((outside == std::array<GLubyte, 4>{0, 0, 0, 255}));
}
int main() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor; assert(eglInitialize(display, &major, &minor));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[]{EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config; EGLint count; assert(eglChooseConfig(display, attrs, &config, 1, &count) && count);
    const EGLint pb[]{EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE}, ca[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const auto surface = eglCreatePbufferSurface(display, config, pb);
    const auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
    assert(eglMakeCurrent(display, surface, surface, context));
    GLuint vao, vbo; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const float quad[]{0, 0, 1, 0, 0, 1, 1, 1};
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr); glEnableVertexAttribArray(0);
    const GLuint incoming = makeProgram(), shape = makeProgram(), foreground = makeProgram();
    const Box incomingClip{3, 5, 23, 17}, stageClip{31, 19, 18, 13}, rawClip{7, 11, 21, 15};
    cachedGL.setViewport(0, 0, W, H);

    for (bool enabled : {true, false}) {
        for (GLuint stageShader : {shape, shape, foreground, foreground, shape, foreground}) {
            cachedGL.useShader(incoming);
            cachedGL.scissor(incomingClip);
            // The incoming actual box, not an assumed tracked box, is authoritative.
            glScissor(rawClip[0], rawClip[1], rawClip[2], rawClip[3]);
            cachedGL.setCapStatus(GL_SCISSOR_TEST, enabled);
            {
                CStateGuard state;
                cachedGL.setCapStatus(GL_SCISSOR_TEST, false);
                cachedGL.useShader(stageShader);
                cachedGL.scissor(stageClip);
            }
            checkProgram(stageShader);
            checkScissor(rawClip, enabled);
            // Same-program and same-box selections after restoration must work.
            checkCachedDraw(stageShader, rawClip);
            // Returning to the stage's last box must rebind, not hit a stale cache.
            const auto changes = cachedGL.scissorChanges;
            cachedGL.scissor(stageClip);
            assert(cachedGL.scissorChanges == changes + 1);
            checkScissor(stageClip, true);
            const auto binds = cachedGL.programBinds;
            cachedGL.useShader(incoming);
            assert(cachedGL.programBinds == binds + 1);
            checkProgram(incoming);
        }
    }
    // Exception path: raw GL work changed the box but the cache still believes
    // the saved value. A cache-aware call alone may skip; actual GL must agree too.
    cachedGL.scissor(incomingClip);
    try {
        CStateGuard state;
        cachedGL.useShader(shape);
        glScissor(stageClip[0], stageClip[1], stageClip[2], stageClip[3]);
        throw std::runtime_error("simulated stage failure");
    } catch (const std::runtime_error&) {}
    checkProgram(shape);
    checkScissor(incomingClip, true);
    checkCachedDraw(shape, incomingClip);
    assert(glGetError() == GL_NO_ERROR);
    std::cout << "PASS actual CStateGuard, repeated shape/foreground cached selection, incoming scissor enable/box, exception restoration and clipped pixels\n";
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context); eglDestroySurface(display, surface); eglTerminate(display);
}
