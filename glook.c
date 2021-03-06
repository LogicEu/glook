#include <glee.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
===================================================
>>>>>>>>>  GLOOK GLSL SHADER VISUALIZER   >>>>>>>>>
=========================================  @eulogic
*/

#define BUFF_SIZE 1024

#ifdef __APPLE__
static const char* glsl_version = "#version 330 core\n\n";
#else
static const char* glsl_version = "#version 300 es\nprecision mediump float;\n\n";
#endif

static const char* glook_quad_shader = "layout (location = 0) in vec2 vertCoord;\nout vec2 fragCoord;\nvoid main()\n{\nfragCoord = vertCoord;\ngl_Position = vec4(vertCoord.x, vertCoord.y, 0.0, 1.0);}\n";
static const char* glook_template = "out vec4 FragColor;\n\nuniform float u_time;\nuniform vec2 u_resolution;\nuniform vec2 u_mouse;\n\nvoid main() \n{\n\tvec2 uv = gl_FragCoord.xy / u_resolution.y;\n\tvec3 color = vec3(uv.x, uv.y, cos(u_time));\n\tFragColor = vec4(color, 1.0);\n}\n";

static void glook_shader_write()
{
    const char* template = "template.glsl";
    FILE* file = fopen(template, "w");
    if (!file) {
        printf("glook could not write file '%s'\n", template);
        return;
    }
    fprintf(file, "%s", glook_template);
    fclose(file);
    printf("glook succesfully created GLSL shader file '%s'\n", template);
}

static unsigned int glook_shader_load(const char* fpath)
{
    unsigned int shader = glCreateProgram();
    char* fb = glee_shader_file_read(fpath);
    if (!fb) {
        printf("glook had a problem loading shader '%s'\n", fpath);
        return 0;
    }

    size_t size = strlen(glsl_version) + strlen(glook_quad_shader);
    char* glsl_vert = (char*)malloc(size + 1);
    strcpy(glsl_vert, glsl_version);
    strcat(glsl_vert, glook_quad_shader);
    glsl_vert[size] = '\0';

    unsigned int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    unsigned int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

    glee_shader_compile(glsl_vert, vertex_shader);
    glee_shader_compile(fb, fragment_shader);
    glee_shader_link(shader, vertex_shader, fragment_shader);
    glUseProgram(shader);

    free(fb);
    free(glsl_vert);
    return shader;
}

void glook_resolution_set(unsigned int shader)
{
    int w, h, frame_size = 1;
#ifdef __APPLE__
    frame_size *= 2;
#endif
    glee_window_get_size(&w, &h);
    float res[] = {(float)(w * frame_size), (float)(h * frame_size)};
    glee_shader_uniform_set(shader, 2, "u_resolution", &res[0]);
}

int main(int argc, char** argv)
{   
    if (argc < 2) {
        printf("Missing input shader. See -help for more information.\n");
        return EXIT_FAILURE;
    }

    unsigned int w = 400, h = 300, x = 0, y = 0, f = 0;
    char fragment_shader_path[BUFF_SIZE] = "$";
    char window_title[BUFF_SIZE] = "glook";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-help")) {
            printf(" ============= glook GLSL shader visualizer ============== \n");
            printf("\nEnter your input fragment shader '$ glsl file_path.frag'.\n");
            printf("Use -w and -h to set the width and height of the visualizer.\n");
            printf("Set the window position with -x and -y.\n");
            printf("Use the -f flag for fullscreen and -t to set a title.\n");
            printf("You can start off with a standard template using -template.\n");
            printf("Use '&' as last argument to run in the background.\n\n");
            return EXIT_SUCCESS;
        } else if (!strcmp(argv[i], "-template")) {
            glook_shader_write();
            return EXIT_SUCCESS;
        } else if (!strcmp(argv[i], "-w")) {
            if (i + 1 >= argc) {
                printf("Missing value for -w.\n");
                return EXIT_FAILURE;
            }
            w = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-h")) {
            if (i + 1 >= argc) {
                printf("Missing value for -h.\n");
                return EXIT_FAILURE;
            }
            h = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-x")) {
            if (i + 1 >= argc) {
                printf("Missing value for -x.\n");
                return EXIT_FAILURE;
            }
            x = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-y")) {
            if (i + 1 >= argc) {
                printf("Missing value for -y.\n");
                return EXIT_FAILURE;
            } 
            y = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-t")) {
            if (i + 1 >= argc) {
                printf("Missing value for -t.\n");
                return EXIT_FAILURE;
            }
            strcpy(window_title, argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-f")) f = 1;
        else strcpy(fragment_shader_path, argv[i]);
    }

    if (!strcmp(fragment_shader_path, "$")) {
        printf("Missing input shader. See -help for more information.\n");
        return EXIT_FAILURE;
    } 

    glee_init();
    glee_window_create(window_title, w, h, f, 0);
    if (x != 0 || y != 0) glee_window_set_position(x, y);
    glee_buffer_quad_create();
    unsigned int shader = glook_shader_load(fragment_shader_path);
    if (!shader) return EXIT_FAILURE;

    glook_resolution_set(shader);
    float mouse[2], time_current, time_start = glee_time_get();
    while (glee_window_is_open()) {
        glee_screen_clear();

        time_current = glee_time_get() - time_start;
        glee_mouse_pos(&mouse[0], &mouse[1]);

        if (glee_key_pressed(GLFW_KEY_ESCAPE)) break;
        if (glee_key_pressed(GLFW_KEY_R)) {
            unsigned int new_shader = glook_shader_load(fragment_shader_path);
            if (new_shader) {
                glDeleteProgram(shader);
                shader = new_shader;
                glUseProgram(shader);
                time_start = time_current;
                glook_resolution_set(shader);
            }
        }
        glee_shader_uniform_set(shader, 1, "u_time", &time_current);
        glee_shader_uniform_set(shader, 2, "u_mouse", &mouse[0]);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glee_screen_refresh();
    }
    glee_deinit();
    return EXIT_SUCCESS;
}
