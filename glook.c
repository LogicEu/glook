/* Copyright (c) 2023 Eugenio Arteaga A.

Permission is hereby granted, free of charge, to any 
person obtaining a copy of this software and associated 
documentation files (the "Software"), to deal in the 
Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to 
permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice 
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*********************  glook.c  *************************/

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
#define GLOOK_FILE_COUNT 8
#define GLOOK_SHADER_COUNT 8
#define GLOOK_INPUT_COUNT 4
#define GLOOK_KEYBOARD_COUNT 1024
#define GLOOK_COMMON_LINE_COUNT 24

#define COLRED  "\033[31m"
#define COLNRM  "\033[0m"
#define COLBLD  "\033[1m"
#define COLOFF  "\033[m"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static const char glook_shader_body[] = GLOOK_GLSL_VERSION
"out vec4 _glookFragColor;\n\n"

"uniform float iTime;\n"
"uniform float iTimeDelta;\n"
"uniform int iFrame;\n"
"uniform float iFrameRate;\n"
"uniform vec4 iDate;\n"
"uniform vec3 iResolution;\n"
"uniform vec4 iMouse;\n"
"uniform sampler2D iChannel0;\n"
"uniform sampler2D iChannel1;\n"
"uniform sampler2D iChannel2;\n"
"uniform sampler2D iChannel3;\n"
"uniform vec3 iChannelResolution[4];\n\n"

"void mainImage(out vec4, in vec2);\n\n"

"void main(void)\n"
"{\n"
"    mainImage(_glookFragColor, gl_FragCoord.xy);\n"
"    _glookFragColor.w = 1.0;\n"
"}\n\n";

static const char glook_shader_string_quad[] = GLOOK_GLSL_VERSION
"layout (location = 0) in vec2 vertCoord;\n\n"

"void main(void)\n"
"{\n"
"    gl_Position = vec4(vertCoord.x, vertCoord.y, 0.0, 1.0);\n"
"}\n";

static const char glook_shader_string_pass[] = GLOOK_GLSL_VERSION 
"out vec4 _glookFragColor;\n\n"

"uniform sampler2D iChannel0;\n\n"

"void main(void)\n"
"{\n"
"    _glookFragColor = texelFetch(iChannel0, ivec2(gl_FragCoord.xy), 0);\n"
"}\n";

static const char glook_shader_string_template[] = 
"void mainImage(out vec4 fragColor, in vec2 fragCoord)\n"
"{\n"
"    vec2 uv = fragCoord / iResolution.xy;\n"
"    vec3 col = vec3(uv.x, uv.y, (cos(iTime) + 1.0) * 0.5);\n"
"    fragColor = vec4(col, 1.0);\n"
"}\n";

static const char glook_shader_string_template_pass[] = 
"void mainImage(out vec4 fragColor, in vec2 fragCoord)\n"
"{\n"
"    vec2 uv = fragCoord / iResolution.xy;\n"
"    vec3 col = texture(iChannel0, uv).xyz;\n"
"    fragColor = vec4(col, 1.0);\n"
"}\n";

struct texture {
    unsigned int id;
    int width;
    int height;
};

struct framebuffer {
    unsigned int fbo;
    struct texture texture;
};

struct input {
    enum input_type { GLOOK_FRAMEBUFFER, GLOOK_TEXTURE } type;
    void* data;
};

struct ulocator {
    unsigned int iTime;
    unsigned int iTimeDelta;
    unsigned int iFrame;
    unsigned int iFrameRate;
    unsigned int iDate;
    unsigned int iMouse;
    unsigned int iResolution;
    unsigned int iChannels[GLOOK_INPUT_COUNT];
    unsigned int iChannelResolution[GLOOK_INPUT_COUNT];
};

struct shader {
    char* fpath;
    unsigned int id;
    int rendered;
    int inputcount;
    struct ulocator locator;
    struct pipeline* pipeline;
    struct input inputs[GLOOK_INPUT_COUNT];
    struct framebuffer framebuffer;
};

struct common {
    char* path;
    char* source;
    size_t length;
    size_t linecount;
};

struct pipeline {
    int count;
    int capacity;
    struct common common;
    struct shader shaders[GLOOK_SHADER_COUNT];
};

static struct glook {
    struct glook_opts {
        unsigned int dperf;
        unsigned int limit;
        unsigned int chain;
    } opts;
    GLFWwindow* window;
    unsigned int width, height, vshader;
    int filecount;
    char* filepaths[GLOOK_FILE_COUNT];
    struct pipeline pipeline;
    struct shader shaderpass;
    char keys[GLOOK_KEYBOARD_COUNT];
    char keys_pressed[GLOOK_KEYBOARD_COUNT];
    short int mouse[2];
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

static void glook_compile_error_log_line(
    char* line, const char* filebuf, const char* fpath, const struct common* common)
{
    int i, j, n, linenum;
    line += 7; /* remove 'ERROR: ' part from error string */
    if (sscanf(line, "%d:%d%n", &j, &linenum, &i) < 2) {
        line[0] = tolower(line[0]);
        fprintf(stderr, COLBLD "%s: " COLRED "error: " COLNRM COLBLD "%s\n" COLNRM,
            fpath, line
        );
        return;
    }
    
    line += i + 2;
    line[0] = tolower(line[0]);
    n = linenum < (int)common->linecount;
    fprintf(
        stderr, 
        COLBLD "%s:%d:%d: " COLRED "error: " COLNRM COLBLD "%s\n" COLNRM,
        n ? common->path : fpath, j,
        n ? linenum - GLOOK_COMMON_LINE_COUNT : linenum - (int)common->linecount, line
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

static void glook_compile_error_log(
    char* log, const char* filebuf, const char* fpath, const struct common* common)
{
    static const char* div = "\n";
    char* line = strtok(log, div);
    while (line) {
        glook_compile_error_log_line(line, filebuf, fpath, common);
        line = strtok(NULL, div);
    }
}

/* basic file io */

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

static int glook_file_stat(const char* fpath, size_t* size)
{
    struct stat st;
    if (stat(fpath, &st)) {
        glook_error_log("could not open file: '%s'\n", fpath);
        return EXIT_FAILURE;
    }

    if (!S_ISREG(st.st_mode)) {
        glook_error_log("not a regular file: '%s'\n", fpath);
        return EXIT_FAILURE;
    }

    *size = st.st_size;
    return EXIT_SUCCESS; 
}

static char* glook_file_read(const char* fpath, const size_t offset)
{
    FILE* file;
    char* buffer;
    size_t filelen, shaderlen;
    if (glook_file_stat(fpath, &filelen)) {
        return NULL;
    }
    
    file = fopen(fpath, "rb");
    if (!file) {
        glook_error_log("could not access file: '%s'\n", fpath);
        return NULL;
    }

    shaderlen = filelen + offset;
    buffer = (char*)malloc(shaderlen + 1);
    fread(buffer + offset, 1, filelen, file);
    buffer[shaderlen] = 0;
    fclose(file);
    return buffer;
}

static char* glook_file_shader_read(const char* fpath, const struct common* common)
{
    char* source = glook_file_read(fpath, common->length - 1);
    if (source) {   
        memcpy(source, common->source, common->length - 1);
    }
    
    return source;
}

/* common file handling, pre-append to every shader */

static void glook_common_measure(const char* source, size_t* length, size_t* linecount)
{
    size_t i, j = 0;
    for (i = 0; source[i]; ++i) {
        j += source[i] == '\n';
    }
    *length = i;
    *linecount = j + !j * !!i;
}

static struct common glook_common_create(char* path)
{
    struct common common;
    common.path = NULL;
    common.source = (char*)(size_t)glook_shader_body;
    common.length = sizeof(glook_shader_body) - 1;
    common.linecount = GLOOK_COMMON_LINE_COUNT;
    if (path) {
        char* source = glook_file_shader_read(path, &common);
        if (source) {
            common.path = path;
            common.source = source;
            glook_common_measure(
                common.source, &common.length, &common.linecount
            );
        }
    }
    return common;
}

static void glook_common_free(struct common* common)
{
    if (common->path) {
        free(common->path);
        if (common->source != &glook_shader_body[0]) {
            free(common->source);
        }
    }
}

/* runtime shader compiling */

static void glook_shader_free(struct shader* shader)
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
    
    memset(shader, 0, sizeof(struct shader));
}

static int glook_shader_compile(unsigned int shader, 
    const char* filebuf, const char* fpath, const struct common* common)
{
    int success;
    char log[LOGSIZE];
    glShaderSource(shader, 1, &filebuf, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, LOGSIZE, NULL, log);
        glook_compile_error_log(log, filebuf, fpath, common);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int glook_shader_link(unsigned int shader, unsigned int fshader, 
    const char* filebuf, const char* fpath, const struct common* common)
{
    int success;
    char log[LOGSIZE];
    glAttachShader(shader, glook.vshader);
    glAttachShader(shader, fshader);
    glLinkProgram(shader);
    glGetProgramiv(shader, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader, LOGSIZE, NULL, log);
        glook_compile_error_log(log, filebuf, fpath, common);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static struct ulocator glook_shader_ulocator_create(const unsigned int id)
{
    int i;
    struct tm tm;
    struct ulocator locator;
    time_t t = time(NULL);
    char channelstr[] = "iChannel0", resolutionstr[] = "iChannelResolution[0]";
    
    tm = *localtime(&t);
    locator.iTime = glGetUniformLocation(id, "iTime");
    locator.iTimeDelta = glGetUniformLocation(id, "iTimeDelta");
    locator.iFrame = glGetUniformLocation(id, "iFrame");
    locator.iFrameRate = glGetUniformLocation(id, "iFrameRate");
    locator.iDate = glGetUniformLocation(id, "iDate");
    locator.iResolution = glGetUniformLocation(id, "iResolution");
    locator.iMouse = glGetUniformLocation(id, "iMouse");

    for (i = 0; i < GLOOK_INPUT_COUNT; ++i) {
        char c = i + '0';
        channelstr[8] = c;
        resolutionstr[19] = c;
        locator.iChannels[i] = glGetUniformLocation(id, channelstr);
        locator.iChannelResolution[i] = glGetUniformLocation(id, resolutionstr);
        glUniform1i(locator.iChannels[i], i);
        glUniform3f(
            locator.iChannelResolution[i], 
            (float)(glook.width * GLOOK_SCALE),
            (float)(glook.height * GLOOK_SCALE),
            1.0F
        );
    }

    glUniform3f(
            locator.iResolution,
            (float)(glook.width * GLOOK_SCALE),
            (float)(glook.height * GLOOK_SCALE),
            1.0F
    );

    glUniform4f(
        locator.iDate, 
        (float)(tm.tm_year + 1900),
        (float)(tm.tm_mon + 1),
        (float)tm.tm_mday,
        (float)tm.tm_hour
    );
    
    return locator;
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

    /*
    unsigned int lod = 0, w = glook.width;
    while (w >>= 1) {
        ++lod;
    }
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, lod);
    */

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0
    );
    /*glGenerateMipmap(GL_TEXTURE_2D);*/

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

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fb;
}

static struct shader glook_shader_load_buffer(
    const char* buf, char* fpath, const struct common* common)
{
    unsigned int fshader;
    struct shader shader = {0};
    shader.id = glCreateProgram();
    fshader = glCreateShader(GL_FRAGMENT_SHADER);
    if (glook_shader_compile(fshader, buf, fpath, common) ||
        glook_shader_link(shader.id, fshader, buf, fpath, common)) {
        glook_shader_free(&shader);
    } else {
        glUseProgram(shader.id);
        shader.fpath = fpath;
        shader.locator = glook_shader_ulocator_create(shader.id);
        shader.framebuffer = glook_framebuffer_create();
    }

    glDeleteShader(fshader);
    return shader;
}

static struct shader glook_shader_load(char* fpath, struct common* common)
{
    char* filebuf;
    struct shader shader = {0};
    filebuf = glook_file_shader_read(fpath, common);
    if (filebuf) {
        shader = glook_shader_load_buffer(filebuf, fpath, common);
        free(filebuf);
    }

    return shader;
}

static int glook_shader_reload(struct shader* shader)
{
    struct shader reload = glook_shader_load(shader->fpath, &shader->pipeline->common);
    if (reload.id) {
        reload.inputcount = shader->inputcount;
        memcpy(reload.inputs, shader->inputs, sizeof(reload.inputs));
        reload.pipeline = shader->pipeline;
        shader->fpath = NULL;
        glook_shader_free(shader);
        *shader = reload;
    }
    return !reload.id;
}

/* pipeline shader utils */

static struct shader* glook_pipeline_head(struct pipeline* pipeline)
{
    return pipeline->shaders + MIN(pipeline->count - 1, (int)glook.opts.limit);
}

static struct texture* glook_shader_input_texture(struct input input)
{
    switch (input.type) {
        case GLOOK_FRAMEBUFFER:
            return &((struct shader*)input.data)->framebuffer.texture;
        case GLOOK_TEXTURE: break;
    }

    glook_error_log("invalid input type with value: %d\n", input.type);
    return NULL;
}

static void glook_shader_render_self(struct shader* shader)
{
    /*int w = glook.width * GLOOK_SCALE, h = glook.height * GLOOK_SCALE;*/
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shader->framebuffer.texture.id);
    glBindFramebuffer(GL_FRAMEBUFFER, glook.shaderpass.framebuffer.fbo);
    glUseProgram(glook.shaderpass.id);
    glClear(GL_COLOR_BUFFER_BIT);
    /*glBindFramebuffer(GL_READ_FRAMEBUFFER, shader->framebuffer.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, glook.shaderpass.framebuffer.fbo);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, glook.shaderpass.framebuffer.fbo);*/
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /*
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, glook.shaderpass.framebuffer.texture.id);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    */
}

static void glook_shader_render(
    struct shader* shader, int frame, float t, float dt, float fps, float* mouse)
{
    int i, self = -1;
    for (i = 0; i < shader->inputcount; ++i) {
        if (shader->inputs[i].type == GLOOK_FRAMEBUFFER) {
            struct shader* inshader = shader->inputs[i].data;
            if (!inshader->rendered) {
                if (inshader == shader) {
                    glook_shader_render_self(inshader);
                    self = i;
                } else {
                    glook_shader_render(inshader, frame, t, dt, fps, mouse);
                }
            }
        }
    }

    if (shader != glook_pipeline_head(shader->pipeline)) {
        glBindFramebuffer(GL_FRAMEBUFFER, shader->framebuffer.fbo);
    }

    for (i = 0; i < shader->inputcount; ++i) {
        struct texture* texture = glook_shader_input_texture(shader->inputs[i]);
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(
                GL_TEXTURE_2D,
                self != i ? texture->id : glook.shaderpass.framebuffer.texture.id
        );
    }

    glUseProgram(shader->id);
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform1f(shader->locator.iTime, t);
    glUniform1f(shader->locator.iTimeDelta, dt);
    glUniform1i(shader->locator.iFrame, frame);
    glUniform1f(shader->locator.iFrameRate, fps);
    glUniform4f(shader->locator.iMouse, mouse[0], mouse[1], mouse[2], mouse[3]);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /*
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shader->framebuffer.texture.id);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    */
    ++shader->rendered;
}

/* pipeline and shader arrays */

static struct input glook_shader_input(void *data)
{
    struct input input;
    input.type = GLOOK_FRAMEBUFFER;
    input.data = data;
    return input;
}

static int glook_input_parse(char* fpath, char** path, char* inputs)
{
    static const char* div = ";:,";
    char* tok;
    int inputcount = 0;
    *path = strtok(fpath, div);
    while ((tok = strtok(NULL, div))) {
        if (inputcount >= GLOOK_INPUT_COUNT) {
            glook_error_log(
                "cannot link to more than %d inputs\n", GLOOK_INPUT_COUNT
            );
            break;
        }
    
        while (*tok) {
            if (*tok < '0' || *tok > GLOOK_SHADER_COUNT + '0') {
                 glook_error_log(
                    "invalid input channel %d: must be in range (0 - %d)\n", 
                    *tok, GLOOK_SHADER_COUNT
                );
            } else {
                inputs[inputcount++] = *tok;
            }
            ++tok;
        }
    }

    return inputcount;
}

static int glook_shader_input_connect(
    struct shader* shader, const int index, const char* inputs, int inputcount)
{
    int i, self = 0;
    struct pipeline* pipeline = shader->pipeline;
    if (inputcount) {
        for (i = 0; i < inputcount; ++i) {
            int n = inputs[i] - '0';
            shader->inputs[i] = glook_shader_input(pipeline->shaders + n);
            self += (index == n);
        }
    } else if (glook.opts.chain && index) {
        i = 1;
        shader->inputs[0] = glook_shader_input(pipeline->shaders + index - 1);
    } else {
        for (i = 0; i < index; ++i) {
            shader->inputs[i] = glook_shader_input(pipeline->shaders + i);
        }
    }

    if (self && !glook.shaderpass.id) {
        glook.shaderpass = glook_shader_load_buffer(
            glook_shader_string_pass, NULL, NULL
        );
    }

    return i;
}

static int glook_pipeline_push(struct pipeline* pipeline, char* fpath)
{
    int inputcount;
    struct shader shader;
    char *path, inputs[GLOOK_INPUT_COUNT] = {0}; 
    if (pipeline->count >= GLOOK_SHADER_COUNT) {
        glook_error_log(
            "cannot pipeline more than %d shaders at once\n", GLOOK_SHADER_COUNT
        );
        return EXIT_FAILURE;
    }

    inputcount = glook_input_parse(fpath, &path, inputs);
    shader = glook_shader_load(path, &pipeline->common);
    if (shader.id) {
        shader.pipeline = pipeline;
        shader.inputcount = glook_shader_input_connect(
            &shader, pipeline->count, inputs, inputcount
        );
        pipeline->shaders[pipeline->count++] = shader;
    }

    return !shader.id * 2;
}

static int glook_shader_pipeline_load(
    struct pipeline* pipeline, char* commonpath)
{
    int i, err = 0;
    pipeline->common = glook_common_create(commonpath);
    for (i = 0; i < glook.filecount; ++i) {
        if (glook_pipeline_push(pipeline, glook.filepaths[i])) {
            free(glook.filepaths[i]);
            ++err;
        } 
        glook.filepaths[i] = NULL;
    }
    glook.filecount = 0;
    return err;
}

static int glook_shader_pipeline_reload(struct pipeline* pipeline)
{
    int i, err = 0;
    for (i = 0; i < pipeline->count; ++i) {
        err += glook_shader_reload(pipeline->shaders + i);
    }
    return err;
}

static void glook_shader_pipeline_clear(struct pipeline* pipeline)
{
    int i;
    for (i = 0; i < pipeline->count; ++i) {
        pipeline->shaders[i].rendered = 0;
    }
}

static void glook_shader_pipeline_free(struct pipeline* pipeline)
{
    int i;
    for (i = 0; i < pipeline->count; ++i) {
        glook_shader_free(pipeline->shaders + i);
    }
    
    glook_common_free(&pipeline->common);
    memset(pipeline, 0, sizeof(struct pipeline));
}

static void glook_shader_pipeline_render(
    struct pipeline* pipeline, int frame, float t, float dt, float* mouse)
{
    glook_shader_render(glook_pipeline_head(pipeline), frame, t, dt, 1.0 / dt, mouse);
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
    int pressed = glook_mouse_pressed(GLFW_MOUSE_BUTTON_LEFT);
    int down = glook_mouse_down(GLFW_MOUSE_BUTTON_LEFT);
    mouse[2] = (float)((down * 2 - 1) * mouse[0]);
    mouse[3] = (float)((pressed * 2 - 1) * mouse[1]);
    if (down) {
        glook_mouse_pos(mouse, mouse + 1);
    }
}

/* keyboard control functions */

static unsigned int glook_key_down(unsigned int key)
{
    return glook.keys[key];
}

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

static void glook_file_drop(struct pipeline* pipeline)
{
    int i;
    for (i = 0; i < glook.filecount; ++i) {
        if (glook_pipeline_push(pipeline, glook.filepaths[i]) == EXIT_FAILURE) {
            free(pipeline->shaders[pipeline->count - 1].fpath);
            pipeline->shaders[pipeline->count - 1].fpath = glook.filepaths[i];
        }
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
    glook_buffer_quad_create();
    
    glook.window = window;
    glook.vshader = glCreateShader(GL_VERTEX_SHADER);
    glook_shader_compile(glook.vshader, glook_shader_string_quad, NULL, NULL);
    return EXIT_SUCCESS;
}

/* main glook utilities and abstractions */

static float glook_time(void)
{
    return (float)glfwGetTime();
}

static int glook_clear(void)
{
    glook_shader_pipeline_clear(&glook.pipeline);
    glfwSwapBuffers(glook.window);
    glfwPollEvents();
    return !glfwWindowShouldClose(glook.window);
}

static void glook_deinit(void)
{ 
    glook_filepaths_free();
    glook_shader_pipeline_free(&glook.pipeline);
    if (glook.vshader) {
        glDeleteShader(glook.vshader);
    }

    glook_shader_free(&glook.shaderpass);
    glfwTerminate();
}

static int glook_init(int width, int height, int fullscreen, char* commonpath)
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

    glook_shader_pipeline_load(&glook.pipeline, commonpath);
    if (!glook.pipeline.count) {
        glook_error_log("could not succesfully compile any shader\n");
        glook_deinit();
        return EXIT_FAILURE;
    }

    glook.opts.limit = GLOOK_SHADER_COUNT - 1;
    return EXIT_SUCCESS;
}

static void glook_run(void)
{
    unsigned int i, frame = 0, reload = 0, pause = 0;
    float mouse[4], t = 0.0F, dt = 1.0F, T = 0.0F, tzero = 0.0F, pt = 0.0F;
    
    while (glook_clear()) {
        if (glook_key_pressed(GLFW_KEY_ESCAPE)) {
            break;
        }
        if (glook_key_pressed(GLFW_KEY_R)) {
            ++reload; 
        }
        if (glook_key_pressed(GLFW_KEY_T)) {
            tzero = t;
            frame = 0;
        }
        if (glook_key_pressed(GLFW_KEY_SPACE)) {
            pause = !pause;
        }
        if (glook.pipeline.count > 1 &&
            glook_key_down(GLFW_KEY_LEFT_SHIFT) && glook_key_pressed(GLFW_KEY_P)) {
            glook_shader_free(glook.pipeline.shaders + --glook.pipeline.count);
        }

        for (i = 0; i < GLOOK_SHADER_COUNT; ++i) {
            if (glook_key_pressed(i + 48)) {
                glook.opts.limit = i;
                break;
            }
        }

        if (glook.filepaths[0]) {
            glook_file_drop(&glook.pipeline);
            ++reload;
        }

        if (reload) {
            glook_shader_pipeline_reload(&glook.pipeline);
            tzero = t;
            frame = 0;
            reload = 0;
        }

        if (glook.opts.dperf && !(frame % 2)) {
            glook_log("%d x %d\tfps: %f\tframe: %lu\ttime: %f\r",
                glook.width, glook.height, 1.0F / dt, frame, t
            );
        }

        if (pause) {
            pt = glook_time() - t;
            continue;
        }

        t = glook_time() - pt;
        dt = t - T;
        T = t;
        t -= tzero;

        glook_mouse_get(mouse);
        glook_shader_pipeline_render(&glook.pipeline, frame++, t, dt, mouse);
    }
}

static void glook_usage(void)
{
    glook_log(
        "\n<file>\t\t: read, compile and visualize <file> as GLSL shader\n"
        "-c <file>\t: read file as common header file for all shaders in pipeline\n"
        "-w <uint>\t: set the width of the rendering window to <uint> pixels\n"
        "-h <uint>\t: set the height of the rendering window to <uint> pixels\n"
        "-f\t\t: visualize shader in fullscreen resolution\n"
        "-d\t\t: print runtime information about display and rendering\n"
    );

    fprintf(stdout,
        "-chain\t\t: set structure of shader pipeline to link as a single chain\n"
        "-template\t: write template shader 'template.glsl' at current directory\n"
        "-pass\t\t: write simple pass shader 'pass.glsl' taking input from iChannel0\n"
        "-help\t\t: print this help message\n"
    );
}

int main(int argc, char** argv)
{
    char* commonpath = NULL;
    int i, width = 640, height = 360, fullscreen = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            int c = 0, *p = NULL;
            if (!strcmp(argv[i] + 1, "help")) {
                glook_usage();
                return EXIT_SUCCESS;
            } else if (!strcmp(argv[i] + 1, "template")) {
                glook_file_write("template.glsl", glook_shader_string_template);
                return EXIT_SUCCESS;
            } else if (!strcmp(argv[i] + 1, "pass")) {
                glook_file_write("pass.glsl", glook_shader_string_template_pass);
                return EXIT_SUCCESS;
            } else if (!strcmp(argv[i] + 1, "chain")) {
                ++glook.opts.chain;
            } else if (argv[i][1] == 'w' && !argv[i][2]) {
                p = &width;
            } else if (argv[i][1] == 'h' && !argv[i][2]) {
                p = &height;
            } else if (argv[i][1] == 'f' && !argv[i][2]) {
                ++fullscreen;
            } else if (argv[i][1] == 'd' && !argv[i][2]) {
                ++glook.opts.dperf;
            } else if (argv[i][1] == 'c' && !argv[i][2]) {
                ++c;
            } else {
                glook_error_log("unknown argument: '%s'\n", argv[i]);
            }

            if (p || c) {
                if (i + 1 >= argc) {
                    glook_error_log(
                        "argument to '%s' is missing (expected 1 value)\n", 
                        argv[i]
                    );
                } else if (c) {
                    if (commonpath) {
                        glook_error_log(
                            "cannot include more than 1 file with the '%s' option\n",
                            argv[i]
                        );
                    } else commonpath = glook_strdup(argv[++i]);
                } else {
                    *p = atoi(argv[++i]);
                }
            }
        } else glook_filepaths_push(argv[i]);
    }

    if (glook_init(width, height, fullscreen, commonpath)) {
        return EXIT_FAILURE;
    }

    glook_run(); 
    glook_deinit();
    return EXIT_SUCCESS;
}

