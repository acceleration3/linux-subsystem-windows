#pragma once

#define LSW_GL_FUNCTIONS(X) \
    X(const unsigned char*, glGetString, unsigned) \
    X(unsigned, glGetError, void) \
    X(void, glGetIntegerv, unsigned, int*) \
    X(void, glGetFloatv, unsigned, float*) \
    X(void, glGetBooleanv, unsigned, unsigned char*) \
    X(void, glClear, unsigned) \
    X(void, glClearColor, float, float, float, float) \
    X(void, glClearDepth, double) \
    X(void, glClearStencil, int) \
    X(void, glViewport, int, int, int, int) \
    X(void, glScissor, int, int, int, int) \
    X(void, glEnable, unsigned) \
    X(void, glDisable, unsigned) \
    X(unsigned char, glIsEnabled, unsigned) \
    X(void, glReadPixels, int, int, int, int, unsigned, unsigned, void*) \
    X(void, glReadBuffer, unsigned) \
    X(void, glDrawBuffer, unsigned) \
    X(void, glFinish, void) \
    X(void, glFlush, void) \
    X(void, glPixelStorei, unsigned, int) \
    X(void, glBlendFunc, unsigned, unsigned) \
    X(void, glDepthFunc, unsigned) \
    X(void, glDepthMask, unsigned char) \
    X(void, glColorMask, unsigned char, unsigned char, unsigned char, unsigned char) \
    X(void, glGenTextures, int, unsigned*) \
    X(void, glDeleteTextures, int, const unsigned*) \
    X(void, glBindTexture, unsigned, unsigned) \
    X(unsigned char, glIsTexture, unsigned) \
    X(void, glTexImage1D, unsigned, int, int, int, int, unsigned, unsigned, const void*) \
    X(void, glTexImage2D, unsigned, int, int, int, int, int, unsigned, unsigned, const void*) \
    X(void, glTexSubImage1D, unsigned, int, int, int, unsigned, unsigned, const void*) \
    X(void, glTexSubImage2D, unsigned, int, int, int, int, int, unsigned, unsigned, const void*) \
    X(void, glCopyTexImage2D, unsigned, int, unsigned, int, int, int, int, int) \
    X(void, glTexParameteri, unsigned, unsigned, int) \
    X(void, glGetTexImage, unsigned, int, unsigned, unsigned, void*) \
    X(void, glGetTexLevelParameteriv, unsigned, int, unsigned, int*) \
    X(void, glGetTexParameteriv, unsigned, unsigned, int*) \
    X(void, glDrawArrays, unsigned, int, int) \
    X(void, glDrawElements, unsigned, int, unsigned, const void*) \
    X(void, glLineWidth, float) \
    X(void, glBegin, unsigned) \
    X(void, glEnd, void) \
    X(void, glVertex2f, float, float) \
    X(void, glColor4f, float, float, float, float) \
    X(void, glColor4ub, unsigned char, unsigned char, unsigned char, unsigned char) \
    X(void, glMatrixMode, unsigned) \
    X(void, glLoadIdentity, void) \
    X(void, glPushMatrix, void) \
    X(void, glPopMatrix, void) \
    X(void, glEnableClientState, unsigned) \
    X(void, glDisableClientState, unsigned) \
    X(void, glVertexPointer, int, unsigned, int, const void*) \
    X(void, glColorPointer, int, unsigned, int, const void*) \
    X(void, glTexImage3D, unsigned, int, int, int, int, int, int, unsigned, unsigned, const void*) \
    X(void, glTexSubImage3D, unsigned, int, int, int, int, int, int, int, unsigned, unsigned, const void*) \
    X(void, glActiveTexture, unsigned) \
    X(void, glCompressedTexImage2D, unsigned, int, unsigned, int, int, int, int, const void*) \
    X(void, glGetCompressedTexImage, unsigned, int, void*) \
    X(void, glBlendEquation, unsigned) \
    X(void, glBlendFuncSeparate, unsigned, unsigned, unsigned, unsigned) \
    X(void, glGenBuffers, int, unsigned*) \
    X(void, glDeleteBuffers, int, const unsigned*) \
    X(void, glBindBuffer, unsigned, unsigned) \
    X(unsigned char, glIsBuffer, unsigned) \
    X(void, glBufferData, unsigned, std::ptrdiff_t, const void*, unsigned) \
    X(void, glBufferSubData, unsigned, std::ptrdiff_t, std::ptrdiff_t, const void*) \
    X(void, glGetBufferSubData, unsigned, std::ptrdiff_t, std::ptrdiff_t, void*) \
    X(void, glGetBufferParameteriv, unsigned, unsigned, int*) \
    X(unsigned char, glUnmapBuffer, unsigned) \
    X(void*, glMapBufferRange, unsigned, std::ptrdiff_t, std::ptrdiff_t, unsigned) \
    X(void, glFlushMappedBufferRange, unsigned, std::ptrdiff_t, std::ptrdiff_t) \
    X(void, glGenQueries, int, unsigned*) \
    X(void, glDeleteQueries, int, const unsigned*) \
    X(unsigned char, glIsQuery, unsigned) \
    X(void, glBeginQuery, unsigned, unsigned) \
    X(void, glEndQuery, unsigned) \
    X(void, glQueryCounter, unsigned, unsigned) \
    X(void, glGetQueryiv, unsigned, unsigned, int*) \
    X(void, glGetQueryObjectiv, unsigned, unsigned, int*) \
    X(void, glGetQueryObjectuiv, unsigned, unsigned, unsigned*) \
    X(void, glGetQueryObjectui64v, unsigned, unsigned, unsigned long long*) \
    X(unsigned, glCreateShader, unsigned) \
    X(void, glShaderSource, unsigned, int, const char* const*, const int*) \
    X(void, glCompileShader, unsigned) \
    X(void, glGetShaderiv, unsigned, unsigned, int*) \
    X(void, glGetShaderInfoLog, unsigned, int, int*, char*) \
    X(unsigned, glCreateProgram, void) \
    X(void, glAttachShader, unsigned, unsigned) \
    X(void, glLinkProgram, unsigned) \
    X(void, glGetProgramiv, unsigned, unsigned, int*) \
    X(void, glGetProgramInfoLog, unsigned, int, int*, char*) \
    X(void, glUseProgram, unsigned) \
    X(void, glDeleteShader, unsigned) \
    X(void, glDeleteProgram, unsigned) \
    X(unsigned char, glIsShader, unsigned) \
    X(unsigned char, glIsProgram, unsigned) \
    X(int, glGetUniformLocation, unsigned, const char*) \
    X(void, glUniform1i, int, int) \
    X(void, glUniform1f, int, float) \
    X(void, glUniform2i, int, int, int) \
    X(void, glEnableVertexAttribArray, unsigned) \
    X(void, glDisableVertexAttribArray, unsigned) \
    X(void, glVertexAttribPointer, unsigned, int, unsigned, unsigned char, int, const void*) \
    X(void, glVertexAttribDivisor, unsigned, unsigned) \
    X(void, glDrawBuffers, int, const unsigned*) \
    X(const unsigned char*, glGetStringi, unsigned, unsigned) \
    X(void, glGenVertexArrays, int, unsigned*) \
    X(void, glBindVertexArray, unsigned) \
    X(void, glDeleteVertexArrays, int, const unsigned*) \
    X(unsigned char, glIsVertexArray, unsigned) \
    X(void, glGenFramebuffers, int, unsigned*) \
    X(void, glBindFramebuffer, unsigned, unsigned) \
    X(void, glDeleteFramebuffers, int, const unsigned*) \
    X(unsigned char, glIsFramebuffer, unsigned) \
    X(void, glFramebufferTexture2D, unsigned, unsigned, unsigned, unsigned, int) \
    X(void, glFramebufferTexture, unsigned, unsigned, unsigned, int) \
    X(void, glFramebufferTextureLayer, unsigned, unsigned, unsigned, int, int) \
    X(void, glFramebufferRenderbuffer, unsigned, unsigned, unsigned, unsigned) \
    X(unsigned, glCheckFramebufferStatus, unsigned) \
    X(void, glGenRenderbuffers, int, unsigned*) \
    X(void, glBindRenderbuffer, unsigned, unsigned) \
    X(void, glDeleteRenderbuffers, int, const unsigned*) \
    X(void, glRenderbufferStorage, unsigned, unsigned, int, int) \
    X(void, glRenderbufferStorageMultisample, unsigned, int, unsigned, int, int) \
    X(void, glBlitFramebuffer, int, int, int, int, int, int, int, int, unsigned, unsigned) \
    X(void, glClearBufferfv, unsigned, int, const float*) \
    X(void, glClearBufferiv, unsigned, int, const int*) \
    X(void, glClearBufferuiv, unsigned, int, const unsigned*) \
    X(void, glGenerateMipmap, unsigned) \
    X(void, glGetFramebufferAttachmentParameteriv, unsigned, unsigned, unsigned, int*) \
    X(void, glBeginTransformFeedback, unsigned) \
    X(void, glEndTransformFeedback, void) \
    X(void, glBindBufferBase, unsigned, unsigned, unsigned) \
    X(void, glTransformFeedbackVaryings, unsigned, int, const char* const*, unsigned) \
    X(void, glClampColor, unsigned, unsigned) \
    X(void, glDrawArraysInstanced, unsigned, int, int, int) \
    X(void, glCopyBufferSubData, unsigned, unsigned, std::ptrdiff_t, std::ptrdiff_t, std::ptrdiff_t) \
    X(void, glTexBuffer, unsigned, unsigned, unsigned) \
    X(void*, glFenceSync, unsigned, unsigned) \
    X(unsigned char, glIsSync, void*) \
    X(void, glDeleteSync, void*) \
    X(unsigned, glClientWaitSync, void*, unsigned, unsigned long long) \
    X(void, glWaitSync, void*, unsigned, unsigned long long) \
    X(void, glGetSynciv, void*, unsigned, int, int*, int*) \
    X(void, glDrawElementsBaseVertex, unsigned, int, unsigned, const void*, int) \
    X(void, glTexImage2DMultisample, unsigned, int, unsigned, int, int, unsigned char) \
    X(void, glDrawArraysIndirect, unsigned, const void*) \
    X(void, glDrawElementsIndirect, unsigned, unsigned, const void*) \
    X(void, glGenTransformFeedbacks, int, unsigned*) \
    X(void, glBindTransformFeedback, unsigned, unsigned) \
    X(void, glDeleteTransformFeedbacks, int, const unsigned*) \
    X(void, glDrawTransformFeedback, unsigned, unsigned) \
    X(void, glTexStorage1D, unsigned, int, unsigned, int) \
    X(void, glTexStorage2D, unsigned, int, unsigned, int, int) \
    X(void, glTexStorage3D, unsigned, int, unsigned, int, int, int) \
    X(void, glMemoryBarrier, unsigned) \
    X(void, glDispatchCompute, unsigned, unsigned, unsigned) \
    X(void, glClearBufferData, unsigned, unsigned, unsigned, unsigned, const void*) \
    X(void, glClearBufferSubData, unsigned, unsigned, std::ptrdiff_t, std::ptrdiff_t, unsigned, unsigned, const void*) \
    X(void, glDebugMessageCallback, const void*, const void*) \
    X(void, glDebugMessageControl, unsigned, unsigned, unsigned, int, const unsigned*, unsigned char) \
    X(void, glDebugMessageInsert, unsigned, unsigned, unsigned, unsigned, int, const char*) \
    X(void, glTextureView, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned) \
    X(void, glMultiDrawArraysIndirect, unsigned, const void*, int, int) \
    X(void, glMultiDrawElementsIndirect, unsigned, unsigned, const void*, int, int) \
    X(void, glBufferStorage, unsigned, std::ptrdiff_t, const void*, unsigned) \
    X(unsigned, glGetGraphicsResetStatus, void) \
    X(void, glGetUnsignedBytevEXT, unsigned, unsigned char*) \
    X(void, glCreateMemoryObjectsEXT, int, unsigned*) \
    X(void, glDeleteMemoryObjectsEXT, int, const unsigned*) \
    X(void, glMemoryObjectParameterivEXT, unsigned, unsigned, const int*) \
    X(void, glImportMemoryWin32HandleEXT, unsigned, unsigned long long, unsigned, void*) \
    X(void, glTexStorageMem2DEXT, unsigned, int, unsigned, int, int, unsigned, unsigned long long) \
    X(void, glGenSemaphoresEXT, int, unsigned*) \
    X(void, glDeleteSemaphoresEXT, int, const unsigned*) \
    X(void, glImportSemaphoreWin32HandleEXT, unsigned, unsigned, void*) \
    X(void, glWaitSemaphoreEXT, unsigned, unsigned, const unsigned*, unsigned, const unsigned*, const unsigned*) \
    X(void, glSignalSemaphoreEXT, unsigned, unsigned, const unsigned*, unsigned, const unsigned*, const unsigned*) \
    X(void, glGetInternalformativ, unsigned, unsigned, unsigned, int, int*) \
    X(void, glGetProgramBinary, unsigned, int, int*, unsigned*, void*) \
    X(void, glProgramBinary, unsigned, unsigned, const void*, int) \
    X(void, glProgramParameteri, unsigned, unsigned, int) \
    X(void, glUniform4f, int, float, float, float, float) \
    X(void, glUniform1ui, int, unsigned) \
    X(void, glValidateProgram, unsigned) \
    X(int, glGetAttribLocation, unsigned, const char*) \
    X(void, glBindAttribLocation, unsigned, unsigned, const char*) \
    X(void, glBindFragDataLocation, unsigned, unsigned, const char*) \
    X(void, glClearBufferfi, unsigned, int, float, int) \
    X(void, glGetShaderSource, unsigned, int, int*, char*) \
    X(void, glDetachShader, unsigned, unsigned) \
    X(void, glCompressedTexSubImage2D, unsigned, int, int, int, int, int, unsigned, int, const void*)
