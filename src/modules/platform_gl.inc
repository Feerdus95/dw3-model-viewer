/* A window app has no console: the command-line modes print to the one they
   were started from */
static void attach_console() {
#ifdef _WIN32
    if (_isatty(1) || GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN) return; /* redirected */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
}

/* An error the user has to see: a message box in the window app */
static int fatal(const char *what) {
    fprintf(stderr, "%s\n", what);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DW3 Model Viewer", what, nullptr);
    return 1;
}

/* --- OpenGL 3.3, through SDL (opengl32 only exports 1.1) --- */
#define GL_FUNCS(X)                                                                                              \
    X(glEnable) X(glDisable) X(glViewport) X(glClear) X(glClearColor) X(glGenTextures) X(glDeleteTextures)        \
    X(glBindTexture) X(glTexImage2D) X(glTexParameteri) X(glPixelStorei) X(glReadPixels) X(glDrawArrays)          \
    X(glActiveTexture) X(glBlendFunc) X(glBlendEquation) X(glDepthMask) X(glDepthFunc) X(glCullFace)              \
    X(glFrontFace) X(glPolygonMode) X(glCreateShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv)       \
    X(glGetShaderInfoLog) X(glCreateProgram) X(glAttachShader) X(glLinkProgram) X(glGetProgramiv)                 \
    X(glGetProgramInfoLog) X(glUseProgram) X(glGetUniformLocation) X(glUniform1i) X(glUniform1f) X(glUniform3f)   \
    X(glUniform4f) X(glUniform2f) X(glUniform3fv) X(glUniformMatrix4fv) X(glGenVertexArrays) X(glBindVertexArray) X(glGenBuffers)                \
    X(glDeleteBuffers) X(glDeleteVertexArrays) X(glBindBuffer) X(glBufferData) X(glEnableVertexAttribArray) X(glVertexAttribPointer)     \
    X(glVertexAttribIPointer) X(glGenFramebuffers) X(glBindFramebuffer) X(glFramebufferTexture2D)                \
    X(glGenRenderbuffers) X(glBindRenderbuffer) X(glRenderbufferStorage) X(glFramebufferRenderbuffer) X(glGenerateMipmap) \
    X(glDrawArraysInstanced) X(glVertexAttribDivisor)

#define X(f) static decltype(&f) p_##f;
GL_FUNCS(X)
#undef X
#define GL(f) p_##f

static bool gl_load() {
#define X(f) if (!(p_##f = (decltype(&f))SDL_GL_GetProcAddress(#f))) return false;
    GL_FUNCS(X)
#undef X
    return true;
}

