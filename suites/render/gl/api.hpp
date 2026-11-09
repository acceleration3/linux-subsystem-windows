#pragma once

#include "render/gl/defs.hpp"

#include <functional>
#include <string>

#define LSW_GL_FUNCTIONS(X)                                                                                     \
    X(const GLubyte*, glGetString, GLenum)                                                                      \
    X(const GLubyte*, glGetStringi, GLenum, GLuint)                                                             \
    X(void, glGetIntegerv, GLenum, GLint*)                                                                      \
    X(GLenum, glGetError)                                                                                       \
    X(void, glEnable, GLenum)                                                                                   \
    X(void, glDisable, GLenum)                                                                                  \
    X(void, glClear, GLbitfield)                                                                                \
    X(void, glClearColor, GLfloat, GLfloat, GLfloat, GLfloat)                                                   \
    X(void, glClearDepth, GLdouble)                                                                             \
    X(void, glClearStencil, GLint)                                                                              \
    X(void, glViewport, GLint, GLint, GLsizei, GLsizei)                                                         \
    X(void, glScissor, GLint, GLint, GLsizei, GLsizei)                                                          \
    X(void, glDepthFunc, GLenum)                                                                                \
    X(void, glDepthMask, GLboolean)                                                                             \
    X(void, glColorMask, GLboolean, GLboolean, GLboolean, GLboolean)                                            \
    X(void, glBlendFunc, GLenum, GLenum)                                                                        \
    X(void, glBlendFuncSeparate, GLenum, GLenum, GLenum, GLenum)                                                \
    X(void, glCullFace, GLenum)                                                                                 \
    X(void, glFrontFace, GLenum)                                                                                \
    X(void, glStencilFunc, GLenum, GLint, GLuint)                                                               \
    X(void, glStencilOp, GLenum, GLenum, GLenum)                                                                \
    X(void, glReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)                                \
    X(void, glReadBuffer, GLenum)                                                                               \
    X(void, glDrawBuffers, GLsizei, const GLenum*)                                                              \
    X(void, glFlush)                                                                                            \
    X(void, glFinish)                                                                                           \
    X(void, glPixelStorei, GLenum, GLint)                                                                       \
    X(void, glGenTextures, GLsizei, GLuint*)                                                                    \
    X(void, glDeleteTextures, GLsizei, const GLuint*)                                                           \
    X(void, glBindTexture, GLenum, GLuint)                                                                      \
    X(void, glActiveTexture, GLenum)                                                                            \
    X(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)           \
    X(void, glTexParameteri, GLenum, GLenum, GLint)                                                             \
    X(void, glGenerateMipmap, GLenum)                                                                           \
    X(void, glGenBuffers, GLsizei, GLuint*)                                                                     \
    X(void, glDeleteBuffers, GLsizei, const GLuint*)                                                            \
    X(void, glBindBuffer, GLenum, GLuint)                                                                       \
    X(void, glBufferData, GLenum, GLsizeiptr, const void*, GLenum)                                              \
    X(void, glBufferSubData, GLenum, GLintptr, GLsizeiptr, const void*)                                         \
    X(void*, glMapBufferRange, GLenum, GLintptr, GLsizeiptr, GLbitfield)                                        \
    X(GLboolean, glUnmapBuffer, GLenum)                                                                         \
    X(void, glGenVertexArrays, GLsizei, GLuint*)                                                                \
    X(void, glDeleteVertexArrays, GLsizei, const GLuint*)                                                       \
    X(void, glBindVertexArray, GLuint)                                                                          \
    X(void, glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)                      \
    X(void, glEnableVertexAttribArray, GLuint)                                                                  \
    X(GLuint, glCreateShader, GLenum)                                                                           \
    X(void, glShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*)                                \
    X(void, glCompileShader, GLuint)                                                                            \
    X(void, glGetShaderiv, GLuint, GLenum, GLint*)                                                              \
    X(void, glGetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                                             \
    X(void, glDeleteShader, GLuint)                                                                             \
    X(GLuint, glCreateProgram)                                                                                  \
    X(void, glAttachShader, GLuint, GLuint)                                                                     \
    X(void, glLinkProgram, GLuint)                                                                              \
    X(void, glGetProgramiv, GLuint, GLenum, GLint*)                                                             \
    X(void, glGetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                                            \
    X(void, glUseProgram, GLuint)                                                                               \
    X(void, glDeleteProgram, GLuint)                                                                            \
    X(GLint, glGetUniformLocation, GLuint, const GLchar*)                                                       \
    X(void, glUniform1i, GLint, GLint)                                                                          \
    X(void, glUniform1f, GLint, GLfloat)                                                                        \
    X(void, glUniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat)                                             \
    X(void, glUniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*)                                      \
    X(void, glDrawArrays, GLenum, GLint, GLsizei)                                                               \
    X(void, glDrawElements, GLenum, GLsizei, GLenum, const void*)                                               \
    X(void, glGenFramebuffers, GLsizei, GLuint*)                                                                \
    X(void, glDeleteFramebuffers, GLsizei, const GLuint*)                                                       \
    X(void, glBindFramebuffer, GLenum, GLuint)                                                                  \
    X(void, glFramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint)                                      \
    X(void, glFramebufferRenderbuffer, GLenum, GLenum, GLenum, GLuint)                                          \
    X(GLenum, glCheckFramebufferStatus, GLenum)                                                                 \
    X(void, glGenRenderbuffers, GLsizei, GLuint*)                                                               \
    X(void, glDeleteRenderbuffers, GLsizei, const GLuint*)                                                      \
    X(void, glBindRenderbuffer, GLenum, GLuint)                                                                 \
    X(void, glRenderbufferStorage, GLenum, GLenum, GLsizei, GLsizei)                                            \
    X(void, glRenderbufferStorageMultisample, GLenum, GLsizei, GLenum, GLsizei, GLsizei)                        \
    X(void, glBlitFramebuffer, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)

namespace lsw::render::gl {
class Api {
public:
#define LSW_GL_MEMBER(ret, name, ...) ret(LSW_GLAPI* name)(__VA_ARGS__) = nullptr;
    LSW_GL_FUNCTIONS(LSW_GL_MEMBER)
#undef LSW_GL_MEMBER

    [[nodiscard]] bool load(const std::function<void*(const char*)>& get_proc, std::string& error);

    [[nodiscard]] static std::string error_name(unsigned code);
};
}  // namespace lsw::render::gl
