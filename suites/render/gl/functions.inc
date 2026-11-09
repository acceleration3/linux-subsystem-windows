#define LSW_GL_FUNCTIONS(X) \
    X(const GLubyte *, glGetString, GLenum) \
    X(const GLubyte *, glGetStringi, GLenum, GLuint) \
    X(void, glGetIntegerv, GLenum, GLint *) \
    X(void, glGetInteger64v, GLenum, GLint64 *) \
    X(void, glGetFloatv, GLenum, GLfloat *) \
    X(void, glGetBooleanv, GLenum, GLboolean *) \
    X(void, glGetDoublev, GLenum, GLdouble *) \
    X(GLenum, glGetError) \
    X(void, glEnable, GLenum) \
    X(void, glDisable, GLenum) \
    X(GLboolean, glIsEnabled, GLenum) \
    X(void, glEnablei, GLenum, GLuint) \
    X(void, glDisablei, GLenum, GLuint) \
    X(GLboolean, glIsEnabledi, GLenum, GLuint) \
    X(void, glGetIntegeri_v, GLenum, GLuint, GLint *) \
    X(void, glGetFloati_v, GLenum, GLuint, GLfloat *) \
    X(void, glClear, GLbitfield) \
    X(void, glClearColor, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glClearDepth, GLdouble) \
    X(void, glClearStencil, GLint) \
    X(void, glViewport, GLint, GLint, GLsizei, GLsizei) \
    X(void, glScissor, GLint, GLint, GLsizei, GLsizei) \
    X(void, glDepthFunc, GLenum) \
    X(void, glDepthMask, GLboolean) \
    X(void, glDepthRange, GLdouble, GLdouble) \
    X(void, glDepthRangef, GLfloat, GLfloat) \
    X(void, glColorMask, GLboolean, GLboolean, GLboolean, GLboolean) \
    X(void, glColorMaski, GLuint, GLboolean, GLboolean, GLboolean, GLboolean) \
    X(void, glBlendFunc, GLenum, GLenum) \
    X(void, glBlendFuncSeparate, GLenum, GLenum, GLenum, GLenum) \
    X(void, glBlendFunci, GLuint, GLenum, GLenum) \
    X(void, glBlendFuncSeparatei, GLuint, GLenum, GLenum, GLenum, GLenum) \
    X(void, glBlendEquation, GLenum) \
    X(void, glBlendEquationSeparate, GLenum, GLenum) \
    X(void, glBlendEquationi, GLuint, GLenum) \
    X(void, glBlendEquationSeparatei, GLuint, GLenum, GLenum) \
    X(void, glBlendColor, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glLogicOp, GLenum) \
    X(void, glCullFace, GLenum) \
    X(void, glFrontFace, GLenum) \
    X(void, glPolygonMode, GLenum, GLenum) \
    X(void, glPolygonOffset, GLfloat, GLfloat) \
    X(void, glPolygonOffsetClamp, GLfloat, GLfloat, GLfloat) \
    X(void, glLineWidth, GLfloat) \
    X(void, glPointSize, GLfloat) \
    X(void, glPointParameteri, GLenum, GLint) \
    X(void, glPointParameterf, GLenum, GLfloat) \
    X(void, glPointParameterfv, GLenum, const GLfloat *) \
    X(void, glHint, GLenum, GLenum) \
    X(void, glStencilFunc, GLenum, GLint, GLuint) \
    X(void, glStencilOp, GLenum, GLenum, GLenum) \
    X(void, glStencilMask, GLuint) \
    X(void, glStencilFuncSeparate, GLenum, GLenum, GLint, GLuint) \
    X(void, glStencilOpSeparate, GLenum, GLenum, GLenum, GLenum) \
    X(void, glStencilMaskSeparate, GLenum, GLuint) \
    X(void, glSampleCoverage, GLfloat, GLboolean) \
    X(void, glSampleMaski, GLuint, GLbitfield) \
    X(void, glMinSampleShading, GLfloat) \
    X(void, glClipControl, GLenum, GLenum) \
    X(void, glDepthRangeIndexed, GLuint, GLdouble, GLdouble) \
    X(void, glDepthRangeArrayv, GLuint, GLsizei, const GLdouble *) \
    X(void, glViewportIndexedf, GLuint, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glViewportIndexedfv, GLuint, const GLfloat *) \
    X(void, glViewportArrayv, GLuint, GLsizei, const GLfloat *) \
    X(void, glScissorIndexed, GLuint, GLint, GLint, GLsizei, GLsizei) \
    X(void, glScissorIndexedv, GLuint, const GLint *) \
    X(void, glScissorArrayv, GLuint, GLsizei, const GLint *) \
    X(void, glReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) \
    X(void, glReadBuffer, GLenum) \
    X(void, glDrawBuffer, GLenum) \
    X(void, glDrawBuffers, GLsizei, const GLenum *) \
    X(void, glFlush) \
    X(void, glFinish) \
    X(void, glPixelStorei, GLenum, GLint) \
    X(void, glPixelStoref, GLenum, GLfloat) \
    X(void, glGenTextures, GLsizei, GLuint *) \
    X(void, glDeleteTextures, GLsizei, const GLuint *) \
    X(void, glBindTexture, GLenum, GLuint) \
    X(void, glActiveTexture, GLenum) \
    X(GLboolean, glIsTexture, GLuint) \
    X(void, glTexImage1D, GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const void *) \
    X(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *) \
    X(void, glTexImage3D, GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *) \
    X(void, glTexSubImage1D, GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const void *) \
    X(void, glTexSubImage2D, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *) \
    X(void, glTexSubImage3D, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *) \
    X(void, glTexStorage1D, GLenum, GLsizei, GLenum, GLsizei) \
    X(void, glTexStorage2D, GLenum, GLsizei, GLenum, GLsizei, GLsizei) \
    X(void, glTexStorage3D, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei) \
    X(void, glTexStorage2DMultisample, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean) \
    X(void, glTexStorage3DMultisample, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean) \
    X(void, glTexImage2DMultisample, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean) \
    X(void, glTexImage3DMultisample, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean) \
    X(void, glTexBuffer, GLenum, GLenum, GLuint) \
    X(void, glTexBufferRange, GLenum, GLenum, GLuint, GLintptr, GLsizeiptr) \
    X(void, glTextureView, GLuint, GLenum, GLuint, GLenum, GLuint, GLuint, GLuint, GLuint) \
    X(void, glCompressedTexImage2D, GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *) \
    X(void, glCompressedTexSubImage2D, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *) \
    X(void, glCompressedTexImage3D, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const void *) \
    X(void, glCompressedTexSubImage3D, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const void *) \
    X(void, glTexParameteri, GLenum, GLenum, GLint) \
    X(void, glTexParameterf, GLenum, GLenum, GLfloat) \
    X(void, glTexParameterfv, GLenum, GLenum, const GLfloat *) \
    X(void, glTexParameteriv, GLenum, GLenum, const GLint *) \
    X(void, glTexParameterIiv, GLenum, GLenum, const GLint *) \
    X(void, glTexParameterIuiv, GLenum, GLenum, const GLuint *) \
    X(void, glGetTexParameteriv, GLenum, GLenum, GLint *) \
    X(void, glGetTexParameterfv, GLenum, GLenum, GLfloat *) \
    X(void, glGetTexLevelParameteriv, GLenum, GLint, GLenum, GLint *) \
    X(void, glGetTexImage, GLenum, GLint, GLenum, GLenum, void *) \
    X(void, glGetCompressedTexImage, GLenum, GLint, void *) \
    X(void, glCopyTexImage2D, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint) \
    X(void, glCopyTexSubImage2D, GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) \
    X(void, glCopyTexSubImage3D, GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) \
    X(void, glCopyImageSubData, GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei) \
    X(void, glClearTexImage, GLuint, GLint, GLenum, GLenum, const void *) \
    X(void, glClearTexSubImage, GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *) \
    X(void, glInvalidateTexImage, GLuint, GLint) \
    X(void, glInvalidateTexSubImage, GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei) \
    X(void, glGenerateMipmap, GLenum) \
    X(void, glGenSamplers, GLsizei, GLuint *) \
    X(void, glDeleteSamplers, GLsizei, const GLuint *) \
    X(void, glBindSampler, GLuint, GLuint) \
    X(void, glSamplerParameteri, GLuint, GLenum, GLint) \
    X(void, glSamplerParameterf, GLuint, GLenum, GLfloat) \
    X(void, glSamplerParameterfv, GLuint, GLenum, const GLfloat *) \
    X(void, glSamplerParameteriv, GLuint, GLenum, const GLint *) \
    X(void, glSamplerParameterIuiv, GLuint, GLenum, const GLuint *) \
    X(void, glBindTextures, GLuint, GLsizei, const GLuint *) \
    X(void, glBindSamplers, GLuint, GLsizei, const GLuint *) \
    X(void, glBindImageTexture, GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum) \
    X(void, glBindImageTextures, GLuint, GLsizei, const GLuint *) \
    X(void, glMemoryBarrier, GLbitfield) \
    X(void, glMemoryBarrierByRegion, GLbitfield) \
    X(void, glTextureBarrier) \
    X(void, glCreateTextures, GLenum, GLsizei, GLuint *) \
    X(void, glTextureStorage2D, GLuint, GLsizei, GLenum, GLsizei, GLsizei) \
    X(void, glTextureStorage3D, GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei) \
    X(void, glTextureSubImage2D, GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *) \
    X(void, glTextureSubImage3D, GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *) \
    X(void, glBindTextureUnit, GLuint, GLuint) \
    X(void, glGenerateTextureMipmap, GLuint) \
    X(void, glTextureParameteri, GLuint, GLenum, GLint) \
    X(void, glTextureParameterf, GLuint, GLenum, GLfloat) \
    X(void, glTextureBuffer, GLuint, GLenum, GLuint) \
    X(void, glTexPageCommitmentARB, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLboolean) \
    X(GLuint64, glGetTextureHandleARB, GLuint) \
    X(GLuint64, glGetTextureSamplerHandleARB, GLuint, GLuint) \
    X(void, glMakeTextureHandleResidentARB, GLuint64) \
    X(void, glMakeTextureHandleNonResidentARB, GLuint64) \
    X(void, glUniformHandleui64ARB, GLint, GLuint64) \
    X(GLuint64, glGetImageHandleARB, GLuint, GLint, GLboolean, GLint, GLenum) \
    X(void, glMakeImageHandleResidentARB, GLuint64, GLenum) \
    X(void, glGenBuffers, GLsizei, GLuint *) \
    X(void, glDeleteBuffers, GLsizei, const GLuint *) \
    X(void, glBindBuffer, GLenum, GLuint) \
    X(void, glBufferData, GLenum, GLsizeiptr, const void *, GLenum) \
    X(void, glBufferSubData, GLenum, GLintptr, GLsizeiptr, const void *) \
    X(void *, glMapBuffer, GLenum, GLenum) \
    X(void *, glMapBufferRange, GLenum, GLintptr, GLsizeiptr, GLbitfield) \
    X(GLboolean, glUnmapBuffer, GLenum) \
    X(void, glFlushMappedBufferRange, GLenum, GLintptr, GLsizeiptr) \
    X(void, glBufferStorage, GLenum, GLsizeiptr, const void *, GLbitfield) \
    X(void, glCopyBufferSubData, GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr) \
    X(void, glGetBufferSubData, GLenum, GLintptr, GLsizeiptr, void *) \
    X(void, glGetBufferParameteriv, GLenum, GLenum, GLint *) \
    X(void, glInvalidateBufferData, GLuint) \
    X(void, glInvalidateBufferSubData, GLuint, GLintptr, GLsizeiptr) \
    X(void, glClearBufferData, GLenum, GLenum, GLenum, GLenum, const void *) \
    X(void, glClearBufferSubData, GLenum, GLenum, GLintptr, GLsizeiptr, GLenum, GLenum, const void *) \
    X(void, glBindBufferBase, GLenum, GLuint, GLuint) \
    X(void, glBindBufferRange, GLenum, GLuint, GLuint, GLintptr, GLsizeiptr) \
    X(void, glBindBuffersBase, GLenum, GLuint, GLsizei, const GLuint *) \
    X(void, glCreateBuffers, GLsizei, GLuint *) \
    X(void, glNamedBufferStorage, GLuint, GLsizeiptr, const void *, GLbitfield) \
    X(void, glNamedBufferData, GLuint, GLsizeiptr, const void *, GLenum) \
    X(void, glNamedBufferSubData, GLuint, GLintptr, GLsizeiptr, const void *) \
    X(void *, glMapNamedBufferRange, GLuint, GLintptr, GLsizeiptr, GLbitfield) \
    X(GLboolean, glUnmapNamedBuffer, GLuint) \
    X(void, glFlushMappedNamedBufferRange, GLuint, GLintptr, GLsizeiptr) \
    X(void, glGenVertexArrays, GLsizei, GLuint *) \
    X(void, glDeleteVertexArrays, GLsizei, const GLuint *) \
    X(void, glBindVertexArray, GLuint) \
    X(void, glCreateVertexArrays, GLsizei, GLuint *) \
    X(void, glVertexArrayVertexBuffer, GLuint, GLuint, GLuint, GLintptr, GLsizei) \
    X(void, glVertexArrayAttribFormat, GLuint, GLuint, GLint, GLenum, GLboolean, GLuint) \
    X(void, glVertexArrayAttribIFormat, GLuint, GLuint, GLint, GLenum, GLuint) \
    X(void, glVertexArrayAttribBinding, GLuint, GLuint, GLuint) \
    X(void, glEnableVertexArrayAttrib, GLuint, GLuint) \
    X(void, glVertexArrayElementBuffer, GLuint, GLuint) \
    X(void, glVertexArrayBindingDivisor, GLuint, GLuint, GLuint) \
    X(void, glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) \
    X(void, glVertexAttribIPointer, GLuint, GLint, GLenum, GLsizei, const void *) \
    X(void, glVertexAttribLPointer, GLuint, GLint, GLenum, GLsizei, const void *) \
    X(void, glEnableVertexAttribArray, GLuint) \
    X(void, glDisableVertexAttribArray, GLuint) \
    X(void, glVertexAttribDivisor, GLuint, GLuint) \
    X(void, glVertexAttribFormat, GLuint, GLint, GLenum, GLboolean, GLuint) \
    X(void, glVertexAttribIFormat, GLuint, GLint, GLenum, GLuint) \
    X(void, glVertexAttribLFormat, GLuint, GLint, GLenum, GLuint) \
    X(void, glBindVertexBuffer, GLuint, GLuint, GLintptr, GLsizei) \
    X(void, glBindVertexBuffers, GLuint, GLsizei, const GLuint *, const GLintptr *, const GLsizei *) \
    X(void, glVertexAttribBinding, GLuint, GLuint) \
    X(void, glVertexBindingDivisor, GLuint, GLuint) \
    X(void, glVertexAttrib1f, GLuint, GLfloat) \
    X(void, glVertexAttrib2f, GLuint, GLfloat, GLfloat) \
    X(void, glVertexAttrib3f, GLuint, GLfloat, GLfloat, GLfloat) \
    X(void, glVertexAttrib4f, GLuint, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glVertexAttrib4fv, GLuint, const GLfloat *) \
    X(void, glVertexAttrib4Nub, GLuint, GLubyte, GLubyte, GLubyte, GLubyte) \
    X(void, glVertexAttribI4i, GLuint, GLint, GLint, GLint, GLint) \
    X(void, glVertexAttribI4ui, GLuint, GLuint, GLuint, GLuint, GLuint) \
    X(void, glVertexAttribL1d, GLuint, GLdouble) \
    X(void, glVertexAttribP1ui, GLuint, GLenum, GLboolean, GLuint) \
    X(void, glVertexAttribP2ui, GLuint, GLenum, GLboolean, GLuint) \
    X(void, glVertexAttribP3ui, GLuint, GLenum, GLboolean, GLuint) \
    X(void, glVertexAttribP4ui, GLuint, GLenum, GLboolean, GLuint) \
    X(GLuint, glCreateShader, GLenum) \
    X(void, glShaderSource, GLuint, GLsizei, const GLchar *const*, const GLint *) \
    X(void, glCompileShader, GLuint) \
    X(void, glGetShaderiv, GLuint, GLenum, GLint *) \
    X(void, glGetShaderInfoLog, GLuint, GLsizei, GLsizei *, GLchar *) \
    X(void, glGetShaderSource, GLuint, GLsizei, GLsizei *, GLchar *) \
    X(void, glDeleteShader, GLuint) \
    X(GLboolean, glIsShader, GLuint) \
    X(void, glDetachShader, GLuint, GLuint) \
    X(void, glGetAttachedShaders, GLuint, GLsizei, GLsizei *, GLuint *) \
    X(GLuint, glCreateProgram) \
    X(void, glAttachShader, GLuint, GLuint) \
    X(void, glLinkProgram, GLuint) \
    X(void, glGetProgramiv, GLuint, GLenum, GLint *) \
    X(void, glGetProgramInfoLog, GLuint, GLsizei, GLsizei *, GLchar *) \
    X(void, glUseProgram, GLuint) \
    X(void, glDeleteProgram, GLuint) \
    X(GLboolean, glIsProgram, GLuint) \
    X(void, glValidateProgram, GLuint) \
    X(GLint, glGetUniformLocation, GLuint, const GLchar *) \
    X(GLint, glGetAttribLocation, GLuint, const GLchar *) \
    X(void, glBindAttribLocation, GLuint, GLuint, const GLchar *) \
    X(void, glBindFragDataLocation, GLuint, GLuint, const GLchar *) \
    X(void, glBindFragDataLocationIndexed, GLuint, GLuint, GLuint, const GLchar *) \
    X(GLint, glGetFragDataLocation, GLuint, const GLchar *) \
    X(GLint, glGetFragDataIndex, GLuint, const GLchar *) \
    X(void, glGetActiveUniform, GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *) \
    X(void, glGetActiveAttrib, GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *) \
    X(void, glGetUniformfv, GLuint, GLint, GLfloat *) \
    X(void, glGetUniformiv, GLuint, GLint, GLint *) \
    X(void, glUniform1i, GLint, GLint) \
    X(void, glUniform2i, GLint, GLint, GLint) \
    X(void, glUniform3i, GLint, GLint, GLint, GLint) \
    X(void, glUniform4i, GLint, GLint, GLint, GLint, GLint) \
    X(void, glUniform1f, GLint, GLfloat) \
    X(void, glUniform2f, GLint, GLfloat, GLfloat) \
    X(void, glUniform3f, GLint, GLfloat, GLfloat, GLfloat) \
    X(void, glUniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glUniform1ui, GLint, GLuint) \
    X(void, glUniform2ui, GLint, GLuint, GLuint) \
    X(void, glUniform3ui, GLint, GLuint, GLuint, GLuint) \
    X(void, glUniform4ui, GLint, GLuint, GLuint, GLuint, GLuint) \
    X(void, glUniform1iv, GLint, GLsizei, const GLint *) \
    X(void, glUniform2iv, GLint, GLsizei, const GLint *) \
    X(void, glUniform3iv, GLint, GLsizei, const GLint *) \
    X(void, glUniform4iv, GLint, GLsizei, const GLint *) \
    X(void, glUniform1fv, GLint, GLsizei, const GLfloat *) \
    X(void, glUniform2fv, GLint, GLsizei, const GLfloat *) \
    X(void, glUniform3fv, GLint, GLsizei, const GLfloat *) \
    X(void, glUniform4fv, GLint, GLsizei, const GLfloat *) \
    X(void, glUniform1uiv, GLint, GLsizei, const GLuint *) \
    X(void, glUniform2uiv, GLint, GLsizei, const GLuint *) \
    X(void, glUniform3uiv, GLint, GLsizei, const GLuint *) \
    X(void, glUniform4uiv, GLint, GLsizei, const GLuint *) \
    X(void, glUniform1d, GLint, GLdouble) \
    X(void, glUniform2d, GLint, GLdouble, GLdouble) \
    X(void, glUniform3d, GLint, GLdouble, GLdouble, GLdouble) \
    X(void, glUniform4d, GLint, GLdouble, GLdouble, GLdouble, GLdouble) \
    X(void, glUniform1dv, GLint, GLsizei, const GLdouble *) \
    X(void, glUniformMatrix2fv, GLint, GLsizei, GLboolean, const GLfloat *) \
    X(void, glUniformMatrix3fv, GLint, GLsizei, GLboolean, const GLfloat *) \
    X(void, glUniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat *) \
    X(void, glUniformMatrix3x2fv, GLint, GLsizei, GLboolean, const GLfloat *) \
    X(void, glUniformMatrix4x3fv, GLint, GLsizei, GLboolean, const GLfloat *) \
    X(void, glUniformMatrix4dv, GLint, GLsizei, GLboolean, const GLdouble *) \
    X(void, glProgramUniform1i, GLuint, GLint, GLint) \
    X(void, glProgramUniform1f, GLuint, GLint, GLfloat) \
    X(void, glProgramUniform4f, GLuint, GLint, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glProgramUniform1fv, GLuint, GLint, GLsizei, const GLfloat *) \
    X(void, glProgramUniform4fv, GLuint, GLint, GLsizei, const GLfloat *) \
    X(void, glProgramUniformMatrix4fv, GLuint, GLint, GLsizei, GLboolean, const GLfloat *) \
    X(void, glProgramUniform1ui, GLuint, GLint, GLuint) \
    X(GLuint, glGetUniformBlockIndex, GLuint, const GLchar *) \
    X(void, glUniformBlockBinding, GLuint, GLuint, GLuint) \
    X(void, glGetActiveUniformBlockiv, GLuint, GLuint, GLenum, GLint *) \
    X(void, glGetActiveUniformBlockName, GLuint, GLuint, GLsizei, GLsizei *, GLchar *) \
    X(void, glGetActiveUniformsiv, GLuint, GLsizei, const GLuint *, GLenum, GLint *) \
    X(void, glGetUniformIndices, GLuint, GLsizei, const GLchar *const*, GLuint *) \
    X(void, glShaderStorageBlockBinding, GLuint, GLuint, GLuint) \
    X(void, glGetProgramInterfaceiv, GLuint, GLenum, GLenum, GLint *) \
    X(GLuint, glGetProgramResourceIndex, GLuint, GLenum, const GLchar *) \
    X(void, glGetProgramResourceiv, GLuint, GLenum, GLuint, GLsizei, const GLenum *, GLsizei, GLsizei *, GLint *) \
    X(void, glGetProgramResourceName, GLuint, GLenum, GLuint, GLsizei, GLsizei *, GLchar *) \
    X(GLint, glGetProgramResourceLocation, GLuint, GLenum, const GLchar *) \
    X(void, glUseProgramStages, GLuint, GLbitfield, GLuint) \
    X(void, glGenProgramPipelines, GLsizei, GLuint *) \
    X(void, glBindProgramPipeline, GLuint) \
    X(void, glDeleteProgramPipelines, GLsizei, const GLuint *) \
    X(void, glProgramParameteri, GLuint, GLenum, GLint) \
    X(GLuint, glCreateShaderProgramv, GLenum, GLsizei, const GLchar *const*) \
    X(void, glActiveShaderProgram, GLuint, GLuint) \
    X(void, glGetProgramPipelineiv, GLuint, GLenum, GLint *) \
    X(void, glGetProgramBinary, GLuint, GLsizei, GLsizei *, GLenum *, void *) \
    X(void, glProgramBinary, GLuint, GLenum, const void *, GLsizei) \
    X(void, glSpecializeShader, GLuint, const GLchar *, GLuint, const GLuint *, const GLuint *) \
    X(void, glShaderBinary, GLsizei, const GLuint *, GLenum, const void *, GLsizei) \
    X(GLint, glGetSubroutineUniformLocation, GLuint, GLenum, const GLchar *) \
    X(GLuint, glGetSubroutineIndex, GLuint, GLenum, const GLchar *) \
    X(void, glUniformSubroutinesuiv, GLenum, GLsizei, const GLuint *) \
    X(void, glGetProgramStageiv, GLuint, GLenum, GLenum, GLint *) \
    X(void, glDispatchCompute, GLuint, GLuint, GLuint) \
    X(void, glDispatchComputeIndirect, GLintptr) \
    X(void, glDrawArrays, GLenum, GLint, GLsizei) \
    X(void, glDrawElements, GLenum, GLsizei, GLenum, const void *) \
    X(void, glDrawRangeElements, GLenum, GLuint, GLuint, GLsizei, GLenum, const void *) \
    X(void, glDrawArraysInstanced, GLenum, GLint, GLsizei, GLsizei) \
    X(void, glDrawArraysInstancedBaseInstance, GLenum, GLint, GLsizei, GLsizei, GLuint) \
    X(void, glDrawElementsInstanced, GLenum, GLsizei, GLenum, const void *, GLsizei) \
    X(void, glDrawElementsBaseVertex, GLenum, GLsizei, GLenum, const void *, GLint) \
    X(void, glDrawRangeElementsBaseVertex, GLenum, GLuint, GLuint, GLsizei, GLenum, const void *, GLint) \
    X(void, glDrawElementsInstancedBaseVertex, GLenum, GLsizei, GLenum, const void *, GLsizei, GLint) \
    X(void, glDrawElementsInstancedBaseInstance, GLenum, GLsizei, GLenum, const void *, GLsizei, GLuint) \
    X(void, glDrawElementsInstancedBaseVertexBaseInstance, GLenum, GLsizei, GLenum, const void *, GLsizei, GLint, GLuint) \
    X(void, glMultiDrawArrays, GLenum, const GLint *, const GLsizei *, GLsizei) \
    X(void, glMultiDrawElements, GLenum, const GLsizei *, GLenum, const void *const*, GLsizei) \
    X(void, glMultiDrawElementsBaseVertex, GLenum, const GLsizei *, GLenum, const void *const*, GLsizei, const GLint *) \
    X(void, glDrawArraysIndirect, GLenum, const void *) \
    X(void, glDrawElementsIndirect, GLenum, GLenum, const void *) \
    X(void, glMultiDrawArraysIndirect, GLenum, const void *, GLsizei, GLsizei) \
    X(void, glMultiDrawElementsIndirect, GLenum, GLenum, const void *, GLsizei, GLsizei) \
    X(void, glMultiDrawArraysIndirectCount, GLenum, const void *, GLintptr, GLsizei, GLsizei) \
    X(void, glMultiDrawElementsIndirectCount, GLenum, GLenum, const void *, GLintptr, GLsizei, GLsizei) \
    X(void, glPrimitiveRestartIndex, GLuint) \
    X(void, glProvokingVertex, GLenum) \
    X(void, glPatchParameteri, GLenum, GLint) \
    X(void, glPatchParameterfv, GLenum, const GLfloat *) \
    X(void, glGenTransformFeedbacks, GLsizei, GLuint *) \
    X(void, glBindTransformFeedback, GLenum, GLuint) \
    X(void, glDeleteTransformFeedbacks, GLsizei, const GLuint *) \
    X(void, glBeginTransformFeedback, GLenum) \
    X(void, glEndTransformFeedback) \
    X(void, glPauseTransformFeedback) \
    X(void, glResumeTransformFeedback) \
    X(void, glTransformFeedbackVaryings, GLuint, GLsizei, const GLchar *const*, GLenum) \
    X(void, glGetTransformFeedbackVarying, GLuint, GLuint, GLsizei, GLsizei *, GLsizei *, GLenum *, GLchar *) \
    X(void, glDrawTransformFeedback, GLenum, GLuint) \
    X(void, glDrawTransformFeedbackStream, GLenum, GLuint, GLuint) \
    X(void, glDrawTransformFeedbackInstanced, GLenum, GLuint, GLsizei) \
    X(void, glDrawTransformFeedbackStreamInstanced, GLenum, GLuint, GLuint, GLsizei) \
    X(void, glGenFramebuffers, GLsizei, GLuint *) \
    X(void, glDeleteFramebuffers, GLsizei, const GLuint *) \
    X(void, glBindFramebuffer, GLenum, GLuint) \
    X(void, glFramebufferTexture, GLenum, GLenum, GLuint, GLint) \
    X(void, glFramebufferTexture1D, GLenum, GLenum, GLenum, GLuint, GLint) \
    X(void, glFramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint) \
    X(void, glFramebufferTexture3D, GLenum, GLenum, GLenum, GLuint, GLint, GLint) \
    X(void, glFramebufferTextureLayer, GLenum, GLenum, GLuint, GLint, GLint) \
    X(void, glFramebufferRenderbuffer, GLenum, GLenum, GLenum, GLuint) \
    X(void, glFramebufferParameteri, GLenum, GLenum, GLint) \
    X(GLenum, glCheckFramebufferStatus, GLenum) \
    X(void, glGenRenderbuffers, GLsizei, GLuint *) \
    X(void, glDeleteRenderbuffers, GLsizei, const GLuint *) \
    X(void, glBindRenderbuffer, GLenum, GLuint) \
    X(void, glRenderbufferStorage, GLenum, GLenum, GLsizei, GLsizei) \
    X(void, glRenderbufferStorageMultisample, GLenum, GLsizei, GLenum, GLsizei, GLsizei) \
    X(void, glGetRenderbufferParameteriv, GLenum, GLenum, GLint *) \
    X(void, glGetFramebufferAttachmentParameteriv, GLenum, GLenum, GLenum, GLint *) \
    X(void, glGetFramebufferParameteriv, GLenum, GLenum, GLint *) \
    X(void, glBlitFramebuffer, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) \
    X(void, glInvalidateFramebuffer, GLenum, GLsizei, const GLenum *) \
    X(void, glInvalidateSubFramebuffer, GLenum, GLsizei, const GLenum *, GLint, GLint, GLsizei, GLsizei) \
    X(void, glClearBufferfv, GLenum, GLint, const GLfloat *) \
    X(void, glClearBufferiv, GLenum, GLint, const GLint *) \
    X(void, glClearBufferuiv, GLenum, GLint, const GLuint *) \
    X(void, glClearBufferfi, GLenum, GLint, GLfloat, GLint) \
    X(void, glGetInternalformativ, GLenum, GLenum, GLenum, GLsizei, GLint *) \
    X(void, glGetInternalformati64v, GLenum, GLenum, GLenum, GLsizei, GLint64 *) \
    X(void, glCreateFramebuffers, GLsizei, GLuint *) \
    X(void, glNamedFramebufferTexture, GLuint, GLenum, GLuint, GLint) \
    X(void, glNamedFramebufferTextureLayer, GLuint, GLenum, GLuint, GLint, GLint) \
    X(void, glNamedFramebufferDrawBuffers, GLuint, GLsizei, const GLenum *) \
    X(void, glNamedFramebufferRenderbuffer, GLuint, GLenum, GLenum, GLuint) \
    X(void, glBlitNamedFramebuffer, GLuint, GLuint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) \
    X(void, glClearNamedFramebufferfv, GLuint, GLenum, GLint, const GLfloat *) \
    X(GLenum, glCheckNamedFramebufferStatus, GLuint, GLenum) \
    X(void, glCreateRenderbuffers, GLsizei, GLuint *) \
    X(void, glNamedRenderbufferStorage, GLuint, GLenum, GLsizei, GLsizei) \
    X(void, glNamedRenderbufferStorageMultisample, GLuint, GLsizei, GLenum, GLsizei, GLsizei) \
    X(void, glNamedFramebufferReadBuffer, GLuint, GLenum) \
    X(void, glReadnPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLsizei, void *) \
    X(void, glGenQueries, GLsizei, GLuint *) \
    X(void, glDeleteQueries, GLsizei, const GLuint *) \
    X(void, glBeginQuery, GLenum, GLuint) \
    X(void, glEndQuery, GLenum) \
    X(void, glGetQueryObjectuiv, GLuint, GLenum, GLuint *) \
    X(void, glGetQueryObjectiv, GLuint, GLenum, GLint *) \
    X(void, glGetQueryObjectui64v, GLuint, GLenum, GLuint64 *) \
    X(void, glQueryCounter, GLuint, GLenum) \
    X(void, glBeginQueryIndexed, GLenum, GLuint, GLuint) \
    X(void, glEndQueryIndexed, GLenum, GLuint) \
    X(void, glBeginConditionalRender, GLuint, GLenum) \
    X(void, glEndConditionalRender) \
    X(void, glGetQueryiv, GLenum, GLenum, GLint *) \
    X(GLsync, glFenceSync, GLenum, GLbitfield) \
    X(GLenum, glClientWaitSync, GLsync, GLbitfield, GLuint64) \
    X(void, glWaitSync, GLsync, GLbitfield, GLuint64) \
    X(void, glDeleteSync, GLsync) \
    X(GLboolean, glIsSync, GLsync) \
    X(void, glDebugMessageCallback, GLDEBUGPROC, const void *) \
    X(void, glDebugMessageControl, GLenum, GLenum, GLenum, GLsizei, const GLuint *, GLboolean) \
    X(void, glPushDebugGroup, GLenum, GLuint, GLsizei, const GLchar *) \
    X(void, glPopDebugGroup) \
    X(void, glObjectLabel, GLenum, GLuint, GLsizei, const GLchar *) \
    X(void, glGetUnsignedBytevEXT, GLenum, GLubyte *) \
    X(void, glBegin, GLenum) \
    X(void, glEnd) \
    X(void, glVertex2f, GLfloat, GLfloat) \
    X(void, glVertex3f, GLfloat, GLfloat, GLfloat) \
    X(void, glVertex4f, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glVertex3fv, const GLfloat *) \
    X(void, glVertex2i, GLint, GLint) \
    X(void, glColor3f, GLfloat, GLfloat, GLfloat) \
    X(void, glColor4f, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glColor3fv, const GLfloat *) \
    X(void, glColor4fv, const GLfloat *) \
    X(void, glColor4ub, GLubyte, GLubyte, GLubyte, GLubyte) \
    X(void, glColor3ub, GLubyte, GLubyte, GLubyte) \
    X(void, glNormal3f, GLfloat, GLfloat, GLfloat) \
    X(void, glNormal3fv, const GLfloat *) \
    X(void, glTexCoord2f, GLfloat, GLfloat) \
    X(void, glTexCoord3f, GLfloat, GLfloat, GLfloat) \
    X(void, glTexCoord4f, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glTexCoord2fv, const GLfloat *) \
    X(void, glMultiTexCoord2f, GLenum, GLfloat, GLfloat) \
    X(void, glMultiTexCoord3f, GLenum, GLfloat, GLfloat, GLfloat) \
    X(void, glMultiTexCoord4f, GLenum, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glFogCoordf, GLfloat) \
    X(void, glSecondaryColor3f, GLfloat, GLfloat, GLfloat) \
    X(void, glEdgeFlag, GLboolean) \
    X(void, glIndexi, GLint) \
    X(void, glArrayElement, GLint) \
    X(void, glMatrixMode, GLenum) \
    X(void, glLoadIdentity) \
    X(void, glLoadMatrixf, const GLfloat *) \
    X(void, glMultMatrixf, const GLfloat *) \
    X(void, glPushMatrix) \
    X(void, glPopMatrix) \
    X(void, glOrtho, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) \
    X(void, glFrustum, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) \
    X(void, glTranslatef, GLfloat, GLfloat, GLfloat) \
    X(void, glRotatef, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glScalef, GLfloat, GLfloat, GLfloat) \
    X(void, glLoadTransposeMatrixf, const GLfloat *) \
    X(void, glLightfv, GLenum, GLenum, const GLfloat *) \
    X(void, glLightf, GLenum, GLenum, GLfloat) \
    X(void, glLightModelfv, GLenum, const GLfloat *) \
    X(void, glLightModeli, GLenum, GLint) \
    X(void, glLightModelf, GLenum, GLfloat) \
    X(void, glMaterialfv, GLenum, GLenum, const GLfloat *) \
    X(void, glMaterialf, GLenum, GLenum, GLfloat) \
    X(void, glColorMaterial, GLenum, GLenum) \
    X(void, glShadeModel, GLenum) \
    X(void, glFogf, GLenum, GLfloat) \
    X(void, glFogfv, GLenum, const GLfloat *) \
    X(void, glFogi, GLenum, GLint) \
    X(void, glFogiv, GLenum, const GLint *) \
    X(void, glTexEnvi, GLenum, GLenum, GLint) \
    X(void, glTexEnvf, GLenum, GLenum, GLfloat) \
    X(void, glTexEnvfv, GLenum, GLenum, const GLfloat *) \
    X(void, glTexGeni, GLenum, GLenum, GLint) \
    X(void, glTexGenf, GLenum, GLenum, GLfloat) \
    X(void, glTexGenfv, GLenum, GLenum, const GLfloat *) \
    X(void, glClipPlane, GLenum, const GLdouble *) \
    X(void, glAlphaFunc, GLenum, GLfloat) \
    X(void, glPolygonStipple, const GLubyte *) \
    X(void, glLineStipple, GLint, GLushort) \
    X(void, glNewList, GLuint, GLenum) \
    X(void, glEndList) \
    X(void, glCallList, GLuint) \
    X(void, glCallLists, GLsizei, GLenum, const void *) \
    X(GLuint, glGenLists, GLsizei) \
    X(void, glDeleteLists, GLuint, GLsizei) \
    X(void, glListBase, GLuint) \
    X(GLboolean, glIsList, GLuint) \
    X(void, glVertexPointer, GLint, GLenum, GLsizei, const void *) \
    X(void, glColorPointer, GLint, GLenum, GLsizei, const void *) \
    X(void, glNormalPointer, GLenum, GLsizei, const void *) \
    X(void, glTexCoordPointer, GLint, GLenum, GLsizei, const void *) \
    X(void, glFogCoordPointer, GLenum, GLsizei, const void *) \
    X(void, glSecondaryColorPointer, GLint, GLenum, GLsizei, const void *) \
    X(void, glEdgeFlagPointer, GLsizei, const void *) \
    X(void, glIndexPointer, GLenum, GLsizei, const void *) \
    X(void, glEnableClientState, GLenum) \
    X(void, glDisableClientState, GLenum) \
    X(void, glClientActiveTexture, GLenum) \
    X(void, glInterleavedArrays, GLenum, GLsizei, const void *) \
    X(void, glDrawPixels, GLsizei, GLsizei, GLenum, GLenum, const void *) \
    X(void, glBitmap, GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat, const GLubyte *) \
    X(void, glCopyPixels, GLint, GLint, GLsizei, GLsizei, GLenum) \
    X(void, glRasterPos2f, GLfloat, GLfloat) \
    X(void, glRasterPos3f, GLfloat, GLfloat, GLfloat) \
    X(void, glRasterPos4f, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glPixelZoom, GLfloat, GLfloat) \
    X(void, glPixelTransferf, GLenum, GLfloat) \
    X(void, glPixelTransferi, GLenum, GLint) \
    X(void, glWindowPos2i, GLint, GLint) \
    X(void, glWindowPos2f, GLfloat, GLfloat) \
    X(void, glWindowPos3f, GLfloat, GLfloat, GLfloat) \
    X(void, glAccum, GLenum, GLfloat) \
    X(void, glClearAccum, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glRectf, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glRecti, GLint, GLint, GLint, GLint) \
    X(void, glPushAttrib, GLbitfield) \
    X(void, glPopAttrib) \
    X(void, glPushClientAttrib, GLbitfield) \
    X(void, glPopClientAttrib) \
    X(void, glClearIndex, GLfloat) \
    X(void, glGetLightfv, GLenum, GLenum, GLfloat *) \
    X(void, glGetMaterialfv, GLenum, GLenum, GLfloat *) \
    X(void, glGetTexEnviv, GLenum, GLenum, GLint *) \
    X(void, glGetClipPlane, GLenum, GLdouble *) \
    X(void, glGetPolygonStipple, GLubyte *) \
    X(void, glGenProgramsARB, GLsizei, GLuint *) \
    X(void, glBindProgramARB, GLenum, GLuint) \
    X(void, glDeleteProgramsARB, GLsizei, const GLuint *) \
    X(void, glProgramStringARB, GLenum, GLenum, GLsizei, const void *) \
    X(void, glProgramLocalParameter4fARB, GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glProgramEnvParameter4fARB, GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, glGetProgramivARB, GLenum, GLenum, GLint *) \
    X(void, glMapGrid1f, GLint, GLfloat, GLfloat) \
    X(void, glEvalMesh1, GLenum, GLint, GLint) \
    X(void, glMap1f, GLenum, GLfloat, GLfloat, GLint, GLint, const GLfloat *) \
    X(void, glEvalCoord1f, GLfloat) \
    X(void, glMap2f, GLenum, GLfloat, GLfloat, GLint, GLint, GLfloat, GLfloat, GLint, GLint, const GLfloat *) \
    X(void, glMapGrid2f, GLint, GLfloat, GLfloat, GLint, GLfloat, GLfloat) \
    X(void, glEvalMesh2, GLenum, GLint, GLint, GLint, GLint) \
    X(void, glEvalCoord2f, GLfloat, GLfloat) \
    X(void, glDrawMeshTasksNV, GLuint, GLuint) \
    X(void, glBlendBarrierKHR)
