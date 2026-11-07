#pragma once

namespace lsw::suites::sanity::gl {
struct ExtensionFunctions {
    const char* extension;
    const char* functions;
};

struct VersionFunctions {
    int version;
    const char* functions;
};

struct VersionLimit {
    int since;
    unsigned name;
    int minimum;
    const char* label;
    int major_only;
};

inline const ExtensionFunctions kGlExtensionFunctions[] = {
    {"GL_ARB_buffer_storage", "glBufferStorage"},
    {"GL_ARB_compute_shader", "glDispatchCompute glDispatchComputeIndirect"},
    {"GL_ARB_direct_state_access", "glCreateBuffers glCreateTextures glNamedBufferData glNamedBufferStorage glNamedBufferSubData glCreateFramebuffers glCreateVertexArrays glBindTextureUnit glTextureStorage2D glTextureSubImage2D glGetTextureImage glCreateSamplers glCreateQueries glCreateTransformFeedbacks glMapNamedBufferRange glUnmapNamedBuffer glNamedFramebufferTexture glClearNamedFramebufferfv glBlitNamedFramebuffer glGenerateTextureMipmap"},
    {"GL_KHR_debug", "glDebugMessageControl glDebugMessageInsert glDebugMessageCallback glGetDebugMessageLog glPushDebugGroup glPopDebugGroup glObjectLabel glGetObjectLabel glObjectPtrLabel glGetObjectPtrLabel"},
    {"GL_ARB_debug_output", "glDebugMessageControlARB glDebugMessageInsertARB glDebugMessageCallbackARB glGetDebugMessageLogARB"},
    {"GL_ARB_sync", "glFenceSync glIsSync glDeleteSync glClientWaitSync glWaitSync glGetInteger64v glGetSynciv"},
    {"GL_ARB_vertex_array_object", "glBindVertexArray glDeleteVertexArrays glGenVertexArrays glIsVertexArray"},
    {"GL_ARB_framebuffer_object", "glIsRenderbuffer glBindRenderbuffer glDeleteRenderbuffers glGenRenderbuffers glRenderbufferStorage glGetRenderbufferParameteriv glIsFramebuffer glBindFramebuffer glDeleteFramebuffers glGenFramebuffers glCheckFramebufferStatus glFramebufferTexture1D glFramebufferTexture2D glFramebufferTexture3D glFramebufferRenderbuffer glGetFramebufferAttachmentParameteriv glGenerateMipmap glBlitFramebuffer glRenderbufferStorageMultisample glFramebufferTextureLayer"},
    {"GL_ARB_map_buffer_range", "glMapBufferRange glFlushMappedBufferRange"},
    {"GL_ARB_copy_buffer", "glCopyBufferSubData"},
    {"GL_ARB_uniform_buffer_object", "glGetUniformIndices glGetActiveUniformsiv glGetActiveUniformName glGetUniformBlockIndex glGetActiveUniformBlockiv glGetActiveUniformBlockName glUniformBlockBinding glBindBufferRange glBindBufferBase glGetIntegeri_v"},
    {"GL_ARB_texture_storage", "glTexStorage1D glTexStorage2D glTexStorage3D"},
    {"GL_ARB_texture_view", "glTextureView"},
    {"GL_ARB_multi_draw_indirect", "glMultiDrawArraysIndirect glMultiDrawElementsIndirect"},
    {"GL_ARB_draw_indirect", "glDrawArraysIndirect glDrawElementsIndirect"},
    {"GL_ARB_clear_buffer_object", "glClearBufferData glClearBufferSubData"},
    {"GL_ARB_shader_image_load_store", "glBindImageTexture glMemoryBarrier"},
    {"GL_ARB_transform_feedback2", "glBindTransformFeedback glDeleteTransformFeedbacks glGenTransformFeedbacks glIsTransformFeedback glPauseTransformFeedback glResumeTransformFeedback glDrawTransformFeedback"},
    {"GL_ARB_timer_query", "glQueryCounter glGetQueryObjecti64v glGetQueryObjectui64v"},
    {"GL_ARB_sampler_objects", "glGenSamplers glDeleteSamplers glIsSampler glBindSampler glSamplerParameteri glSamplerParameterf glGetSamplerParameteriv"},
    {"GL_ARB_instanced_arrays", "glVertexAttribDivisorARB"},
    {"GL_ARB_draw_elements_base_vertex", "glDrawElementsBaseVertex glDrawRangeElementsBaseVertex glDrawElementsInstancedBaseVertex glMultiDrawElementsBaseVertex"},
    {"GL_ARB_tessellation_shader", "glPatchParameteri glPatchParameterfv"},
    {"GL_ARB_robustness", "glGetGraphicsResetStatusARB glReadnPixelsARB glGetnUniformfvARB"},
    {"GL_ARB_texture_multisample", "glTexImage2DMultisample glTexImage3DMultisample glGetMultisamplefv glSampleMaski"},
    {"GL_ARB_texture_buffer_object", "glTexBufferARB"},
    {"GL_ARB_get_texture_sub_image", "glGetTextureSubImage glGetCompressedTextureSubImage"},
    {"GL_ARB_clip_control", "glClipControl"},
    {"GL_ARB_gl_spirv", "glSpecializeShaderARB"},
    {"GL_ARB_separate_shader_objects", "glUseProgramStages glActiveShaderProgram glCreateShaderProgramv glBindProgramPipeline glDeleteProgramPipelines glGenProgramPipelines glIsProgramPipeline glProgramParameteri glProgramUniform1i"},
    {"GL_ARB_program_interface_query", "glGetProgramInterfaceiv glGetProgramResourceIndex glGetProgramResourceName glGetProgramResourceiv glGetProgramResourceLocation"},
    {"GL_ARB_shader_storage_buffer_object", "glShaderStorageBlockBinding"},
    {"GL_ARB_vertex_attrib_binding", "glBindVertexBuffer glVertexAttribFormat glVertexAttribIFormat glVertexAttribLFormat glVertexAttribBinding glVertexBindingDivisor"},
    {"GL_ARB_framebuffer_no_attachments", "glFramebufferParameteri glGetFramebufferParameteriv"},
    {"GL_ARB_invalidate_subdata", "glInvalidateTexSubImage glInvalidateTexImage glInvalidateBufferSubData glInvalidateBufferData glInvalidateFramebuffer glInvalidateSubFramebuffer"},
    {"GL_ARB_ES2_compatibility", "glReleaseShaderCompiler glShaderBinary glGetShaderPrecisionFormat glDepthRangef glClearDepthf"},
    {"GL_ARB_get_program_binary", "glGetProgramBinary glProgramBinary glProgramParameteri"},
    {"GL_EXT_memory_object", "glGetUnsignedBytevEXT glCreateMemoryObjectsEXT glDeleteMemoryObjectsEXT glMemoryObjectParameterivEXT glTexStorageMem2DEXT"},
    {"GL_EXT_memory_object_win32", "glImportMemoryWin32HandleEXT glImportMemoryWin32NameEXT"},
    {"GL_EXT_semaphore", "glGenSemaphoresEXT glDeleteSemaphoresEXT glWaitSemaphoreEXT glSignalSemaphoreEXT"},
    {"GL_EXT_semaphore_win32", "glImportSemaphoreWin32HandleEXT glImportSemaphoreWin32NameEXT"},
};

inline const ExtensionFunctions kWglExtensionFunctions[] = {
    {"WGL_ARB_extensions_string", "wglGetExtensionsStringARB"},
    {"WGL_EXT_extensions_string", "wglGetExtensionsStringEXT"},
    {"WGL_ARB_pixel_format", "wglGetPixelFormatAttribivARB wglGetPixelFormatAttribfvARB wglChoosePixelFormatARB"},
    {"WGL_ARB_create_context", "wglCreateContextAttribsARB"},
    {"WGL_EXT_swap_control", "wglSwapIntervalEXT wglGetSwapIntervalEXT"},
    {"WGL_ARB_make_current_read", "wglMakeContextCurrentARB wglGetCurrentReadDCARB"},
    {"WGL_ARB_pbuffer", "wglCreatePbufferARB wglGetPbufferDCARB wglReleasePbufferDCARB wglDestroyPbufferARB wglQueryPbufferARB"},
    {"WGL_ARB_render_texture", "wglBindTexImageARB wglReleaseTexImageARB wglSetPbufferAttribARB"},
    {"WGL_NV_DX_interop", "wglDXSetResourceShareHandleNV wglDXOpenDeviceNV wglDXCloseDeviceNV wglDXRegisterObjectNV wglDXUnregisterObjectNV wglDXObjectAccessNV wglDXLockObjectsNV wglDXUnlockObjectsNV"},
};

inline const VersionFunctions kCoreFunctions[] = {
    {12, "glTexImage3D glTexSubImage3D glCopyTexSubImage3D glDrawRangeElements"},
    {13, "glActiveTexture glSampleCoverage glCompressedTexImage2D glCompressedTexImage3D glCompressedTexSubImage2D glGetCompressedTexImage"},
    {14, "glBlendFuncSeparate glMultiDrawArrays glMultiDrawElements glPointParameteri glPointParameterf glBlendColor glBlendEquation"},
    {15, "glGenQueries glDeleteQueries glIsQuery glBeginQuery glEndQuery glGetQueryiv glGetQueryObjectiv glGetQueryObjectuiv glBindBuffer glDeleteBuffers glGenBuffers glIsBuffer glBufferData glBufferSubData glGetBufferSubData glMapBuffer glUnmapBuffer glGetBufferParameteriv glGetBufferPointerv"},
    {20, "glBlendEquationSeparate glDrawBuffers glStencilOpSeparate glStencilFuncSeparate glStencilMaskSeparate glAttachShader glBindAttribLocation glCompileShader glCreateProgram glCreateShader glDeleteProgram glDeleteShader glDetachShader glDisableVertexAttribArray glEnableVertexAttribArray glGetActiveAttrib glGetActiveUniform glGetAttachedShaders glGetAttribLocation glGetProgramiv glGetProgramInfoLog glGetShaderiv glGetShaderInfoLog glGetShaderSource glGetUniformLocation glGetUniformfv glGetUniformiv glGetVertexAttribiv glIsProgram glIsShader glLinkProgram glShaderSource glUseProgram glUniform1f glUniform4f glUniform1i glUniform4i glUniformMatrix4fv glValidateProgram glVertexAttribPointer"},
    {21, "glUniformMatrix2x3fv glUniformMatrix3x2fv glUniformMatrix2x4fv glUniformMatrix4x2fv glUniformMatrix3x4fv glUniformMatrix4x3fv"},
    {30, "glColorMaski glGetBooleani_v glGetIntegeri_v glEnablei glDisablei glIsEnabledi glBeginTransformFeedback glEndTransformFeedback glBindBufferRange glBindBufferBase glTransformFeedbackVaryings glGetTransformFeedbackVarying glClampColor glBeginConditionalRender glEndConditionalRender glVertexAttribIPointer glGetVertexAttribIiv glGetVertexAttribIuiv glGetUniformuiv glBindFragDataLocation glGetFragDataLocation glUniform1ui glTexParameterIiv glTexParameterIuiv glGetTexParameterIiv glGetTexParameterIuiv glClearBufferiv glClearBufferuiv glClearBufferfv glClearBufferfi glGetStringi glIsRenderbuffer glBindRenderbuffer glDeleteRenderbuffers glGenRenderbuffers glRenderbufferStorage glGetRenderbufferParameteriv glIsFramebuffer glBindFramebuffer glDeleteFramebuffers glGenFramebuffers glCheckFramebufferStatus glFramebufferTexture2D glFramebufferTexture3D glFramebufferRenderbuffer glGetFramebufferAttachmentParameteriv glGenerateMipmap glBlitFramebuffer glRenderbufferStorageMultisample glFramebufferTextureLayer glMapBufferRange glFlushMappedBufferRange glBindVertexArray glDeleteVertexArrays glGenVertexArrays glIsVertexArray"},
    {31, "glDrawArraysInstanced glDrawElementsInstanced glTexBuffer glPrimitiveRestartIndex glCopyBufferSubData glGetUniformIndices glGetActiveUniformsiv glGetActiveUniformName glGetUniformBlockIndex glGetActiveUniformBlockiv glGetActiveUniformBlockName glUniformBlockBinding"},
    {32, "glDrawElementsBaseVertex glDrawRangeElementsBaseVertex glDrawElementsInstancedBaseVertex glMultiDrawElementsBaseVertex glProvokingVertex glFenceSync glIsSync glDeleteSync glClientWaitSync glWaitSync glGetInteger64v glGetSynciv glGetInteger64i_v glGetBufferParameteri64v glFramebufferTexture glTexImage2DMultisample glTexImage3DMultisample glGetMultisamplefv glSampleMaski"},
    {33, "glBindFragDataLocationIndexed glGetFragDataIndex glGenSamplers glDeleteSamplers glIsSampler glBindSampler glSamplerParameteri glSamplerParameterf glGetSamplerParameteriv glQueryCounter glGetQueryObjecti64v glGetQueryObjectui64v glVertexAttribDivisor glVertexAttribP1ui"},
    {40, "glMinSampleShading glBlendEquationi glBlendFunci glDrawArraysIndirect glDrawElementsIndirect glUniform1d glGetSubroutineUniformLocation glGetSubroutineIndex glUniformSubroutinesuiv glPatchParameteri glPatchParameterfv glBindTransformFeedback glDeleteTransformFeedbacks glGenTransformFeedbacks glIsTransformFeedback glPauseTransformFeedback glResumeTransformFeedback glDrawTransformFeedback glDrawTransformFeedbackStream glBeginQueryIndexed glEndQueryIndexed glGetQueryIndexediv"},
    {41, "glReleaseShaderCompiler glShaderBinary glGetShaderPrecisionFormat glDepthRangef glClearDepthf glGetProgramBinary glProgramBinary glProgramParameteri glUseProgramStages glActiveShaderProgram glCreateShaderProgramv glBindProgramPipeline glDeleteProgramPipelines glGenProgramPipelines glIsProgramPipeline glGetProgramPipelineiv glProgramUniform1i glValidateProgramPipeline glGetProgramPipelineInfoLog glViewportArrayv glViewportIndexedf glScissorArrayv glScissorIndexed glDepthRangeArrayv glDepthRangeIndexed glGetFloati_v glGetDoublei_v"},
    {42, "glDrawArraysInstancedBaseInstance glDrawElementsInstancedBaseInstance glDrawElementsInstancedBaseVertexBaseInstance glGetInternalformativ glGetActiveAtomicCounterBufferiv glBindImageTexture glMemoryBarrier glTexStorage1D glTexStorage2D glTexStorage3D glDrawTransformFeedbackInstanced glDrawTransformFeedbackStreamInstanced"},
    {43, "glClearBufferData glClearBufferSubData glDispatchCompute glDispatchComputeIndirect glCopyImageSubData glFramebufferParameteri glGetFramebufferParameteriv glGetInternalformati64v glInvalidateTexSubImage glInvalidateTexImage glInvalidateBufferSubData glInvalidateBufferData glInvalidateFramebuffer glInvalidateSubFramebuffer glMultiDrawArraysIndirect glMultiDrawElementsIndirect glGetProgramInterfaceiv glGetProgramResourceIndex glGetProgramResourceName glGetProgramResourceiv glGetProgramResourceLocation glGetProgramResourceLocationIndex glShaderStorageBlockBinding glTexBufferRange glTexStorage2DMultisample glTexStorage3DMultisample glTextureView glBindVertexBuffer glVertexAttribFormat glVertexAttribIFormat glVertexAttribLFormat glVertexAttribBinding glVertexBindingDivisor glDebugMessageControl glDebugMessageInsert glDebugMessageCallback glGetDebugMessageLog glPushDebugGroup glPopDebugGroup glObjectLabel glGetObjectLabel glObjectPtrLabel glGetObjectPtrLabel"},
    {44, "glBufferStorage glClearTexImage glClearTexSubImage glBindBuffersBase glBindBuffersRange glBindTextures glBindSamplers glBindImageTextures glBindVertexBuffers"},
    {45, "glClipControl glCreateTransformFeedbacks glTransformFeedbackBufferBase glTransformFeedbackBufferRange glGetTransformFeedbackiv glGetTransformFeedbacki_v glGetTransformFeedbacki64_v glCreateBuffers glNamedBufferStorage glNamedBufferData glNamedBufferSubData glCopyNamedBufferSubData glClearNamedBufferData glClearNamedBufferSubData glMapNamedBuffer glMapNamedBufferRange glUnmapNamedBuffer glFlushMappedNamedBufferRange glGetNamedBufferParameteriv glGetNamedBufferParameteri64v glGetNamedBufferPointerv glGetNamedBufferSubData glCreateFramebuffers glNamedFramebufferRenderbuffer glNamedFramebufferParameteri glNamedFramebufferTexture glNamedFramebufferTextureLayer glNamedFramebufferDrawBuffer glNamedFramebufferDrawBuffers glNamedFramebufferReadBuffer glInvalidateNamedFramebufferData glInvalidateNamedFramebufferSubData glClearNamedFramebufferiv glClearNamedFramebufferuiv glClearNamedFramebufferfv glClearNamedFramebufferfi glBlitNamedFramebuffer glCheckNamedFramebufferStatus glGetNamedFramebufferParameteriv glGetNamedFramebufferAttachmentParameteriv glCreateRenderbuffers glNamedRenderbufferStorage glNamedRenderbufferStorageMultisample glGetNamedRenderbufferParameteriv glCreateTextures glTextureBuffer glTextureBufferRange glTextureStorage1D glTextureStorage2D glTextureStorage3D glTextureStorage2DMultisample glTextureStorage3DMultisample glTextureSubImage1D glTextureSubImage2D glTextureSubImage3D glCompressedTextureSubImage1D glCompressedTextureSubImage2D glCompressedTextureSubImage3D glCopyTextureSubImage1D glCopyTextureSubImage2D glCopyTextureSubImage3D glTextureParameterf glTextureParameterfv glTextureParameteri glTextureParameterIiv glTextureParameterIuiv glTextureParameteriv glGenerateTextureMipmap glBindTextureUnit glGetTextureImage glGetCompressedTextureImage glGetTextureLevelParameterfv glGetTextureLevelParameteriv glGetTextureParameterfv glGetTextureParameterIiv glGetTextureParameterIuiv glGetTextureParameteriv glCreateVertexArrays glDisableVertexArrayAttrib glEnableVertexArrayAttrib glVertexArrayElementBuffer glVertexArrayVertexBuffer glVertexArrayVertexBuffers glVertexArrayAttribBinding glVertexArrayAttribFormat glVertexArrayAttribIFormat glVertexArrayAttribLFormat glVertexArrayBindingDivisor glGetVertexArrayiv glGetVertexArrayIndexediv glGetVertexArrayIndexed64iv glCreateSamplers glCreateProgramPipelines glCreateQueries glGetQueryBufferObjectiv glGetQueryBufferObjectuiv glGetQueryBufferObjecti64v glGetQueryBufferObjectui64v glMemoryBarrierByRegion glGetTextureSubImage glGetCompressedTextureSubImage glGetGraphicsResetStatus glGetnCompressedTexImage glGetnTexImage glGetnUniformdv glGetnUniformfv glGetnUniformiv glGetnUniformuiv glReadnPixels glTextureBarrier"},
    {46, "glSpecializeShader glMultiDrawArraysIndirectCount glMultiDrawElementsIndirectCount glPolygonOffsetClamp"},
};

inline const VersionLimit kLimits[] = {
    {30, 0x0D33, 1024, "GL_MAX_TEXTURE_SIZE", 3},
    {40, 0x0D33, 16384, "GL_MAX_TEXTURE_SIZE", 0},
    {30, 0x8073, 256, "GL_MAX_3D_TEXTURE_SIZE", 3},
    {40, 0x8073, 2048, "GL_MAX_3D_TEXTURE_SIZE", 0},
    {30, 0x851C, 1024, "GL_MAX_CUBE_MAP_TEXTURE_SIZE", 3},
    {40, 0x851C, 16384, "GL_MAX_CUBE_MAP_TEXTURE_SIZE", 0},
    {30, 0x88FF, 256, "GL_MAX_ARRAY_TEXTURE_LAYERS", 3},
    {40, 0x88FF, 2048, "GL_MAX_ARRAY_TEXTURE_LAYERS", 0},
    {30, 0x84E8, 1024, "GL_MAX_RENDERBUFFER_SIZE", 3},
    {40, 0x84E8, 16384, "GL_MAX_RENDERBUFFER_SIZE", 0},
    {30, 0x8824, 8, "GL_MAX_DRAW_BUFFERS", 0},
    {30, 0x8CDF, 8, "GL_MAX_COLOR_ATTACHMENTS", 0},
    {30, 0x8869, 16, "GL_MAX_VERTEX_ATTRIBS", 0},
    {30, 0x8872, 16, "GL_MAX_TEXTURE_IMAGE_UNITS", 0},
    {30, 0x8B4C, 16, "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS", 0},
    {30, 0x8B4D, 48, "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS", 3},
    {40, 0x8B4D, 80, "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS", 0},
    {31, 0x8A2F, 36, "GL_MAX_UNIFORM_BUFFER_BINDINGS", 3},
    {40, 0x8A2F, 72, "GL_MAX_UNIFORM_BUFFER_BINDINGS", 0},
    {31, 0x8A30, 16384, "GL_MAX_UNIFORM_BLOCK_SIZE", 0},
    {30, 0x8B49, 1024, "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS", 0},
    {30, 0x8B4A, 1024, "GL_MAX_VERTEX_UNIFORM_COMPONENTS", 0},
    {30, 0x8D57, 4, "GL_MAX_SAMPLES", 0},
    {32, 0x9122, 64, "GL_MAX_VERTEX_OUTPUT_COMPONENTS", 0},
    {32, 0x9125, 128, "GL_MAX_FRAGMENT_INPUT_COMPONENTS", 0},
    {30, 0x8C8B, 4, "GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS", 0},
    {30, 0x8C8A, 64, "GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS", 0},
    {41, 0x825B, 16, "GL_MAX_VIEWPORTS", 0},
    {40, 0x8E7D, 32, "GL_MAX_PATCH_VERTICES", 0},
    {40, 0x8E7E, 64, "GL_MAX_TESS_GEN_LEVEL", 0},
    {42, 0x8F38, 8, "GL_MAX_IMAGE_UNITS", 0},
    {43, 0x90EB, 1024, "GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS", 0},
    {43, 0x8262, 32768, "GL_MAX_COMPUTE_SHARED_MEMORY_SIZE", 0},
    {43, 0x90DD, 8, "GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS", 0},
    {43, 0x90DE, 134217728, "GL_MAX_SHADER_STORAGE_BLOCK_SIZE", 0},
};
}  // namespace lsw::suites::sanity::gl
