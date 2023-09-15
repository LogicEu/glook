#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __APPLE__
    #define GLOOK_SCALE 1
    #define GLOOK_GLSL_VERSION "#version 300 es\nprecision mediump float;\n\n"
    #include <GL/glew.h>
#else
    #define GLOOK_SCALE 1
    #define GLOOK_GLSL_VERSION "#version 330 core\n\n"
    #define GL_SILENCE_DEPRECATION
    #define GLFW_INCLUDE_GLCOREARB
#endif

#include <GLFW/glfw3.h>

#define BUFSIZE 1024
#define LOGSIZE 512
#define GLOOK_FILE_COUNT 1
#define GLOOK_KEYBOARD_COUNT 1024

static const char glook_shader_body[] = GLOOK_GLSL_VERSION
"out vec4 _glookFragColor;\n\n"

"uniform float iTime;\n"
"uniform vec2 iResolution;\n"
"uniform vec2 iMouse;\n\n"

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

static struct glook {
    GLFWwindow* window;
    unsigned int width, height;
    char* filepaths[GLOOK_FILE_COUNT];
    char keys[GLOOK_KEYBOARD_COUNT];
    char keys_pressed[GLOOK_KEYBOARD_COUNT];
} glook = {NULL, 400, 300, {0}, {0}, {0}};

static void glook_template_write(void)
{
    static const char* filename = "template.glsl";
    FILE* file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "glook could not write file '%s'\n", filename);
        return;
    }
    
    fprintf(file, "%s", glook_shader_string_template);
    fprintf(stdout, "glook created shader file: %s\n", filename);
    fclose(file);
}

static char* glook_file_read(const char* path)
{
    char* buffer;
    size_t filelen;
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "glook could not open file '%s'\n", path);
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

static int glook_shader_compile(const GLchar* buffer, unsigned int shader)
{
    int success;
    char infoLog[LOGSIZE];
    glShaderSource(shader, 1, &buffer, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, LOGSIZE, NULL, infoLog);
        fprintf(stderr, "glook failed to compile shader\n%s", infoLog);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int glook_shader_link(
    unsigned int shader, unsigned int vshader, unsigned int fshader)
{
    int success;
    char infoLog[LOGSIZE];
    glAttachShader(shader, vshader);
    glAttachShader(shader, fshader);
    glLinkProgram(shader);
    glGetProgramiv(shader, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader, LOGSIZE, NULL, infoLog);
        fprintf(stderr, "glook failed to compile shader\n%s", infoLog);
        return EXIT_FAILURE;
    }

    glDeleteShader(vshader);
    glDeleteShader(fshader);
    return EXIT_SUCCESS;
}

static unsigned int glook_shader_load(const char* fpath)
{
    char* fb;
    unsigned int vshader, fshader, shader = glCreateProgram();
    fb = glook_file_read(fpath);
    if (!fb) {
        return 0;
    }

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);
    glook_shader_compile(glook_shader_string_quad, vshader);
    if (glook_shader_compile(fb, fshader)) {
        free(fb);
        return 0;
    }

    if (glook_shader_link(shader, vshader, fshader)) {
        free(fb);
        return 0;
    }

    glUseProgram(shader);
    free(fb);
    return shader;
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

static void glook_mouse_pos(float* x, float* y)
{
    double mx, my;
    glfwGetCursorPos(glook.window, &mx, &my);
    *x = (float)mx;
    *y = (float)glook.height - (float)my;
}

static unsigned int glook_key_pressed(unsigned int key)
{
    if (!glook.keys_pressed[key] && glook.keys[key]) {
        glook.keys_pressed[key] = 1;
        return 1;
    }
    return 0;
}

static int glook_filepaths_count(void)
{
    int i;
    for (i = 0; glook.filepaths[i] && i < GLOOK_FILE_COUNT; ++i);
    return i;
}

static char* glook_filepath_copy(const char* src)
{
    char* ret;
    int len = strlen(src);
    ret = (char*)malloc(len + 1);
    memcpy(ret, src, len + 1);
    return ret;
}

static void glook_file_drop_callback(GLFWwindow* window, int count, const char** paths)
{
    int i, filecount = glook_filepaths_count();
    for (i = 0; i < count; i++) {
        if (filecount + 1 == GLOOK_FILE_COUNT) {
            fprintf(
                stderr,
                "glook: cannot open more than %d files at once\n",
                GLOOK_FILE_COUNT
            );
            break;
        }
        glook.filepaths[filecount++] = glook_filepath_copy(paths[i]);
    }
    (void)window;
}

static void glook_keyboard_callback(
    GLFWwindow* window, int key, int scancode, int action, int mods)
{
    glook.keys[key] = action;
    glook.keys_pressed[key] = glook.keys_pressed[key] * !!action;
    (void)window;
    (void)scancode;
    (void)mods;
}

static void glook_window_size_callback(GLFWwindow* window, int width, int height)
{
    glook.width = width;
    glook.height = height;
    (void)window;
}

static int glook_window_create(
    const char* title, int width, int height, unsigned int fullscreen)
{
    GLFWwindow* window;

    if (width < 1 || height < 1) {
        fprintf(stderr, "glook: invalid resolution: %dx%d\n", width, height);
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
        fprintf(stderr, "glook: could not open a glfw window\n");
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
        fprintf(stderr, "glook: failed to initiate glew\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }
#endif

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND); 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LESS);
    glook.window = window;
    return EXIT_SUCCESS;
}

static void glook_usage(void)
{
    fprintf(
        stdout,
        "<file>\t\t: read, compile and visualize <file> as GLSL shader\n"
        "-w <uint>\t: set the width of the rendering window to <uint> pixels\n"
        "-h <uint>\t: set the height of the rendering window to <uint> pixels\n"
        "-f\t\t: visualize shader in fullscreen resolution\n"
        "-template\t: write template shader 'template.glsl' at current directory\n"
        "-help\t\t: print this help message\n"
    );
}

int main(const int argc, const char** argv)
{
    char* fpath = NULL;
    int i, w = 400, h = 300, f = 0, reload = 0;
    float mx, my, rh, rw, time, tzero = 0.0F;
    unsigned int shader, iTime, iMouse, iResolution;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            int* p = NULL;
            if (!strcmp(argv[i] + 1, "help")) {
                glook_usage();
                return EXIT_SUCCESS;
            } else if (!strcmp(argv[i] + 1, "template")) {
                glook_template_write();
                return EXIT_SUCCESS;
            } else if (argv[i][1] == 'w' && !argv[i][2]) {
                p = &w;
            } else if (argv[i][1] == 'h' && !argv[i][2]) {
                p = &h;
            } else if (argv[i][1] == 'f' && !argv[i][2]) {
                ++f;
            } else {
                fprintf(stderr, "glook: unknown option %s\n", argv[i]);
            }

            if (p) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "glook: missing value for option %s\n", argv[i]);
                    return EXIT_FAILURE;
                }
                *p = atoi(argv[++i]);
            }
        } else {
            if (fpath) {
                free(fpath);
            }
            fpath = glook_filepath_copy(argv[i]);
        }
    }        

    if (!fpath) {
        fprintf(stderr, "glook: missing input file. See -help for more information.\n");
        return EXIT_FAILURE;
    } 

    if (!glfwInit()) {
        fprintf(stderr, "glook: failed to initiate glfw\n");
        free(fpath);
        return EXIT_FAILURE;
    }

    if (glook_window_create("glook", w, h, f)) {
        glfwTerminate();
        free(fpath);
        return EXIT_FAILURE;
    }

    glook_buffer_quad_create();
    shader = glook_shader_load(fpath);
    if (!shader) {
        return EXIT_FAILURE;
    }

    rw = (float)(w * GLOOK_SCALE);
    rh = (float)(h * GLOOK_SCALE);
    iTime = glGetUniformLocation(shader, "iTime");
    iMouse = glGetUniformLocation(shader, "iMouse");
    iResolution = glGetUniformLocation(shader, "iResolution");
    glUniform2f(iResolution, rw, rh);

    while (!glfwWindowShouldClose(glook.window)) {
        glClear(GL_COLOR_BUFFER_BIT);

        time = glfwGetTime() - tzero;
        glook_mouse_pos(&mx, &my);

        if (glook_key_pressed(GLFW_KEY_ESCAPE)) {
            break;
        }
        if (glook_key_pressed(GLFW_KEY_R)) {
            ++reload; 
        }
        if (glook_key_pressed(GLFW_KEY_T)) {
            tzero = time;
        }
        printf("%f, %f\n", mx, my);
        if (glook.filepaths[0]) {
            for (i = 0; glook.filepaths[i]; ++i) {
                free(fpath);
                fpath = glook_filepath_copy(glook.filepaths[i]);
                free(glook.filepaths[i++]);
            }
            ++reload;
        }

        if (reload) {
            unsigned int rshader = glook_shader_load(fpath);
            if (rshader) {
                glDeleteProgram(shader);
                shader = rshader;
                glUniform2f(iResolution, rw, rh);
            }

            tzero = time;
            reload = 0;
        }

        glUniform1f(iTime, time);
        glUniform2f(iMouse, mx, my);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(glook.window);
        glfwPollEvents();
    }

    free(fpath);
    glfwTerminate();
    return EXIT_SUCCESS;
}

