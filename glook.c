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
#define GLOOK_FILE_COUNT 1
#define GLOOK_SHADER_COUNT 4
#define GLOOK_KEYBOARD_COUNT 1024

#define COLRED  "\033[31m"
#define COLNRM  "\033[0m"
#define COLBLD  "\033[1m"
#define COLOFF  "\033[m"

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
    unsigned int iChannels[4];
};

static struct glook {
    GLFWwindow* window;
    short int mouse[2];
    int filecount, shadercount;
    unsigned int width, height, vshader;
    char keys[GLOOK_KEYBOARD_COUNT];
    char keys_pressed[GLOOK_KEYBOARD_COUNT];
    char* filepaths[GLOOK_FILE_COUNT];
    struct GLSLshader shaders[GLOOK_SHADER_COUNT];
} glook = {0};

static char* glook_strdup(const char* src)
{
    char* ret;
    int len = strlen(src);
    ret = (char*)malloc(len + 1);
    memcpy(ret, src, len + 1);
    return ret;
}

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
        fpath, j, linenum - 15, line
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

static char* glook_file_read(const char* path)
{
    char* buffer;
    size_t filelen;
    FILE* file = fopen(path, "rb");
    if (!file) {
        glook_error_log("could not open file '%s'\n", path);
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    filelen = ftell(file);
    fseek(file, 0, SEEK_SET);
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
    struct tm tm;
    time_t t = time(NULL);
    tm = *localtime(&t);

    shader->fpath = fpath;
    shader->iTime = glGetUniformLocation(shader->id, "iTime");
    shader->iTimeDelta = glGetUniformLocation(shader->id, "iTimeDelta");
    shader->iFrame = glGetUniformLocation(shader->id, "iFrame");
    shader->iFrameRate = glGetUniformLocation(shader->id, "iFrameRate");
    shader->iDate = glGetUniformLocation(shader->id, "iDate");
    shader->iResolution = glGetUniformLocation(shader->id, "iResolution");
    shader->iMouse = glGetUniformLocation(shader->id, "iMouse");
    shader->iChannels[0] = glGetUniformLocation(shader->id, "iChannel0");
    shader->iChannels[1] = glGetUniformLocation(shader->id, "iChannel1");
    shader->iChannels[2] = glGetUniformLocation(shader->id, "iChannel2");
    shader->iChannels[3] = glGetUniformLocation(shader->id, "iChannel3");
    
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
        return shader;
    }

    fshader = glCreateShader(GL_FRAGMENT_SHADER);
    if (glook_shader_compile(fshader, filebuf, fpath)) {
        free(filebuf);
        shader.id = 0;
        return shader;
    }

    if (glook_shader_link(shader.id, glook.vshader, fshader, filebuf, fpath)) {
        free(filebuf);
        shader.id = 0;
        return shader;
    }

    free(filebuf);
    glDeleteShader(fshader);
    glUseProgram(shader.id);
    glook_shader_uniform_set(&shader, fpath);
    return shader;
}

static int glook_shader_pipeline_load(void)
{
    int i, shadercount = 0;
    for (i = 0; i < glook.filecount && shadercount < GLOOK_SHADER_COUNT; ++i) {
        glook.shaders[shadercount] = glook_shader_load(glook.filepaths[i]);
        if (!glook.shaders[shadercount].id) {
            free(glook.filepaths[i]);
        } else {
            ++shadercount;
        }
        glook.filepaths[i] = NULL;
    }

    glook.filecount = 0;
    return shadercount;
}

static int glook_shader_pipeline_reload(void)
{
    int i, count = 0;
    for (i = 0; i < glook.shadercount; ++i) {
        struct GLSLshader shader = glook_shader_load(glook.shaders[i].fpath);
        if (shader.id) {
            glook.shaders[i] = shader;
            ++count;
        }
    }
    return count;
}

static void glook_shader_render(
    struct GLSLshader* shader, float t, float dt, float f, float fps, float* mouse)
{
    glUseProgram(shader->id);
    glUniform1f(shader->iTime, t);
    glUniform1f(shader->iTimeDelta, dt);
    glUniform1f(shader->iFrame, f);
    glUniform1f(shader->iFrameRate, fps);
    glUniform4f(shader->iMouse, mouse[0], mouse[1], mouse[2], mouse[3]);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

static void glook_shaders_render(float t, float dt, float f, float* mouse)
{
    int i;
    float fps = 1.0F / dt;
    for (i = 0; i < glook.shadercount; ++i) {
        glook_shader_render(glook.shaders + i, t, dt, f, fps, mouse);
    }
    
    glfwSwapBuffers(glook.window);
    glfwPollEvents();
}

static void glook_shader_free(struct GLSLshader* shader)
{
    if (shader->fpath) {
        free(shader->fpath);
    }
    if (shader->id) {
        glDeleteProgram(shader->id);
    }
    memset(shader, 0, sizeof(struct GLSLshader));
}

static void glook_shaders_free(void)
{
    int i;
    for (i = 0; i < glook.shadercount; ++i) {
        glook_shader_free(glook.shaders + i);
    }
}

static void glook_mouse_pos(float* x, float* y)
{
    double mx, my, scale = (float)GLOOK_SCALE;
    glfwGetCursorPos(glook.window, &mx, &my);
    *x = (float)mx * scale;
    *y = (float)(glook.height - (float)my) * scale;
}

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

static void glook_mouse_get(float* mouse)
{
    glook_mouse_pos(mouse, mouse + 1);
    mouse[2] = (float)glook_mouse_down(GLFW_MOUSE_BUTTON_LEFT);
    mouse[3] = (float)glook_mouse_pressed(GLFW_MOUSE_BUTTON_LEFT);
}

static unsigned int glook_key_pressed(unsigned int key)
{
    unsigned int pressed = glook.keys_pressed[key];
    glook.keys_pressed[key] = 0;
    return pressed;
}

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

static void glook_keyboard_callback(
    GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)mods;
    (void)window;
    (void)scancode;
    glook.keys_pressed[key] = (char)(!glook.keys[key] && !!action);
    glook.keys[key] = (char)!!action;
}

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
    glDepthFunc(GL_LESS);
    glook.window = window;
    glook.vshader = glCreateShader(GL_VERTEX_SHADER);
    glook_shader_compile(glook.vshader, glook_shader_string_quad, NULL);
    return EXIT_SUCCESS;
}

static void glook_deinit(void)
{
    glook_filepaths_free();
    glook_shaders_free();
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
    }

    return EXIT_SUCCESS;
}

static int glook_opened(void)
{
    glClear(GL_COLOR_BUFFER_BIT);
    return !glfwWindowShouldClose(glook.window);
}

static void glook_run(const int perf)
{
    unsigned int frame = 0, reload = 0;
    float mouse[4], f, t, dt, T = 0.0F, tzero = 0.0F;
    while (glook_opened()) {
        t = glfwGetTime();
        dt = t - T;
        T = t;
        t -= tzero;
        f = (float)frame++;

        glook_mouse_get(mouse);
        if (glook_key_pressed(GLFW_KEY_ESCAPE)) {
            break;
        }
        if (glook_key_pressed(GLFW_KEY_R)) {
            ++reload; 
        }
        if (glook_key_pressed(GLFW_KEY_T)) {
            tzero = t;
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

        if (perf && !(frame % 2)) {
            glook_log(
                "%d x %d\tfps: %f\tframe: %lu\ttime: %f\r",
                glook.width, glook.height, 1.0F / dt, frame, t
            );
        }

        glook_shaders_render(t, dt, f, mouse);
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
    int i, width = 640, height = 360, fullscreen = 0, perf = 0;
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
                ++perf;
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

    glook_run(perf); 
    glook_deinit();
    return EXIT_SUCCESS;
}

