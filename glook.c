#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#ifndef __APPLE__
    #define GLOOK_SCALE 1
    #define GLOOK_GLSL_VERSION "#version 300 es\nprecision mediump float;\n\n"
    #include <GL/glew.h>
#else
    #define GLOOK_SCALE 2
    #define GLOOK_GLSL_VERSION "#version 330 core\n\n"
    #define GL_SILENCE_DEPRECATION
    #define GLFW_INCLUDE_GLCOREARB
#endif

#include <GLFW/glfw3.h>

#define BUFSIZE 1024
#define LOGSIZE 512
#define GLOOK_FILE_COUNT 5
#define GLOOK_SHADER_COUNT 4
#define GLOOK_INPUT_COUNT 4
#define GLOOK_KEYBOARD_COUNT 1024

#define COLRED  "\033[31m"
#define COLNRM  "\033[0m"
#define COLBLD  "\033[1m"
#define COLOFF  "\033[m"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static const char glook_shader_body[] = GLOOK_GLSL_VERSION
"out vec4 _glookFragColor;\n\n"

"uniform float iTime;\n"
"uniform float iTimeDelta;\n"
"uniform float iFrame;\n"

"uniform float iFrameRate;\n"
"uniform vec4 iDate;\n"
"uniform vec3 iResolution;\n"
"uniform vec4 iMouse;\n"
"uniform sampler2D iChannel0;\n"
"uniform sampler2D iChannel1;\n"
"uniform sampler2D iChannel2;\n"
"uniform sampler2D iChannel3;\n\n"

"void mainImage(out vec4, in vec2);\n\n"

"void main(void)\n"
"{\n"
"    mainImage(_glookFragColor, gl_FragCoord.xy);\n"
"}\n\n";

static const char glook_shader_string_quad[] = GLOOK_GLSL_VERSION
"layout (location = 0) in vec2 vertCoord;\n"

"void main(void)\n"
"{\n"
"    gl_Position = vec4(vertCoord.x, vertCoord.y, 0.0, 1.0);\n"
"}\n";

static const char glook_shader_string_template[] = 
"void mainImage(out vec4 fragColor, in vec2 fragCoord)\n"
"{\n"
"    vec2 uv = fragCoord / iResolution.xy;\n"
"    vec3 color = vec3(uv.x, uv.y, (cos(iTime) + 1.0) * 0.5);\n"
"    fragColor = vec4(color, 1.0);\n"
"}\n";

struct texture {
    unsigned int id;
    int width, height;
};

struct framebuffer {
    unsigned int fbo;
    struct texture texture;
};

struct input {
    enum type {
        GLOOK_FRAMEBUFFER,
        GLOOK_TEXTURE
    } type;
    void* data;
};

struct GLSLshader {
    char* fpath;
    unsigned int id;
    unsigned int iTime;
    unsigned int iTimeDelta;
    unsigned int iFrame;
    unsigned int iFrameRate;
    unsigned int iDate;
    unsigned int iMouse;
    unsigned int iResolution;
    unsigned int iChannels[GLOOK_INPUT_COUNT];
    int inputcount;
    struct input inputs[GLOOK_INPUT_COUNT];
    struct framebuffer framebuffer;
    int rendered;
};

static struct glook {
    struct glook_opts {
        unsigned int dperf;
        unsigned int limit;
    } opts;
    GLFWwindow* window;
    short int mouse[2];
    int filecount, shadercount;
    unsigned int width, height, vshader;
    char* filepaths[GLOOK_FILE_COUNT];
    struct GLSLshader shaders[GLOOK_SHADER_COUNT];
    char keys[GLOOK_KEYBOARD_COUNT];
    char keys_pressed[GLOOK_KEYBOARD_COUNT];
} glook = {0};

/* common string */

static char* glook_strdup(const char* src)
{
    char* ret;
    int len = strlen(src);
    ret = (char*)malloc(len + 1);
    memcpy(ret, src, len + 1);
    return ret;
}

/* error and logging */

static void glook_log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, COLBLD "glook: " COLNRM);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

static void glook_error_log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, COLBLD "glook: " COLRED "error: " COLNRM COLBLD);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, COLNRM);
    va_end(args);
}

static void glook_shader_error_log_line(
    char* line, const char* filebuf, const char* fpath)
{
    int linenum, i, j = 0;
    line += 7;
    sscanf(line, "%d:%d%n", &j, &linenum, &i);
    line += i + 2;
    line[0] = tolower(line[0]);
    
    fprintf(
        stderr, 
        COLBLD "%s:%d:%d: " COLRED "error: " COLNRM COLBLD "%s\n" COLNRM,
        fpath, j, linenum - 23, line
    );
    
    for (i = 1; i < linenum; ++i) {
        for (++j; filebuf[j] != '\n' && filebuf[j] != '\r'; ++j) {
            if (!filebuf[j]) {
                return;
            }
        }
    }

    for (++j; filebuf[j] && filebuf[j] != '\n' && filebuf[j] != '\r'; ++j) {
        fputc(filebuf[j], stderr);
    }
    fputc('\n', stderr);
}

static void glook_shader_error_log(char* log, const char* filebuf, const char* fpath)
{
    static const char* div = "\n";
    char* line = strtok(log, div);
    while (line) {
        glook_shader_error_log_line(line, filebuf, fpath);
        line = strtok(NULL, div);
    }
}

/* basic file io */

static char* glook_file_read(const char* path)
{
    char* buffer;
    size_t filelen;
    struct stat st;
    FILE* file = fopen(path, "rb");
    if (!file) {
        glook_error_log("could not open file '%s'\n", path);
        return NULL;
    }

    stat(path, &st);
    if (!S_ISREG(st.st_mode)) {
        glook_error_log("not a regular file: '%s'\n", path);
        return NULL; 
    }
    
    filelen = st.st_size;
    buffer = (char*)malloc(filelen + sizeof(glook_shader_body)); 
    memcpy(buffer, glook_shader_body, sizeof(glook_shader_body));
    fread(buffer + sizeof(glook_shader_body) - 1, 1, filelen, file);
    buffer[sizeof(glook_shader_body) + filelen - 1] = 0;
    fclose(file);
    return buffer;
}

static int glook_file_write(const char* fpath, const char* filebuf)
{
    FILE* file = fopen(fpath, "w");
    if (!file) {
        glook_error_log("could not write file '%s'\n", fpath);
        return EXIT_FAILURE;
    }
    
    fprintf(file, "%s", filebuf);
    glook_log("created shader file: %s\n", fpath);
    return fclose(file);
}

/* framebuffer to texture */

static struct texture glook_texture_framebuffer(void)
{
    struct texture texture;
    glGenTextures(1, &texture.id);
    glfwGetFramebufferSize(glook.window, &texture.width, &texture.height);

    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height,
        0, GL_RGBA, GL_UNSIGNED_BYTE, NULL
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0
    );

    return texture;
}

static struct framebuffer glook_framebuffer_create(void)
{
    unsigned int rbo;
    struct framebuffer fb;
    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
    
    fb.texture = glook_texture_framebuffer();
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo); 
    glRenderbufferStorage(
        GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fb.texture.width, fb.texture.height
    );

    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo
    );

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glook_error_log("failed to create framebuffer render object\n");
        fb.fbo = 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fb;
}

/* runtime shader compiling */

static void glook_shader_free(struct GLSLshader* shader)
{
    if (shader->fpath) {
        free(shader->fpath);
    }
    if (shader->id) {
        glDeleteProgram(shader->id);
    }
    if (shader->framebuffer.fbo) {
        glDeleteFramebuffers(1, &shader->framebuffer.fbo);
    }
    memset(shader, 0, sizeof(struct GLSLshader));
}

static int glook_shader_compile(
    unsigned int shader, const char* filebuf, const char* fpath)
{
    int success;
    char log[LOGSIZE];
    glShaderSource(shader, 1, &filebuf, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, LOGSIZE, NULL, log);
        glook_shader_error_log(log, filebuf, fpath);
        glook_error_log("failed to compile shader\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int glook_shader_link(unsigned int shader, unsigned int vshader, 
    unsigned int fshader, const char* filebuf, const char* fpath)
{
    int success;
    char log[LOGSIZE];
    glAttachShader(shader, vshader);
    glAttachShader(shader, fshader);
    glLinkProgram(shader);
    glGetProgramiv(shader, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader, LOGSIZE, NULL, log);
        glook_shader_error_log(log, filebuf, fpath);
        glook_error_log("failed to link shader\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static void glook_shader_uniform_set(struct GLSLshader* shader, char* fpath)
{
    int i;
    struct tm tm;
    time_t t = time(NULL);
    char channelstr[] = "iChannel0";
    tm = *localtime(&t);

    shader->fpath = fpath;
    shader->iTime = glGetUniformLocation(shader->id, "iTime");
    shader->iTimeDelta = glGetUniformLocation(shader->id, "iTimeDelta");
    shader->iFrame = glGetUniformLocation(shader->id, "iFrame");
    shader->iFrameRate = glGetUniformLocation(shader->id, "iFrameRate");
    shader->iDate = glGetUniformLocation(shader->id, "iDate");
    shader->iResolution = glGetUniformLocation(shader->id, "iResolution");
    shader->iMouse = glGetUniformLocation(shader->id, "iMouse");
 
    for (i = 0; i < GLOOK_INPUT_COUNT; ++i) {
        channelstr[8] = i + '0';
        shader->iChannels[i] = glGetUniformLocation(shader->id, channelstr);
        glUniform1i(shader->iChannels[i], i);
    }

    glUniform3f(
            shader->iResolution,
            (float)glook.width * GLOOK_SCALE,
            (float)glook.height * GLOOK_SCALE,
            1.0F
    );

    glUniform4f(
        shader->iDate, 
        (float)(tm.tm_year + 1900),
        (float)(tm.tm_mon + 1),
        (float)tm.tm_mday,
        (float)tm.tm_hour
    );
}

static struct GLSLshader glook_shader_load(char* fpath)
{
    char* filebuf;
    unsigned int fshader;
    struct GLSLshader shader = {0};
    shader.id = glCreateProgram();
    filebuf = glook_file_read(fpath);
    if (!filebuf) {
        glook_shader_free(&shader);
        return shader;
    }

    fshader = glCreateShader(GL_FRAGMENT_SHADER);
    if (glook_shader_compile(fshader, filebuf, fpath)) {
        glook_shader_free(&shader);
        return shader;
    }

    if (glook_shader_link(shader.id, glook.vshader, fshader, filebuf, fpath)) {
        glook_shader_free(&shader);
        return shader;
    }

    free(filebuf);
    glDeleteShader(fshader);
    glUseProgram(shader.id);
    glook_shader_uniform_set(&shader, fpath);
    shader.framebuffer = glook_framebuffer_create();
    return shader;
}

/* basic shader utils */

static struct GLSLshader* glook_shader_head(void)
{
    return glook.shaders + MIN(glook.shadercount - 1, (int)glook.opts.limit);
}

static unsigned int glook_shader_input_id(struct input input)
{
    switch (input.type) {
        case GLOOK_FRAMEBUFFER: 
            return ((struct GLSLshader*)input.data)->framebuffer.texture.id;
        case GLOOK_TEXTURE:
            return ((struct texture*)input.data)->id;
    }
    
    glook_error_log("invalid input type with value: %d\n", input.type);
    return 0;
}

static void glook_shader_render(
    struct GLSLshader* shader, float t, float dt, float f, float fps, float* mouse)
{
    int i;
    for (i = 0; i < shader->inputcount; ++i) {
        if (shader->inputs[i].type == GLOOK_FRAMEBUFFER) {
            struct GLSLshader* inshader = shader->inputs[i].data;
            if (!inshader->rendered) {
                inshader->rendered += shader == inshader;
                glook_shader_render(inshader, t, dt, f, fps, mouse);
            }
        }
    }

    for (i = 0; i < shader->inputcount; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, glook_shader_input_id(shader->inputs[i]));
    }

    if (shader != glook_shader_head()) {
        glBindFramebuffer(GL_FRAMEBUFFER, shader->framebuffer.fbo);
    }

    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shader->id);
    glUniform1f(shader->iTime, t);
    glUniform1f(shader->iTimeDelta, dt);
    glUniform1f(shader->iFrame, f);
    glUniform1f(shader->iFrameRate, fps);
    glUniform4f(shader->iMouse, mouse[0], mouse[1], mouse[2], mouse[3]);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ++shader->rendered;
}

/* pipeline and shader arrays */

static int glook_shader_pipeline_build(
    const int* inputs, int inputcount, const int index)
{
    int i, j = 0;
    if (inputcount) {
        for (i = 0; i < inputcount; ++i) {
            if (inputs[i] < 1 || inputs[i] > 4) {
                glook_error_log(
                    "invalid input channel %d: must be in range (0 - %d)\n", 
                    inputs[i], GLOOK_INPUT_COUNT - 1
                );
                ++j;
                continue;
            }
            glook.shaders[index].inputs[i].type = GLOOK_FRAMEBUFFER;
            glook.shaders[index].inputs[i].data = glook.shaders + inputs[i] - 1;
        }
    } else {
        for (i = 0; i < index; ++i) {
            glook.shaders[index].inputs[i].type = GLOOK_FRAMEBUFFER;
            glook.shaders[index].inputs[i].data = glook.shaders + i;
        }
    }
    return i - j;
}

static int glook_shader_pipeline_load(void)
{
    static const char* div = ";:,";
    char* path, *tok;
    int i, count = 0;
    for (i = 0; i < glook.filecount; ++i) {
        if (count == GLOOK_SHADER_COUNT) {
            glook_error_log(
                "cannot pipeline more than %d shaders at once\n", GLOOK_SHADER_COUNT
            );
            break;
        }

        path = strtok(glook.filepaths[i], div);
        glook.shaders[count] = glook_shader_load(path);
        if (!glook.shaders[count].id) {
            free(glook.filepaths[i]);
        } else {
            int inputs[GLOOK_INPUT_COUNT] = {0}, inputcount = 0;
            while ((tok = strtok(NULL, div))) {
                if (inputcount > GLOOK_INPUT_COUNT) {
                    glook_error_log(
                        "cannot link to more than %d inputs\n", GLOOK_INPUT_COUNT
                    );
                    break;
                }
                inputs[inputcount++] = atoi(tok) + 1;
            }
            glook.shaders[count].inputcount = glook_shader_pipeline_build(
                inputs, inputcount, count
            );
            ++count;
        }
        glook.filepaths[i] = NULL;
    }

    glook.opts.limit = count - 1;
    glook.filecount = 0;
    return count;
}

static int glook_shader_pipeline_reload(void)
{
    int i, count = 0;
    for (i = 0; i < glook.shadercount; ++i) {
        struct GLSLshader shader = glook_shader_load(glook.shaders[i].fpath);
        if (shader.id) {
            shader.inputcount = glook.shaders[i].inputcount;
            memcpy(shader.inputs, glook.shaders[i].inputs, sizeof(shader.inputs));
            glook.shaders[i].fpath = NULL;
            glook_shader_free(glook.shaders + i);
            glook.shaders[i] = shader;
            ++count;
        }
    }
    
    return count;
}

static void glook_shader_pipeline_render(float t, float dt, float f, float* mouse)
{
    glook_shader_render(glook_shader_head(), t, dt, f, 1.0 / dt, mouse);
    glfwSwapBuffers(glook.window);
    glfwPollEvents();
}

static void glook_shader_pipeline_clear(void)
{
    int i;
    for (i = 0; i < glook.shadercount; ++i) {
        glook.shaders[i].rendered = 0;
    }
}

static void glook_shader_pipeline_free(void)
{
    int i;
    for (i = 0; i < glook.shadercount; ++i) {
        glook_shader_free(glook.shaders + i);
    }
}

/* mouse control functions */

static unsigned int glook_mouse_down(unsigned int button)
{
    return glook.mouse[button];
}

static unsigned int glook_mouse_pressed(unsigned int button)
{
    unsigned int held = glfwGetMouseButton(glook.window, button);
    unsigned int pressed = (held == GLFW_PRESS && glook.mouse[button] == GLFW_RELEASE);
    glook.mouse[button] = held;
    return pressed;
}

static void glook_mouse_pos(float* x, float* y)
{
    double mx, my, scale = (float)GLOOK_SCALE;
    glfwGetCursorPos(glook.window, &mx, &my);
    *x = (float)mx * scale;
    *y = (float)(glook.height - (float)my) * scale;
}

static void glook_mouse_get(float* mouse)
{
    glook_mouse_pos(mouse, mouse + 1);
    mouse[2] = (float)glook_mouse_down(GLFW_MOUSE_BUTTON_LEFT);
    mouse[3] = (float)glook_mouse_pressed(GLFW_MOUSE_BUTTON_LEFT);
}

/* keyboard control functions */

static unsigned int glook_key_pressed(unsigned int key)
{
    unsigned int pressed = glook.keys_pressed[key];
    glook.keys_pressed[key] = 0;
    return pressed;
}

static void glook_keyboard_callback(
    GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)mods;
    (void)window;
    (void)scancode;
    glook.keys_pressed[key] = (char)(!glook.keys[key] && !!action);
    glook.keys[key] = (char)!!action;
}

/* file paths and droped files handling */

static void glook_filepaths_free(void)
{
    int i;
    for (i = 0; glook.filepaths[i] && i < GLOOK_FILE_COUNT; ++i) {
        free(glook.filepaths[i]);
        glook.filepaths[i] = NULL;
    }
}

static int glook_filepaths_push(const char* str)
{
    if (glook.filecount == GLOOK_FILE_COUNT) {
        glook_error_log("cannot open more than %d files\n", GLOOK_FILE_COUNT);
        return EXIT_FAILURE;
    }
    glook.filepaths[glook.filecount++] = glook_strdup(str);
    return EXIT_SUCCESS;
}

static void glook_file_drop(void)
{
    int i;
    for (i = 0; i < glook.filecount; ++i) {
        free(glook.shaders[glook.shadercount - 1].fpath);
        glook.shaders[glook.shadercount - 1].fpath = glook.filepaths[i];
        glook.filepaths[i] = NULL;
    }
    glook.filecount = 0;
}

static void glook_file_drop_callback(GLFWwindow* window, int count, const char** paths)
{
    int i;
    (void)window;
    for (i = 0; i < count; i++) {
        if (glook_filepaths_push(paths[i])) {
            break;
        }
    }
}

/* window and OpenGL buffers */

static void glook_window_size_callback(GLFWwindow* window, int width, int height)
{
    (void)window;
    glook.width = width;
    glook.height = height;
    glViewport(0, 0, width * GLOOK_SCALE, height * GLOOK_SCALE);
}

static unsigned int glook_buffer_quad_create(void)
{
    const float vertices[] = {
        1.0f,   1.0f,
        1.0f,   -1.0f,
        -1.0f,  -1.0f,
        -1.0f,  1.0f
    };
    
    const unsigned int indices[] = {
        0,  1,  3,
        1,  2,  3 
    };

    unsigned int id, VBO, EBO;
    glGenVertexArrays(1, &id);
    glBindVertexArray(id);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);  
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    return id;
}

static int glook_window_create(
    const char* title, int width, int height, unsigned int fullscreen)
{
    GLFWwindow* window;
    if (width < 1 || height < 1) {
        glook_error_log("invalid resolution: %d x %d\n", width, height);
        return EXIT_FAILURE;
    }

#ifdef __APPLE__
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);    
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GLFW_TRUE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
#endif

    if (fullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        window = glfwCreateWindow(mode->width, mode->height, title,monitor, NULL);
        glook.width = mode->width;
        glook.height = mode->height;
    } else {
        window = glfwCreateWindow(width, height, title, NULL, NULL);
        glook.width = width;
        glook.height = height;
    }

    if (window == NULL) {
        glook_error_log("could not open a glfw window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetWindowAspectRatio(window, width, height);
    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
    glfwSetWindowSizeCallback(window, glook_window_size_callback);
    glfwSetDropCallback(window, glook_file_drop_callback);
    glfwSetKeyCallback(window, glook_keyboard_callback);

#ifndef __APPLE__
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        glook_error_log("failed to initiate glew\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }
#endif

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glook.window = window;
    glook.vshader = glCreateShader(GL_VERTEX_SHADER);
    glook_shader_compile(glook.vshader, glook_shader_string_quad, NULL);
    return EXIT_SUCCESS;
}

/* main glook utilities and abstractions */

static float glook_time(void)
{
    return (float)glfwGetTime();
}

static int glook_clear(void)
{
    glook_shader_pipeline_clear();
    return !glfwWindowShouldClose(glook.window);
}

static void glook_deinit(void)
{
    glook_filepaths_free();
    glook_shader_pipeline_free();
    if (glook.vshader) {
        glDeleteShader(glook.vshader);
    }
    glfwTerminate();
}

static int glook_init(int width, int height, int fullscreen)
{
    if (!glook.filecount) {
        glook_error_log("no input files\n");
        return EXIT_FAILURE;
    }
    
    if (!glfwInit()) {
        glook_error_log("failed to initiate glfw\n");
        glook_filepaths_free();
        return EXIT_FAILURE;
    }

    if (glook_window_create("glook", width, height, fullscreen)) {
        glook_deinit();
        return EXIT_FAILURE;
    }

    glook_buffer_quad_create();
    glook.shadercount = glook_shader_pipeline_load();
    if (!glook.shadercount) {
        glook_error_log("could not succesfully compile any shader\n");
        glook_deinit();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void glook_run(void)
{
    unsigned int i, frame = 0, reload = 0;
    float mouse[4], f, t, dt, T = 0.0F, tzero = 0.0F;
    while (glook_clear()) {
        t = glook_time();
        dt = t - T;
        T = t;
        t -= tzero;
        f = (float)frame++;

        if (glook_key_pressed(GLFW_KEY_ESCAPE)) {
            break;
        }
        if (glook_key_pressed(GLFW_KEY_R)) {
            ++reload; 
        }
        if (glook_key_pressed(GLFW_KEY_T)) {
            tzero = t;
        }

        for (i = 0; i < GLOOK_INPUT_COUNT; ++i) {
            if (glook_key_pressed(i + 48)) {
                glook.opts.limit = i;
                break;
            }
        }

        if (glook.filepaths[0]) {
            glook_file_drop();
            ++reload;
        }

        if (reload) {
            glook_shader_pipeline_reload();
            tzero = t;
            frame = 0;
            reload = 0;
        }

        if (glook.opts.dperf && !(frame % 2)) {
            glook_log("%d x %d\tfps: %f\tframe: %lu\ttime: %f\r",
                glook.width, glook.height, 1.0F / dt, frame, t
            );
        }

        glook_mouse_get(mouse);
        glook_shader_pipeline_render(t, dt, f, mouse);
    }
}

static void glook_usage(void)
{
    glook_log(
        "\n<file>\t\t: read, compile and visualize <file> as GLSL shader\n"
        "-w <uint>\t: set the width of the rendering window to <uint> pixels\n"
        "-h <uint>\t: set the height of the rendering window to <uint> pixels\n"
        "-f\t\t: visualize shader in fullscreen resolution\n"
        "-d\t\t: print runtime information about display and rendering\n"
        "-template\t: write template shader 'template.glsl' at current directory\n"
        "-help\t\t: print this help message\n"
    );
}

int main(const int argc, const char** argv)
{
    int i, width = 640, height = 360, fullscreen = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            int* p = NULL;
            if (!strcmp(argv[i] + 1, "help")) {
                glook_usage();
                return EXIT_SUCCESS;
            } else if (!strcmp(argv[i] + 1, "template")) {
                glook_file_write("template.glsl", glook_shader_string_template);
                return EXIT_SUCCESS;
            } else if (argv[i][1] == 'w' && !argv[i][2]) {
                p = &width;
            } else if (argv[i][1] == 'h' && !argv[i][2]) {
                p = &height;
            } else if (argv[i][1] == 'f' && !argv[i][2]) {
                ++fullscreen;
            } else if (argv[i][1] == 'd' && !argv[i][2]) {
                ++glook.opts.dperf;
            } else {
                glook_error_log("unknown argument: '%s'\n", argv[i]);
            }

            if (p) {
                if (i + 1 >= argc) {
                    glook_error_log(
                        "argument to '%s' is missing (expected 1 value)\n", 
                        argv[i]
                    );
                } else *p = atoi(argv[++i]);
            }
        } else glook_filepaths_push(argv[i]);
    }

    if (glook_init(width, height, fullscreen)) {
        return EXIT_FAILURE;
    }

    glook_run(); 
    glook_deinit();
    return EXIT_SUCCESS;
}

