#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_GLCOREARB  
#include <GLFW/glfw3.h>

typedef struct {
    int width;
    int height;
    int fps;
    int duration_secs;
    char output_file[256];
    float rain_intensity; 
    float color[2][3];    
    float skin_color[2][3];
    float wind_force;     
    float light_dir[3];   
    float moon_dir[3];    
    float sky_top[3];
    float sky_horizon[3];
    float ground_color[3];
} RenderConfig;

typedef struct { 
    float x, y, z; 
    float vx, vy, vz; 
    float ox, oy, oz; 
} HairVertex;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
} RainDrop;

typedef struct {
    int hair_type;      // 0: Straight, 1: Wavy, 2: Curly, 3: Coily
    int strand_count;   // Follicle density
    int segments;       // Length resolution
    float length;       // Overall absolute length scale factor
    int is_short;       // 1 for textured crop, 0 for flowing style
    int is_wet;         // 1 for heavy wet clumping behavior, 0 for dry chaotic splitting
} HeadHairConfig;

void print_usage(const char* prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -w <width>          Output width (default: 1280)\n");
    printf("  -h <height>         Output height (default: 720)\n");
    printf("  -f <fps>            Frames per second (default: 30)\n");
    printf("  -d <seconds>        Duration in seconds (default: 5)\n");
    printf("  -o <filename>       Output GIF filename (default: heads.gif)\n");
    printf("  -wind <force>       Wind breeze strength\n");
    printf("  -rain <intensity>   Rain density\n");
}

// --- HIGH-FASHION PHOTOREALISTIC SHADERS WITH INDIVIDUAL SKIN & HAIR UNIFORMS ---
const char* vertexShaderSource = 
"#version 410 core\n"
"layout(location = 0) in vec3 in_position;\n"
"uniform mat4 u_mvp;\n"
"uniform mat4 u_model;\n"
"uniform int u_render_mode;\n" // 0: Head, 1: Hair, 2: Rain, 3: Skybox
"out vec3 v_pos;\n"
"out vec3 v_normal;\n"
"void main() {\n"
"    vec4 worldPos = u_model * vec4(in_position, 1.0);\n"
"    v_pos = worldPos.xyz;\n"
"    if (u_render_mode == 0) {\n"
"        v_normal = normalize(in_position);\n"
"    }\n"
"    if (u_render_mode == 3) {\n"
"        gl_Position = vec4(in_position, 1.0);\n"
"    } else {\n"
"        gl_Position = u_mvp * worldPos;\n"
"    }\n"
"}\n";

const char* fragmentShaderSource = 
"#version 410 core\n"
"in vec3 v_pos;\n"
"in vec3 v_normal;\n"
"out vec4 fragColor;\n"
"uniform vec3 u_color;\n"
"uniform vec3 u_skin_color;\n"
"uniform int u_render_mode;\n"
"uniform vec3 u_light_dir;\n"
"uniform vec3 u_moon_dir;\n"
"uniform int u_is_male;\n"
"uniform float u_time;\n"
"uniform float u_wind_force;\n"
"uniform float u_rain_intensity;\n"
"uniform vec3 u_sky_top;\n"
"uniform vec3 u_sky_horizon;\n"
"uniform vec3 u_ground_color;\n"

"vec3 getEnvironment(vec3 rd) {\n"
"    float up = rd.y;\n"
"    vec3 sky = mix(u_sky_horizon, u_sky_top, clamp(up * 1.5, 0.0, 1.0));\n"
"    sky = mix(sky, u_ground_color, clamp(-up * 2.0, 0.0, 1.0));\n"
"    \n"
"    float moonDist = distance(normalize(rd), normalize(u_moon_dir));\n"
"    float moonDisk = smoothstep(0.040, 0.032, moonDist);\n"
"    float moonGlow = exp(-moonDist * 14.0) * 0.4;\n"
"    vec3 moonCol = vec3(0.95, 0.98, 1.0) * (moonDisk * 4.0 + moonGlow);\n"
"    \n"
"    return sky + moonCol;\n"
"}\n"

"void main() {\n"
"    vec3 lightDir = normalize(u_light_dir);\n"
"    if (u_render_mode == 3) {\n"
"        vec3 rayDir = normalize(vec3(v_pos.xy, -1.2));\n"
"        fragColor = vec4(getEnvironment(rayDir), 1.0);\n"
"        return;\n"
"    }\n"
"    if (u_render_mode == 0) {\n"
"        vec3 n = normalize(v_normal);\n"
"        float ndl = max(dot(n, lightDir), 0.0);\n"
"        float wetness = clamp(u_rain_intensity * 0.25, 0.0, 0.5);\n"
"        \n"
"        float sss = pow(clamp(dot(-n, lightDir), 0.0, 1.0), 2.5) * (0.4 + wetness * 0.5);\n"
"        float rim = pow(1.0 - max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0), 2.5) * (0.55 + wetness);\n"
"        \n"
"        vec3 col = u_skin_color * (0.32 + 0.68 * ndl + sss) + vec3(0.45, 0.55, 0.70) * rim;\n"
"        fragColor = vec4(col, 1.0);\n"
"        return;\n"
"    }\n"
"    if (u_render_mode == 1) {\n"
"        vec3 viewDir = normalize(vec3(0.0, 0.0, 3.0));\n"
"        vec3 tangent = normalize(dFdx(v_pos) + vec3(0.0001));\n"
"        float ndl = abs(dot(tangent, lightDir));\n"
"        vec3 hVec = normalize(lightDir + viewDir);\n"
"        \n"
"        float waterGloss = clamp(u_rain_intensity * 0.4, 0.0, 0.7);\n"
"        float windShimmer = sin(u_time * 12.0 + v_pos.y * 14.0) * 0.12 * u_wind_force;\n"
"        \n"
"        float specular1 = pow(max(dot(reflect(-lightDir, tangent), viewDir), 0.0), 40.0 + waterGloss * 30.0);\n"
"        float specular2 = pow(max(dot(hVec, tangent), 0.0), 20.0);\n"
"        \n"
"        float depthFactor = smoothstep(-0.7, 1.3, v_pos.y);\n"
"        vec3 wetColorShade = u_color * (1.0 - clamp(u_rain_intensity * 0.3, 0.0, 0.4));\n"
"        vec3 baseShade = mix(wetColorShade * 0.2, wetColorShade * 1.4 + vec3(windShimmer), depthFactor);\n"
"        vec3 skyReflect = getEnvironment(reflect(-viewDir, vec3(0.0, 1.0, 0.0))) * (0.3 + waterGloss);\n"
"        \n"
"        vec3 col = baseShade * (0.18 + 0.82 * ndl) + vec3(0.90, 0.95, 1.0) * specular1 * (1.0 + waterGloss) + u_color * specular2 * 0.6 + skyReflect;\n"
"        fragColor = vec4(col, 1.0);\n"
"        return;\n"
"    }\n"
"    vec3 viewDir = normalize(vec3(0.0, 0.0, 3.0));\n"
"    vec3 rainDir = normalize(vec3(0.1, -1.0, 0.0));\n"
"    float rainSpec = pow(max(dot(reflect(-lightDir, rainDir), viewDir), 0.0), 32.0);\n"
"    vec3 rainCol = vec3(0.75, 0.85, 0.95) * 0.8 + vec3(0.9, 0.95, 1.0) * rainSpec;\n"
"    fragColor = vec4(rainCol, 0.55);\n"
"}\n";

void multiplyMatrix(float* out, const float* a, const float* b) {
    float temp[16];
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) sum += a[k * 4 + row] * b[col * 4 + k];
            temp[col * 4 + row] = sum;
        }
    }
    memcpy(out, temp, 16 * sizeof(float));
}

void getModelTranslation(float* m, float x, float y, float z) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

void getPerspective(float* m, float fov, float aspect, float near, float far) {
    float tanHalfFov = tan(fov / 2.0f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = 1.0f / (aspect * tanHalfFov);
    m[5] = -1.0f / tanHalfFov; 
    m[10] = -(far + near) / (far - near);
    m[11] = -1.0f;
    m[14] = -(2.0f * far * near) / (far - near);
}

void getIdentity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

int main(int argc, char** argv) {
    srand((unsigned int)time(NULL) ^ (unsigned int)clock() ^ (unsigned long)(&main));

    RenderConfig cfg = {
        .width = 1280, .height = 720, .fps = 30, .duration_secs = 5,
        .output_file = "heads.gif",
        .rain_intensity = 0.8f + ((float)(rand() % 120) / 100.0f),
        .wind_force = 0.20f + ((float)(rand() % 35) / 100.0f)
    };

    float moon_angle = ((float)(rand() % 360)) * (3.14159f / 180.0f);
    cfg.moon_dir[0] = sinf(moon_angle) * 1.8f;
    cfg.moon_dir[1] = -0.04f; 
    cfg.moon_dir[2] = cosf(moon_angle) * 1.8f;

    cfg.light_dir[0] = cfg.moon_dir[0];
    cfg.light_dir[1] = cfg.moon_dir[1] + 0.40f;
    cfg.light_dir[2] = cfg.moon_dir[2];

    int palette_idx = rand() % 4;
    if (palette_idx == 0) {
        cfg.sky_top[0] = 0.03f; cfg.sky_top[1] = 0.07f; cfg.sky_top[2] = 0.16f;
        cfg.sky_horizon[0] = 0.15f; cfg.sky_horizon[1] = 0.25f; cfg.sky_horizon[2] = 0.38f;
        cfg.ground_color[0] = 0.06f; cfg.ground_color[1] = 0.08f; cfg.ground_color[2] = 0.10f;
    } else if (palette_idx == 1) {
        cfg.sky_top[0] = 0.10f; cfg.sky_top[1] = 0.06f; cfg.sky_top[2] = 0.20f;
        cfg.sky_horizon[0] = 0.32f; cfg.sky_horizon[1] = 0.22f; cfg.sky_horizon[2] = 0.35f;
        cfg.ground_color[0] = 0.10f; cfg.ground_color[1] = 0.08f; cfg.ground_color[2] = 0.12f;
    } else if (palette_idx == 2) {
        cfg.sky_top[0] = 0.06f; cfg.sky_top[1] = 0.10f; cfg.sky_top[2] = 0.14f;
        cfg.sky_horizon[0] = 0.22f; cfg.sky_horizon[1] = 0.32f; cfg.sky_horizon[2] = 0.40f;
        cfg.ground_color[0] = 0.08f; cfg.ground_color[1] = 0.10f; cfg.ground_color[2] = 0.12f;
    } else {
        cfg.sky_top[0] = 0.08f; cfg.sky_top[1] = 0.12f; cfg.sky_top[2] = 0.24f;
        cfg.sky_horizon[0] = 0.28f; cfg.sky_horizon[1] = 0.38f; cfg.sky_horizon[2] = 0.48f;
        cfg.ground_color[0] = 0.12f; cfg.ground_color[1] = 0.11f; cfg.ground_color[2] = 0.13f;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) cfg.width = atoi(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) cfg.height = atoi(argv[++i]);
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) cfg.fps = atoi(argv[++i]);
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) cfg.duration_secs = atoi(argv[++i]);
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) strncpy(cfg.output_file, argv[++i], 255);
        else if (strcmp(argv[i], "-wind") == 0 && i + 1 < argc) cfg.wind_force = atof(argv[++i]);
        else if (strcmp(argv[i], "-rain") == 0 && i + 1 < argc) cfg.rain_intensity = atof(argv[++i]);
        else { print_usage(argv[0]); return 1; }
    }

    HeadHairConfig head_hair[2];
    
    // Head 0 (Female): Long flowing varied hairstyle with refined hairline
    head_hair[0].hair_type = 1; // Wavy / Layered flow
    head_hair[0].strand_count = 2400; 
    head_hair[0].segments = 26;
    head_hair[0].length = 0.85f + ((float)(rand() % 40) / 100.0f); // Varied absolute length
    head_hair[0].is_short = 0;
    head_hair[0].is_wet = 0;

    // Head 1 (Male): Textured modern crop with distinct length
    head_hair[1].hair_type = 2; // Curly / Textured crop
    head_hair[1].strand_count = 3000; 
    head_hair[1].segments = 16;          
    head_hair[1].length = 0.30f + ((float)(rand() % 20) / 100.0f); // Distinct shorter absolute length
    head_hair[1].is_short = 1;
    head_hair[1].is_wet = 1;

    // Diverse, highly realistic natural human hair color palettes
    float hair_palettes[5][3] = {
        {0.05f, 0.04f, 0.03f}, // Raven Black
        {0.18f, 0.11f, 0.07f}, // Rich Dark Espresso
        {0.30f, 0.18f, 0.10f}, // Warm Chestnut Brown
        {0.40f, 0.25f, 0.14f}, // Deep Auburn / Copper
        {0.52f, 0.38f, 0.24f}  // Golden Dark Blonde
    };

    int col_f = rand() % 5;
    int col_m = (col_f + 1 + (rand() % 4)) % 5; // Ensure different hair colors

    cfg.color[0][0] = hair_palettes[col_f][0];
    cfg.color[0][1] = hair_palettes[col_f][1];
    cfg.color[0][2] = hair_palettes[col_f][2];

    cfg.color[1][0] = hair_palettes[col_m][0];
    cfg.color[1][1] = hair_palettes[col_m][1];
    cfg.color[1][2] = hair_palettes[col_m][2];

    // Diverse, highly realistic human skin tone palettes
    float skin_palettes[4][3] = {
        {0.78f, 0.64f, 0.54f}, // Fair / Ivory Warmth
        {0.72f, 0.56f, 0.44f}, // Medium Warm Beige
        {0.65f, 0.48f, 0.36f}, // Olive / Sun-kissed Tan
        {0.48f, 0.34f, 0.24f}  // Rich Warm Complexion
    };

    int skin_f = rand() % 4;
    int skin_m = (skin_f + 1 + (rand() % 3)) % 4; // Ensure distinct skin tones

    cfg.skin_color[0][0] = skin_palettes[skin_f][0];
    cfg.skin_color[0][1] = skin_palettes[skin_f][1];
    cfg.skin_color[0][2] = skin_palettes[skin_f][2];

    cfg.skin_color[1][0] = skin_palettes[skin_m][0];
    cfg.skin_color[1][1] = skin_palettes[skin_m][1];
    cfg.skin_color[1][2] = skin_palettes[skin_m][2];

    int total_frames = cfg.fps * cfg.duration_secs;
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); 
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    
    GLFWwindow* window = glfwCreateWindow(cfg.width, cfg.height, "Realistic Render Engine", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    GLuint fbo, renderbuffer;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &renderbuffer); glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, cfg.width, cfg.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
    glViewport(0, 0, cfg.width, cfg.height);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.8f);

    GLuint vs = glCreateShader(GL_VERTEX_SHADER); glShaderSource(vs, 1, &vertexShaderSource, NULL); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fs, 1, &fragmentShaderSource, NULL); glCompileShader(fs);
    GLuint renderProg = glCreateProgram(); glAttachShader(renderProg, vs); glAttachShader(renderProg, fs);
    glLinkProgram(renderProg);

    float skybox_verts[] = {
        -1.0f,  1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
    };

    int head_lat = 40, head_lon = 40;
    int base_head_vert_count = head_lat * head_lon * 6;
    int ear_vert_count = 56; 
    int head_vert_count_female = base_head_vert_count + ear_vert_count * 3;
    int head_vert_count_male = base_head_vert_count + ear_vert_count * 3;
    
    float head_radius_x = 0.38f;
    float head_radius_y = 0.52f;
    float head_radius_z = 0.42f;
    float head_center_y = 0.08f;
    
    float* head_verts_female = malloc(head_vert_count_female * 3 * sizeof(float));
    float* head_verts_male = malloc(head_vert_count_male * 3 * sizeof(float));
    
    int hv_idx_f = 0;
    for (int i = 0; i < head_lat; i++) {
        float theta1 = (float)i * 3.14159f / (float)head_lat;
        float theta2 = (float)(i + 1) * 3.14159f / (float)head_lat;
        for (int j = 0; j < head_lon; j++) {
            float phi1 = (float)j * 2.0f * 3.14159f / (float)head_lon;
            float phi2 = (float)(j + 1) * 2.0f * 3.14159f / (float)head_lon;

            float f_taper1 = 1.0f - 0.22f * fmaxf(0.0f, -cosf(theta1)) + 0.06f * fmaxf(0.0f, cosf(theta1));
            float f_taper2 = 1.0f - 0.22f * fmaxf(0.0f, -cosf(theta2)) + 0.06f * fmaxf(0.0f, cosf(theta2));

            float x1 = (head_radius_x * 0.96f) * sinf(theta1) * cosf(phi1) * f_taper1;
            float y1 = (head_radius_y * 1.04f) * cosf(theta1) + head_center_y;
            float z1 = (head_radius_z * 0.96f) * sinf(theta1) * sinf(phi1) * f_taper1;

            float x2 = (head_radius_x * 0.96f) * sinf(theta2) * cosf(phi1) * f_taper2;
            float y2 = (head_radius_y * 1.04f) * cosf(theta2) + head_center_y;
            float z2 = (head_radius_z * 0.96f) * sinf(theta2) * sinf(phi1) * f_taper2;

            float x3 = (head_radius_x * 0.96f) * sinf(theta2) * cosf(phi2) * f_taper2;
            float y3 = (head_radius_y * 1.04f) * cosf(theta2) + head_center_y;
            float z3 = (head_radius_z * 0.96f) * sinf(theta2) * sinf(phi2) * f_taper2;

            head_verts_female[hv_idx_f++] = x1; head_verts_female[hv_idx_f++] = y1; head_verts_female[hv_idx_f++] = z1;
            head_verts_female[hv_idx_f++] = x2; head_verts_female[hv_idx_f++] = y2; head_verts_female[hv_idx_f++] = z2;
            head_verts_female[hv_idx_f++] = x3; head_verts_female[hv_idx_f++] = y3; head_verts_female[hv_idx_f++] = z3;

            float f_taper4 = 1.0f - 0.22f * fmaxf(0.0f, -cosf(theta1)) + 0.06f * fmaxf(0.0f, cosf(theta1));
            float x4 = (head_radius_x * 0.96f) * sinf(theta1) * cosf(phi2) * f_taper4;
            float y4 = (head_radius_y * 1.04f) * cosf(theta1) + head_center_y;
            float z4 = (head_radius_z * 0.96f) * sinf(theta1) * sinf(phi1) * f_taper4;

            head_verts_female[hv_idx_f++] = x1; head_verts_female[hv_idx_f++] = y1; head_verts_female[hv_idx_f++] = z1;
            head_verts_female[hv_idx_f++] = x3; head_verts_female[hv_idx_f++] = y3; head_verts_female[hv_idx_f++] = z3;
            head_verts_female[hv_idx_f++] = x4; head_verts_female[hv_idx_f++] = y4; head_verts_female[hv_idx_f++] = z4;
        }
    }

    int hv_idx_m = 0;
    for (int i = 0; i < head_lat; i++) {
        float theta1 = (float)i * 3.14159f / (float)head_lat;
        float theta2 = (float)(i + 1) * 3.14159f / (float)head_lat;
        for (int j = 0; j < head_lon; j++) {
            float phi1 = (float)j * 2.0f * 3.14159f / (float)head_lon;
            float phi2 = (float)(j + 1) * 2.0f * 3.14159f / (float)head_lon;

            float m_taper1 = 1.0f - 0.08f * fmaxf(0.0f, -cosf(theta1)) + 0.16f * fmaxf(0.0f, cosf(theta1));
            float m_taper2 = 1.0f - 0.08f * fmaxf(0.0f, -cosf(theta2)) + 0.16f * fmaxf(0.0f, cosf(theta2));

            float x1 = (head_radius_x * 1.12f) * sinf(theta1) * cosf(phi1) * m_taper1;
            float y1 = (head_radius_y * 1.02f) * cosf(theta1) + head_center_y;
            float z1 = (head_radius_z * 1.15f) * sinf(theta1) * sinf(phi1) * m_taper1;

            float x2 = (head_radius_x * 1.12f) * sinf(theta2) * cosf(phi1) * m_taper2;
            float y2 = (head_radius_y * 1.02f) * cosf(theta2) + head_center_y;
            float z2 = (head_radius_z * 1.15f) * sinf(theta2) * sinf(phi1) * m_taper2;

            float x3 = (head_radius_x * 1.12f) * sinf(theta2) * cosf(phi2) * m_taper2;
            float y3 = (head_radius_y * 1.02f) * cosf(theta2) + head_center_y;
            float z3 = (head_radius_z * 1.15f) * sinf(theta2) * sinf(phi2) * m_taper2;

            head_verts_male[hv_idx_m++] = x1; head_verts_male[hv_idx_m++] = y1; head_verts_male[hv_idx_m++] = z1;
            head_verts_male[hv_idx_m++] = x2; head_verts_male[hv_idx_m++] = y2; head_verts_male[hv_idx_m++] = z2;
            head_verts_male[hv_idx_m++] = x3; head_verts_male[hv_idx_m++] = y3; head_verts_male[hv_idx_m++] = z3;

            float m_taper4 = 1.0f - 0.08f * fmaxf(0.0f, -cosf(theta1)) + 0.16f * fmaxf(0.0f, cosf(theta1));
            float x4 = (head_radius_x * 1.12f) * sinf(theta1) * cosf(phi2) * m_taper4;
            float y4 = (head_radius_y * 1.02f) * cosf(theta1) + head_center_y;
            float z4 = (head_radius_z * 1.15f) * sinf(theta1) * sinf(phi1) * m_taper4;

            head_verts_male[hv_idx_m++] = x1; head_verts_male[hv_idx_m++] = y1; head_verts_male[hv_idx_m++] = z1;
            head_verts_male[hv_idx_m++] = x3; head_verts_male[hv_idx_m++] = y3; head_verts_male[hv_idx_m++] = z3;
            head_verts_male[hv_idx_m++] = x4; head_verts_male[hv_idx_m++] = y4; head_verts_male[hv_idx_m++] = z4;
        }
    }

    float ear_centers[2][3] = {{-head_radius_x * 0.92f, head_center_y - 0.02f, 0.02f}, {head_radius_x * 0.92f, head_center_y - 0.02f, 0.02f}};
    for (int e = 0; e < 2; e++) {
        float ex = ear_centers[e][0];
        float ey = ear_centers[e][1];
        float ez = ear_centers[e][2];
        float ear_w = 0.065f;
        float ear_h = 0.12f;

        int ear_res = 16;
        for (int k = 0; k < ear_res; k++) {
            float a1 = (float)k * 2.0f * 3.14159f / (float)ear_res;
            float a2 = (float)(k + 1) * 2.0f * 3.14159f / (float)ear_res;

            float p1_x = ex + ((e == 0) ? -0.035f : 0.035f);
            float p1_y = ey + ear_h * sinf(a1);
            float p1_z = ez + ear_w * cosf(a1);

            float p2_x = ex + ((e == 0) ? -0.035f : 0.035f);
            float p2_y = ey + ear_h * sinf(a2);
            float p2_z = ez + ear_w * cosf(a2);

            head_verts_female[hv_idx_f++] = ex; head_verts_female[hv_idx_f++] = ey; head_verts_female[hv_idx_f++] = ez;
            head_verts_female[hv_idx_f++] = p1_x; head_verts_female[hv_idx_f++] = p1_y; head_verts_female[hv_idx_f++] = p1_z;
            head_verts_female[hv_idx_f++] = p2_x; head_verts_female[hv_idx_f++] = p2_y; head_verts_female[hv_idx_f++] = p2_z;

            head_verts_male[hv_idx_m++] = ex; head_verts_male[hv_idx_m++] = ey; head_verts_male[hv_idx_m++] = ez;
            head_verts_male[hv_idx_m++] = p1_x; head_verts_male[hv_idx_m++] = p1_y; head_verts_male[hv_idx_m++] = p1_z;
            head_verts_male[hv_idx_m++] = p2_x; head_verts_male[hv_idx_m++] = p2_y; head_verts_male[hv_idx_m++] = p2_z;
        }
    }

    HairVertex* hair_verts1 = malloc(head_hair[0].strand_count * head_hair[0].segments * sizeof(HairVertex));
    HairVertex* hair_verts2 = malloc(head_hair[1].strand_count * head_hair[1].segments * sizeof(HairVertex));

    for (int h = 0; h < 2; h++) {
        HairVertex* hv = (h == 0) ? hair_verts1 : hair_verts2;
        int sc = head_hair[h].strand_count;
        int segs = head_hair[h].segments;
        float len = head_hair[h].length;
        int is_short = head_hair[h].is_short;

        for (int s = 0; s < sc; s++) {
            float phi = acosf(1.0f - 2.0f * ((float)s + 0.5f) / (float)sc);
            float theta = 2.39996f * (float)s;

            // Improved natural female hairline framing (receding slightly at the center forehead for realism)
            if (h == 0) {
                float z_norm = sinf(phi) * sinf(theta);
                float y_norm = cosf(phi);
                if (z_norm > 0.20f && y_norm > -0.15f && y_norm < 0.35f && fabsf(theta - 3.14159f) < 1.0f) {
                    phi += 0.25f; // Push root back to form a clean hairline curve
                }
            }

            if (is_short) {
                if (phi > 2.75f) phi = 2.75f; 
            } else {
                if (phi > 1.45f && len < 0.6f) phi = 1.45f * (1.45f / phi);
            }

            float rx = (h == 1 ? head_radius_x * 1.12f : head_radius_x * 0.96f) * sinf(phi) * cosf(theta);
            float ry = (h == 1 ? head_radius_y * 1.02f : head_radius_y * 1.04f) * cosf(phi) + head_center_y;
            float rz = (h == 1 ? head_radius_z * 1.15f : head_radius_z * 0.96f) * sinf(phi) * sinf(theta);

            for (int seg = 0; seg < segs; seg++) {
                int idx = s * segs + seg;
                float t = (float)seg / (float)(segs - 1);
                
                if (is_short) {
                    float crown_factor = fmaxf(0.0f, cosf(phi));
                    float side_taper = 1.0f - 0.28f * fmaxf(0.0f, sinf(phi) * fabsf(cosf(theta)));
                    float outward = (1.0f + crown_factor * 0.22f + t * len * 0.08f) * side_taper;
                    
                    float directional_sweep = sinf(theta * 2.0f) * 0.04f * t;
                    hv[idx].ox = (rx + directional_sweep) * outward;
                    hv[idx].oy = ry * outward + crown_factor * 0.06f * t;
                    hv[idx].oz = rz * outward;
                } else {
                    // Layered absolute length and graceful flowing hairstyle for the woman
                    float wave_effect = sinf(t * 4.0f + (float)s * 0.1f) * 0.08f * t;
                    hv[idx].ox = rx + wave_effect;
                    hv[idx].oy = ry - t * len;
                    hv[idx].oz = rz + wave_effect * 0.5f;
                }
                
                hv[idx].x = hv[idx].ox;
                hv[idx].y = hv[idx].oy;
                hv[idx].z = hv[idx].oz;
                hv[idx].vx = hv[idx].vy = hv[idx].vz = 0.0f;
            }
        }
    }

    int total_raindrops = 2400;
    int total_rain_verts = total_raindrops * 2;
    RainDrop* rain_drops = malloc(total_raindrops * sizeof(RainDrop));
    float* rain_vert_data = malloc(total_rain_verts * 3 * sizeof(float));

    for (int i = 0; i < total_raindrops; i++) {
        rain_drops[i].x = ((float)(rand() % 600) / 100.0f - 3.0f) * 1.5f;
        rain_drops[i].y = ((float)(rand() % 400) / 100.0f - 2.0f) * 1.5f;
        rain_drops[i].z = ((float)(rand() % 200) / 100.0f - 1.0f) * 1.5f;
        rain_drops[i].vx = 0.0f; 
        rain_drops[i].vy = -8.0f - ((float)(rand() % 400) / 100.0f);
        rain_drops[i].vz = 0.0f;
    }

    GLuint vboSky, vboHeadFemale, vboHeadMale, vboHair1, vboHair2, vboRain, vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vboSky);
    glBindBuffer(GL_ARRAY_BUFFER, vboSky);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skybox_verts), skybox_verts, GL_STATIC_DRAW);

    glGenBuffers(1, &vboHeadFemale);
    glBindBuffer(GL_ARRAY_BUFFER, vboHeadFemale);
    glBufferData(GL_ARRAY_BUFFER, head_vert_count_female * 3 * sizeof(float), head_verts_female, GL_STATIC_DRAW);

    glGenBuffers(1, &vboHeadMale);
    glBindBuffer(GL_ARRAY_BUFFER, vboHeadMale);
    glBufferData(GL_ARRAY_BUFFER, head_vert_count_male * 3 * sizeof(float), head_verts_male, GL_STATIC_DRAW);

    glGenBuffers(1, &vboHair1);
    glBindBuffer(GL_ARRAY_BUFFER, vboHair1);
    glBufferData(GL_ARRAY_BUFFER, head_hair[0].strand_count * head_hair[0].segments * sizeof(HairVertex), hair_verts1, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &vboHair2);
    glBindBuffer(GL_ARRAY_BUFFER, vboHair2);
    glBufferData(GL_ARRAY_BUFFER, head_hair[1].strand_count * head_hair[1].segments * sizeof(HairVertex), hair_verts2, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &vboRain);
    glBindBuffer(GL_ARRAY_BUFFER, vboRain);
    glBufferData(GL_ARRAY_BUFFER, total_rain_verts * 3 * sizeof(float), rain_vert_data, GL_DYNAMIC_DRAW);

    char ffmpeg_cmd[768];
    snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd), 
             "ffmpeg -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -vf \"fps=%d,scale=854:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse\" %s", 
             cfg.width, cfg.height, cfg.fps, cfg.fps, cfg.output_file);
             
    FILE* ffmpeg = popen(ffmpeg_cmd, "w");
    unsigned char* pixel_buffer = malloc(cfg.width * cfg.height * 4);
    const float dt = 1.0f / (float)cfg.fps;

    printf("--- Realistic Hairline & Distinct Color Engine Running ---\n");
    printf("Palette Index: %d | Rain: %.2f | Wind: %.2f\n", palette_idx, cfg.rain_intensity, cfg.wind_force);
    printf("Output: %s\n", cfg.output_file);
    printf("----------------------------------------------------\n");

    for (int frame = 0; frame < total_frames; frame++) {
        float time = (float)frame * dt;

        for (int h = 0; h < 2; h++) {
            HairVertex* hv = (h == 0) ? hair_verts1 : hair_verts2;
            int sc = head_hair[h].strand_count;
            int segs = head_hair[h].segments;
            int htype = head_hair[h].hair_type;
            float len = head_hair[h].length;
            int is_wet = head_hair[h].is_wet;
            int is_short = head_hair[h].is_short;

            for (int s = 0; s < sc; s++) {
                for (int seg = 0; seg < segs; seg++) {
                    int idx = s * segs + seg;
                    if (seg == 0) continue; 

                    float t = (float)seg / (float)(segs - 1);
                    HairVertex* v = &hv[idx];

                    float gust_x = cfg.wind_force * (sinf(time * 2.4f + (float)s * 0.17f + v->ox * 5.0f) * 1.8f + cosf(time * 1.7f - v->oz * 3.5f) * 1.2f);
                    float gust_z = cfg.wind_force * (cosf(time * 2.1f + (float)s * 0.13f + v->oz * 4.5f) * 1.8f + sinf(time * 1.9f + v->ox * 3.0f) * 1.2f);
                    float gust_y = cfg.wind_force * 0.5f * sinf(time * 3.2f + (float)s * 0.09f);

                    float target_x, target_y, target_z;

                    if (is_short) {
                        float crown_factor = fmaxf(0.0f, cosf(acosf(1.0f - 2.0f * ((float)s + 0.5f) / (float)sc)));
                        float side_taper = 1.0f - 0.28f * fmaxf(0.0f, sinf(acosf(1.0f - 2.0f * ((float)s + 0.5f) / (float)sc)) * fabsf(cosf(2.39996f * (float)s)));
                        float outward = (1.0f + crown_factor * 0.22f + t * len * 0.08f) * side_taper;
                        
                        target_x = v->ox * outward + (gust_x * 0.15f * t);
                        target_y = v->oy * outward;
                        target_z = v->oz * outward;
                    } else if (is_wet) {
                        float saturation_drag = 1.0f + cfg.rain_intensity * 0.15f;
                        target_x = v->ox + (gust_x * 0.45f * t) / saturation_drag;
                        target_y = v->oy - t * len * (1.0f + cfg.rain_intensity * 0.05f);
                        target_z = v->oz + (gust_z * 0.45f * t) / saturation_drag;
                    } else {
                        float split_factor = 1.0f + 0.8f * sinf((float)s * 1.3f + time * 1.5f);
                        target_x = v->ox + gust_x * t * 1.8f * split_factor + sinf(t * 6.0f + time * 2.2f) * 0.12f * t;
                        target_y = v->oy - t * len + gust_y * t;
                        target_z = v->oz + gust_z * t * 1.8f * split_factor + cosf(t * 5.0f + time * 1.8f) * 0.12f * t;

                        if (htype == 1) {
                            target_x += sinf(t * 10.0f + time * 1.5f) * 0.15f * t;
                        } else if (htype == 2) {
                            target_x += cosf(t * 16.0f + time * 2.5f) * 0.2f * t;
                            target_z += sinf(t * 16.0f + time * 2.5f) * 0.2f * t;
                        } else if (htype == 3) {
                            float flare = (t > 0.4f) ? 0.4f * (t - 0.4f) : 0.0f;
                            target_x += sinf(t * 22.0f + time * 3.2f) * flare;
                            target_z += cosf(t * 22.0f + time * 3.2f) * flare;
                        }
                    }

                    float stiffness = is_short ? 65.0f : (is_wet ? 45.0f : 24.0f);
                    float damping = is_short ? 5.0f : (is_wet ? 4.0f : 2.0f);

                    float fx = (target_x - v->x) * stiffness;
                    float fy = -9.81f * (is_wet ? 1.4f : 1.0f) + (target_y - v->y) * stiffness;
                    float fz = (target_z - v->z) * stiffness;

                    v->vx += (fx - v->vx * damping) * dt;
                    v->vy += (fy - v->vy * damping) * dt;
                    v->vz += (fz - v->vz * damping) * dt;

                    v->x += v->vx * dt;
                    v->y += v->vy * dt;
                    v->z += v->vz * dt;
                }
            }
        }

        for (int i = 0; i < total_raindrops; i++) {
            rain_drops[i].vy += -9.81f * dt;
            rain_drops[i].y += rain_drops[i].vy * dt;
            rain_drops[i].x += cfg.wind_force * 1.5f * dt;

            if (rain_drops[i].y < -1.8f || fabsf(rain_drops[i].x) > 3.2f) {
                rain_drops[i].y = 2.0f + ((float)(rand() % 100) / 100.0f);
                rain_drops[i].vy = -7.0f - ((float)(rand() % 400) / 100.0f);
                rain_drops[i].x = ((float)(rand() % 600) / 100.0f - 3.0f) * 1.2f;
                rain_drops[i].z = ((float)(rand() % 200) / 100.0f - 1.0f) * 1.5f;
            }

            int vidx = i * 6;
            rain_vert_data[vidx + 0] = rain_drops[i].x;
            rain_vert_data[vidx + 1] = rain_drops[i].y;
            rain_vert_data[vidx + 2] = rain_drops[i].z;
            
            rain_vert_data[vidx + 3] = rain_drops[i].x - cfg.wind_force * 0.05f;
            rain_vert_data[vidx + 4] = rain_drops[i].y + 0.3f * cfg.rain_intensity;
            rain_vert_data[vidx + 5] = rain_drops[i].z;
        }

        glBindBuffer(GL_ARRAY_BUFFER, vboHair1);
        glBufferSubData(GL_ARRAY_BUFFER, 0, head_hair[0].strand_count * head_hair[0].segments * sizeof(HairVertex), hair_verts1);

        glBindBuffer(GL_ARRAY_BUFFER, vboHair2);
        glBufferSubData(GL_ARRAY_BUFFER, 0, head_hair[1].strand_count * head_hair[1].segments * sizeof(HairVertex), hair_verts2);

        glBindBuffer(GL_ARRAY_BUFFER, vboRain);
        glBufferSubData(GL_ARRAY_BUFFER, 0, total_rain_verts * 3 * sizeof(float), rain_vert_data);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(renderProg);
        
        glUniform1f(glGetUniformLocation(renderProg, "u_time"), time);
        glUniform1f(glGetUniformLocation(renderProg, "u_wind_force"), cfg.wind_force);
        glUniform1f(glGetUniformLocation(renderProg, "u_rain_intensity"), cfg.rain_intensity);
        glUniform3f(glGetUniformLocation(renderProg, "u_sky_top"), cfg.sky_top[0], cfg.sky_top[1], cfg.sky_top[2]);
        glUniform3f(glGetUniformLocation(renderProg, "u_sky_horizon"), cfg.sky_horizon[0], cfg.sky_horizon[1], cfg.sky_horizon[2]);
        glUniform3f(glGetUniformLocation(renderProg, "u_ground_color"), cfg.ground_color[0], cfg.ground_color[1], cfg.ground_color[2]);

        float aspect = (float)cfg.width / (float)cfg.height;
        float proj[16], view[16], mvp[16], model[16];
        getPerspective(proj, 45.0f * (3.14159f / 180.0f), aspect, 0.1f, 100.0f);
        getIdentity(view);
        view[14] = -3.8f;  
        view[13] = -0.1f; 
        multiplyMatrix(mvp, proj, view);
        
        glUniformMatrix4fv(glGetUniformLocation(renderProg, "u_mvp"), 1, GL_FALSE, mvp);
        glUniform3f(glGetUniformLocation(renderProg, "u_light_dir"), cfg.light_dir[0], cfg.light_dir[1], cfg.light_dir[2]);
        glUniform3f(glGetUniformLocation(renderProg, "u_moon_dir"), cfg.moon_dir[0], cfg.moon_dir[1], cfg.moon_dir[2]);

        glDepthMask(GL_FALSE);
        glUniform1i(glGetUniformLocation(renderProg, "u_render_mode"), 3);
        getModelTranslation(model, 0.0f, 0.0f, 0.0f);
        glUniformMatrix4fv(glGetUniformLocation(renderProg, "u_model"), 1, GL_FALSE, model);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vboSky);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDepthMask(GL_TRUE);

        float head_x_offsets[2] = {-0.85f, 0.85f};

        for (int h = 0; h < 2; h++) {
            getModelTranslation(model, head_x_offsets[h], 0.0f, 0.0f);
            glUniformMatrix4fv(glGetUniformLocation(renderProg, "u_model"), 1, GL_FALSE, model);
            glUniform3f(glGetUniformLocation(renderProg, "u_color"), cfg.color[h][0], cfg.color[h][1], cfg.color[h][2]);
            glUniform3f(glGetUniformLocation(renderProg, "u_skin_color"), cfg.skin_color[h][0], cfg.skin_color[h][1], cfg.skin_color[h][2]);
            glUniform1i(glGetUniformLocation(renderProg, "u_is_male"), h);

            glUniform1i(glGetUniformLocation(renderProg, "u_render_mode"), 0);
            glBindBuffer(GL_ARRAY_BUFFER, (h == 0) ? vboHeadFemale : vboHeadMale);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            glDrawArrays(GL_TRIANGLES, 0, (h == 0) ? head_vert_count_female : head_vert_count_male);

            glUniform1i(glGetUniformLocation(renderProg, "u_render_mode"), 1);
            int sc = head_hair[h].strand_count;
            int segs = head_hair[h].segments;
            glBindBuffer(GL_ARRAY_BUFFER, (h == 0) ? vboHair1 : vboHair2);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HairVertex), (void*)0);
            for (int s = 0; s < sc; s++) {
                glDrawArrays(GL_LINE_STRIP, s * segs, segs);
            }
        }

        glUniform1i(glGetUniformLocation(renderProg, "u_render_mode"), 2);
        getModelTranslation(model, 0.0f, 0.0f, 0.0f);
        glUniformMatrix4fv(glGetUniformLocation(renderProg, "u_model"), 1, GL_FALSE, model);
        glBindBuffer(GL_ARRAY_BUFFER, vboRain);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glDrawArrays(GL_LINES, 0, total_rain_verts);

        glReadPixels(0, 0, cfg.width, cfg.height, GL_RGBA, GL_UNSIGNED_BYTE, pixel_buffer);
        fwrite(pixel_buffer, 1, cfg.width * cfg.height * 4, ffmpeg);
    }

    pclose(ffmpeg);
    free(pixel_buffer);
    free(head_verts_female);
    free(head_verts_male);
    free(hair_verts1);
    free(hair_verts2);
    free(rain_drops);
    free(rain_vert_data);
    glDeleteBuffers(1, &vboSky);
    glDeleteBuffers(1, &vboHeadFemale);
    glDeleteBuffers(1, &vboHeadMale);
    glDeleteBuffers(1, &vboHair1);
    glDeleteBuffers(1, &vboHair2);
    glDeleteBuffers(1, &vboRain);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(renderProg);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
