#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_GLCOREARB  
#include <GLFW/glfw3.h>

// --- CONFIGURATION STRUCT ---
typedef struct {
    int width;
    int height;
    int fps;
    int duration_secs;
    char output_file[256];
    float ior;
    float spin_speed;
    int cut;          // 0: Brilliant, 1: Princess, 2: Emerald
    float carat;      // Weight/Size scale
    float color[3];   // RGB tint
    float clarity;    // 0.0 (FL) to 1.0 (I3)
} RenderConfig;

void print_usage(const char* prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -w <width>         Output width (default: 720)\n");
    printf("  -h <height>        Output height (default: 720)\n");
    printf("  -f <fps>           Frames per second (default: 60)\n");
    printf("  -d <seconds>       Duration in seconds (default: 10)\n");
    printf("  -o <filename>      Output MP4 filename (default: output.mp4)\n");
    printf("  -i <ior>           Index of Refraction (default: 2.42)\n");
    printf("  -s <speed>         Spin speed multiplier (default: 0.8)\n");
    printf("  -cut <type>        0 = Brilliant, 1 = Princess, 2 = Emerald (default: 0)\n");
    printf("  -carat <weight>    Carat weight (default: 1.0)\n");
    printf("  -clarity <0.0-1.0> 0.0=FL, 0.2=VVS, 0.4=VS, 0.6=SI, 1.0=I3 (default: 0.0)\n");
    printf("  -r <red>           Color tint Red 0.0-1.0 (default: 1.0)\n");
    printf("  -g <green>         Color tint Green 0.0-1.0 (default: 1.0)\n");
    printf("  -b <blue>          Color tint Blue 0.0-1.0 (default: 1.0)\n");
}

// --- GLSL VERTEX SHADER ---
const char* vertexShaderSource = 
"#version 330 core\n"
"layout(location = 0) in vec2 position;\n"
"out vec2 uv;\n"
"void main() {\n"
"    uv = position * 0.5 + 0.5;\n"
"    gl_Position = vec4(position, 0.0, 1.0);\n"
"}\n";

// --- GLSL FRAGMENT SHADER ---
const char* fragmentShaderSource = 
"#version 330 core\n"
"in vec2 uv;\n"
"out vec4 fragColor;\n"
"uniform vec2 u_resolution;\n"
"uniform float u_time;\n"
"uniform float u_ior;\n"
"uniform float u_spin_speed;\n"
"uniform int u_cut;\n"
"uniform float u_carat;\n"
"uniform vec3 u_color;\n"
"uniform float u_clarity;\n"

"mat2 rot(float a) {\n"
"    float s = sin(a), c = cos(a);\n"
"    return mat2(c, -s, s, c);\n"
"}\n"

"// --- 3D Noise for Clarity/Inclusions ---\n"
"float hash(vec3 p) {\n"
"    p = fract(p * 0.3183099 + 0.1);\n"
"    p *= 17.0;\n"
"    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));\n"
"}\n"
"float noise(vec3 x) {\n"
"    vec3 i = floor(x);\n"
"    vec3 f = fract(x);\n"
"    f = f*f*(3.0-2.0*f);\n"
"    return mix(mix(mix(hash(i+vec3(0,0,0)), hash(i+vec3(1,0,0)),f.x),\n"
"                   mix(hash(i+vec3(0,1,0)), hash(i+vec3(1,1,0)),f.x),f.y),\n"
"               mix(mix(hash(i+vec3(0,0,1)), hash(i+vec3(1,0,1)),f.x),\n"
"                   mix(hash(i+vec3(0,1,1)), hash(i+vec3(1,1,1)),f.x),f.y),f.z);\n"
"}\n"

"// CUT 0: High-Fidelity Round Brilliant (58 Facets)\n"
"float sdBrilliant(vec3 p) {\n"
"    float d = p.y - 0.35; // Table\n"
"    float pi = 3.14159265;\n"
"    float a = atan(p.x, p.z);\n"
"    \n"
"    // 8 Main Crown & Pavilion Facets\n"
"    float s8 = pi / 4.0;\n"
"    float a8 = mod(a + s8/2.0, s8) - s8/2.0;\n"
"    vec2 q8 = vec2(sin(a8), cos(a8)) * length(p.xz);\n"
"    d = max(d, dot(vec2(p.y, q8.y), normalize(vec2(0.8, 1.0))) - 0.7);\n"
"    d = max(d, dot(vec2(p.y, q8.y), normalize(vec2(-1.0, 1.0))) - 0.63);\n"
"    \n"
"    // 8 Star Facets (Offset)\n"
"    float a8s = mod(a, s8) - s8/2.0;\n"
"    vec2 q8s = vec2(sin(a8s), cos(a8s)) * length(p.xz);\n"
"    d = max(d, dot(vec2(p.y, q8s.y), normalize(vec2(0.65, 1.0))) - 0.65);\n"
"    \n"
"    // 16 Upper & Lower Girdle Facets\n"
"    float s16 = pi / 8.0;\n"
"    float a16 = mod(a + s16/2.0, s16) - s16/2.0;\n"
"    vec2 q16 = vec2(sin(a16), cos(a16)) * length(p.xz);\n"
"    d = max(d, dot(vec2(p.y, q16.y), normalize(vec2(0.9, 1.0))) - 0.72);\n"
"    d = max(d, dot(vec2(p.y, q16.y), normalize(vec2(-1.15, 1.0))) - 0.61);\n"
"    \n"
"    // Cylindrical Girdle Boundary\n"
"    d = max(d, length(p.xz) - 1.0);\n"
"    return d;\n"
"}\n"

"// CUT 1: Refined Princess\n"
"float sdPrincess(vec3 p) {\n"
"    float d = p.y - 0.35;\n"
"    vec3 a = abs(p);\n"
"    d = max(d, a.x - 0.7);\n"
"    d = max(d, a.z - 0.7);\n"
"    // Chevron Pavilion Cuts\n"
"    d = max(d, dot(vec3(a.x, -p.y, a.z), normalize(vec3(1.0, 1.2, 1.0))) - 0.1);\n"
"    d = max(d, dot(vec3(a.x, -p.y, 0.0), normalize(vec3(1.0, 1.5, 0.0))) - 0.2);\n"
"    d = max(d, dot(vec3(0.0, -p.y, a.z), normalize(vec3(0.0, 1.5, 1.0))) - 0.2);\n"
"    d = max(d, dot(vec3(a.x, p.y, a.z), normalize(vec3(1.0, 0.8, 1.0))) - 0.85);\n"
"    return d;\n"
"}\n"

"// CUT 2: Refined Emerald (Stepped)\n"
"float sdEmerald(vec3 p) {\n"
"    float d = p.y - 0.35;\n"
"    vec3 a = abs(p);\n"
"    d = max(d, a.x - 0.5);\n"
"    d = max(d, a.z - 0.7);\n"
"    d = max(d, a.x + a.z - 1.0);\n" 
"    // Multiple Concentric Steps (Crown & Pavilion)\n"
"    d = max(d, dot(vec3(a.x, p.y, 0.0), normalize(vec3(1.0, 1.2, 0.0))) - 0.65);\n"
"    d = max(d, dot(vec3(0.0, p.y, a.z), normalize(vec3(0.0, 1.2, 1.0))) - 0.85);\n"
"    d = max(d, dot(vec3(a.x, p.y, 0.0), normalize(vec3(1.0, 0.6, 0.0))) - 0.55);\n"
"    d = max(d, dot(vec3(a.x, -p.y, 0.0), normalize(vec3(1.0, 1.5, 0.0))) - 0.1);\n"
"    d = max(d, dot(vec3(0.0, -p.y, a.z), normalize(vec3(0.0, 1.5, 1.0))) - 0.15);\n"
"    d = max(d, dot(vec3(a.x, -p.y, 0.0), normalize(vec3(1.0, 2.5, 0.0))) - 0.05);\n"
"    return d;\n"
"}\n"

"float mapScene(vec3 p) {\n"
"    vec3 q = p;\n"
"    q.xy *= rot(0.25);\n" 
"    q.xz *= rot(u_time * u_spin_speed);\n"
"    float scale = pow(u_carat, 0.333333);\n"
"    q /= scale;\n"
"    \n"
"    float d = 0.0;\n"
"    if (u_cut == 0) d = sdBrilliant(q);\n"
"    else if (u_cut == 1) d = sdPrincess(q);\n"
"    else if (u_cut == 2) d = sdEmerald(q);\n"
"    \n"
"    return d * scale;\n"
"}\n"

"vec3 getNormal(vec3 p) {\n"
"    vec2 e = vec2(0.001, 0.0);\n"
"    return normalize(vec3(mapScene(p+e.xyy) - mapScene(p-e.xyy),\n"
"                          mapScene(p+e.yxy) - mapScene(p-e.yxy),\n"
"                          mapScene(p+e.yyx) - mapScene(p-e.yyx)));\n"
"}\n"

"vec3 envMap(vec3 rd) {\n"
"    float l1 = pow(max(dot(rd, normalize(vec3(1.0, 1.0, 0.5))), 0.0), 30.0);\n"
"    float l2 = pow(max(dot(rd, normalize(vec3(-1.0, 0.8, -0.5))), 0.0), 30.0);\n"
"    float l3 = pow(max(dot(rd, normalize(vec3(0.0, -1.0, 1.0))), 0.0), 30.0);\n"
"    vec3 col = vec3(1.0) * (l1 + l2 + l3) * 2.0;\n"
"    vec2 uv = rd.xy / max(abs(rd.z), 0.001);\n"
"    float grid = smoothstep(0.95, 1.0, max(sin(uv.x * 25.0), sin(uv.y * 25.0)));\n"
"    col += mix(vec3(0.05, 0.06, 0.08), vec3(0.15, 0.18, 0.22), grid) * max(rd.y + 1.0, 0.0);\n"
"    return col;\n"
"}\n"

"void main() {\n"
"    vec2 coord = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;\n"
"    vec3 ro = vec3(0.0, 0.0, 2.5);\n"
"    vec3 rd = normalize(vec3(coord, -2.0));\n"
"    float t = 0.0;\n"
"    vec3 p;\n"

"    for(int i=0; i<100; i++) {\n"
"        p = ro + rd * t;\n"
"        float res = mapScene(p);\n"
"        if (res < 0.0005) break;\n"
"        t += res;\n"
"        if (t > 15.0) break;\n"
"    }\n"
"    \n"
"    vec3 finalColor = envMap(rd) * 0.15;\n"
"    \n"
"    if (t < 15.0) {\n"
"        vec3 n = getNormal(p);\n"
"        vec3 i = rd;\n"
"        \n"
"        float iorR = 1.0 / (u_ior - 0.02);\n"
"        float iorG = 1.0 / u_ior;\n"
"        float iorB = 1.0 / (u_ior + 0.02);\n"
"        \n"
"        vec3 rRay = refract(i, n, iorR);\n"
"        vec3 gRay = refract(i, n, iorG);\n"
"        vec3 bRay = refract(i, n, iorB);\n"
"        \n"
"        float fresnel = pow(clamp(1.0 + dot(i, n), 0.0, 1.0), 5.0);\n"
"        vec3 reflRay = reflect(i, n);\n"
"        vec3 nIn = -n;\n"
"        vec3 rRay2 = reflect(rRay, nIn);\n"
"        vec3 gRay2 = reflect(gRay, nIn);\n"
"        vec3 bRay2 = reflect(bRay, nIn);\n"
"        \n"
"        vec3 refrColor;\n"
"        float bounceMix = fresnel * 0.8 + 0.2;\n"
"        refrColor.r = mix(envMap(rRay).r, envMap(rRay2).r, bounceMix);\n"
"        refrColor.g = mix(envMap(gRay).g, envMap(gRay2).g, bounceMix);\n"
"        refrColor.b = mix(envMap(bRay).b, envMap(bRay2).b, bounceMix);\n"
"        \n"
"        // --- Clarity / Inclusion Simulation ---\n"
"        if (u_clarity > 0.0) {\n"
"            vec3 innerPos = p + rRay * 0.5;\n"
"            \n"
"            // High-frequency noise for pinpoints/crystals (VVS/VS)\n"
"            float pinpoints = smoothstep(0.95, 1.0, noise(innerPos * 60.0));\n"
"            // Low-frequency noise for clouds/feathers (SI/I)\n"
"            float clouds = smoothstep(0.5, 0.9, noise(innerPos * 15.0));\n"
"            \n"
"            float inclusionMask = (pinpoints * u_clarity) + (clouds * pow(u_clarity, 2.0));\n"
"            inclusionMask = clamp(inclusionMask, 0.0, 1.0);\n"
"            \n"
"            // Darken and scatter refracted light where inclusions exist\n"
"            refrColor = mix(refrColor, vec3(0.1, 0.1, 0.12), inclusionMask * 0.8);\n"
"        }\n"
"        \n"
"        refrColor *= u_color;\n"
"        finalColor = refrColor * 1.5 + envMap(reflRay) * fresnel * 3.0;\n"
"    }\n"
"    \n"
"    finalColor = clamp((finalColor*(2.51*finalColor+0.03))/(finalColor*(2.43*finalColor+0.59)+0.14), 0.0, 1.0);\n"
"    fragColor = vec4(finalColor, 1.0);\n"
"}\n";

int main(int argc, char** argv) {
    RenderConfig cfg = {
        .width = 720, .height = 720, .fps = 60, .duration_secs = 10,
        .output_file = "output.mp4", .ior = 2.42f, .spin_speed = 0.8f,
        .cut = 0, .carat = 1.0f, .color = {1.0f, 1.0f, 1.0f}, .clarity = 0.0f
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) cfg.width = atoi(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) cfg.height = atoi(argv[++i]);
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) cfg.fps = atoi(argv[++i]);
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) cfg.duration_secs = atoi(argv[++i]);
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) strncpy(cfg.output_file, argv[++i], 255);
        else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) cfg.ior = atof(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) cfg.spin_speed = atof(argv[++i]);
        else if (strcmp(argv[i], "-cut") == 0 && i + 1 < argc) cfg.cut = atoi(argv[++i]);
        else if (strcmp(argv[i], "-carat") == 0 && i + 1 < argc) cfg.carat = atof(argv[++i]);
        else if (strcmp(argv[i], "-clarity") == 0 && i + 1 < argc) cfg.clarity = atof(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) cfg.color[0] = atof(argv[++i]);
        else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) cfg.color[1] = atof(argv[++i]);
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) cfg.color[2] = atof(argv[++i]);
        else { print_usage(argv[0]); return 1; }
    }

    int total_frames = cfg.fps * cfg.duration_secs;
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); 
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); 
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    
    GLFWwindow* window = glfwCreateWindow(cfg.width, cfg.height, "GIA Parameterized Engine", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    GLuint fbo, renderbuffer;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &renderbuffer); glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, cfg.width, cfg.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
    glViewport(0, 0, cfg.width, cfg.height);

    GLuint vs = glCreateShader(GL_VERTEX_SHADER); glShaderSource(vs, 1, &vertexShaderSource, NULL); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fs, 1, &fragmentShaderSource, NULL); glCompileShader(fs);
    GLuint prog = glCreateProgram(); glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog); glUseProgram(prog);
    
    float vertices[] = { -1.0f,-1.0f, 1.0f,-1.0f, -1.0f,1.0f, -1.0f,1.0f, 1.0f,-1.0f, 1.0f,1.0f };
    GLuint vao, vbo; 
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); 
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    GLint posAttrib = glGetAttribLocation(prog, "position"); 
    glEnableVertexAttribArray(posAttrib); glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
    
    glUniform2f(glGetUniformLocation(prog, "u_resolution"), (float)cfg.width, (float)cfg.height);
    glUniform1f(glGetUniformLocation(prog, "u_ior"), cfg.ior);
    glUniform1f(glGetUniformLocation(prog, "u_spin_speed"), cfg.spin_speed);
    glUniform1i(glGetUniformLocation(prog, "u_cut"), cfg.cut);
    glUniform1f(glGetUniformLocation(prog, "u_carat"), cfg.carat);
    glUniform1f(glGetUniformLocation(prog, "u_clarity"), cfg.clarity);
    glUniform3f(glGetUniformLocation(prog, "u_color"), cfg.color[0], cfg.color[1], cfg.color[2]);
    GLint timeUniform = glGetUniformLocation(prog, "u_time");
    
    char ffmpeg_cmd[768];
    snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd), 
             "ffmpeg -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -vf \"fps=15,scale=480:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse\" %s", 
             cfg.width, cfg.height, cfg.fps, cfg.output_file);
             
    FILE* ffmpeg = popen(ffmpeg_cmd, "w");
    unsigned char* pixel_buffer = malloc(cfg.width * cfg.height * 4);
    const float dt = 1.0f / (float)cfg.fps;

    const char* cut_names[] = {"Round Brilliant (58 Facet)", "Princess", "Emerald"};
    printf("--- Realistic Parameterized Gem Engine ---\n");
    printf("Cut:      %s\n", cut_names[cfg.cut % 3]);
    printf("Weight:   %.2f Carat\n", cfg.carat);
    printf("Clarity:  %.2f (0.0=FL, 1.0=I3)\n", cfg.clarity);
    printf("Color:    RGB(%.2f, %.2f, %.2f)\n", cfg.color[0], cfg.color[1], cfg.color[2]);
    printf("Rendering to: %s\n", cfg.output_file);
    printf("------------------------------------------\n");

    for (int frame = 0; frame < total_frames; frame++) {
        glUniform1f(timeUniform, (float)frame * dt);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glReadPixels(0, 0, cfg.width, cfg.height, GL_RGBA, GL_UNSIGNED_BYTE, pixel_buffer);
        fwrite(pixel_buffer, 1, cfg.width * cfg.height * 4, ffmpeg);
        if (frame % cfg.fps == 0 && frame != 0) printf("Rendered %d / %d seconds...\n", frame / cfg.fps, cfg.duration_secs);
    }

    pclose(ffmpeg); free(pixel_buffer);
    glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
    glDeleteFramebuffers(1, &fbo); glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteProgram(prog); glfwDestroyWindow(window); glfwTerminate();
    
    printf("Render complete: %s\n", cfg.output_file);
    return 0;
}