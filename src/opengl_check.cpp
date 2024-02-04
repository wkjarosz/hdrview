#if defined(HELLOIMGUI_HAS_OPENGL)

#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_include_opengl.h" // cross-platform way to include OpenGL headers
#include <fmt/core.h>

bool check_glerror(const char *cmd, const char *file, int line)
{
    GLenum      err = glGetError();
    const char *msg = nullptr;

    switch (err)
    {
    case GL_NO_ERROR: return false;
    case GL_INVALID_ENUM: msg = "invalid enumeration"; break;
    case GL_INVALID_VALUE: msg = "invalid value"; break;
    case GL_INVALID_OPERATION: msg = "invalid operation"; break;
    case GL_INVALID_FRAMEBUFFER_OPERATION: msg = "invalid framebuffer operation"; break;
    case GL_OUT_OF_MEMORY: msg = "out of memory"; break;
#ifndef __EMSCRIPTEN__
    case GL_STACK_UNDERFLOW: msg = "stack underflow"; break;
    case GL_STACK_OVERFLOW: msg = "stack overflow"; break;
#endif
    default: msg = "unknown error"; break;
    }

    fmt::print(stderr, "OpenGL error {}:{} ({}) during operation \"{}\"!\n", file, line, msg, cmd);
    HelloImGui::Log(HelloImGui::LogLevel::Error, "OpenGL error (%s) during operation \"%s\"!\n", msg, cmd);
    return true;
}

#endif // defined(HELLOIMGUI_HAS_OPENGL)
