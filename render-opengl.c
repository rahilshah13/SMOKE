#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_GLCOREARB  
#include <GLFW/glfw3.h>

#define W 720
#define H 720
#define FPS 60
#define DURATION_SECS 15
#define TOTAL_FRAMES (FPS * DURATION_SECS)

#define OBJ_CIGARETTE 1
#define OBJ_LIGHTER   2
#define OBJ_MOUTH     3

typedef struct {
    int type;
    int shape_type;     // 0: Cylinder, 1: RoundBox, 2: Lips
    float params[4];    // Geometry configuration dims: [r, h, 0, 0] or [b.x, b.y, b.z, r]
    float offset[3];    // Static translational offset anchors
} SceneObject;

const SceneObject WORLD_REGISTRY[3] = {
    { OBJ_CIGARETTE, 0, { 0.16f, 2.50f, 0.00f, 0.00f }, { 0.00f,  0.00f,  0.00f } },
    { OBJ_LIGHTER,   1, { 0.25f, 0.50f, 0.18f, 0.04f }, { 0.00f, -0.60f,  2.50f } },
    { OBJ_MOUTH,     2, { 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f,  0.00f, -2.85f } } 
};

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
"uniform float u_lighter_x;\n" // Decoupled uniform component dynamic translation
"uniform float u_flame_intensity;\n"
"uniform float u_ember_temp;\n"
"uniform float u_suction;\n"
"uniform float u_burn_z;\n"

"float hash(vec3 p) {\n"
"    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123);\n"
"}\n"
"float noise(vec3 p) {\n"
"    vec3 i = floor(p); vec3 f = fract(p);\n"
"    vec3 u = f * f * (3.0 - 2.0 * f);\n"
"    return mix(mix(mix(hash(i+vec3(0,0,0)), hash(i+vec3(1,0,0)), u.x),\n"
"                   mix(hash(i+vec3(0,1,0)), hash(i+vec3(1,1,0)), u.x), u.y),\n"
"               mix(mix(hash(i+vec3(0,0,1)), hash(i+vec3(1,0,1)), u.x),\n"
"                   mix(hash(i+vec3(0,1,1)), hash(i+vec3(1,1,1)), u.x), u.y), u.z);\n"
"}\n"
"float fbm(vec3 p) {\n"
"    float v = 0.0; float a = 0.5;\n"
"    for (int i=0; i<5; i++) { v += a * noise(p); p *= 2.02; a *= 0.5; }\n"
"    return v;\n"
"}\n"

"float sdCylinder(vec3 p, float h, float r) {\n"
"    vec2 d = abs(vec2(length(p.xy), p.z)) - vec2(r, h);\n"
"    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));\n"
"}\n"
"float sdRoundBox(vec3 p, vec3 b, float r) {\n"
"    vec3 q = abs(p) - b + vec3(r);\n"
"    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;\n"
"}\n"
"float sdLips(vec3 p) {\n"
"    vec3 p1 = p - vec3(0.0, 0.15, 0.0);\n"
"    float topLip = length(vec2(length(p1.xz * vec2(0.6, 1.0)) - 0.8, p1.y * 2.0)) - 0.15;\n"
"    vec3 p2 = p - vec3(0.0, -0.15, 0.0);\n"
"    float botLip = length(vec2(length(p2.xz * vec2(0.6, 1.0)) - 0.8, p2.y * 1.6)) - 0.22;\n"
"    return min(topLip, botLip) * 0.6;\n"
"}\n"

// --- GPU UNROLLED STRUCTURAL EVALUATION ENGINE ---
"float mapScene(vec3 p, out int objType) {\n"
"    // Object 1: Cigarette (Static Axis Map)\n"
"    float d = sdCylinder(p - vec3(0.0, 0.0, 0.0), 2.50, 0.16);\n"
"    objType = 1;\n"
"    \n"
"    // Object 2: Lighter (Dynamic Position tracking uniform scalar shift)\n"
"    float dLighter = sdRoundBox(p - vec3(u_lighter_x, -0.60, 2.50), vec3(0.25, 0.50, 0.18), 0.04);\n"
"    if (dLighter < d) { d = dLighter; objType = 2; }\n"
"    \n"
"    // Object 3: Mouth\n"
"    float dMouth = sdLips(p - vec3(0.0, 0.0, -2.85));\n"
"    if (dMouth < d) { d = dMouth; objType = 3; }\n"
"    \n"
"    return d;\n"
"}\n"

"vec3 getNormal(vec3 p) {\n"
"    int dummy;\n"
"    vec2 e = vec2(0.001, 0.0);\n"
"    return normalize(vec3(mapScene(p+e.xyy, dummy) - mapScene(p-e.xyy, dummy),\n"
"                          mapScene(p+e.yxy, dummy) - mapScene(p-e.yxy, dummy),\n"
"                          mapScene(p+e.yyx, dummy) - mapScene(p-e.yyx, dummy)));\n"
"}\n"

"void main() {\n"
"    vec2 coord = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;\n"
"    vec3 ro = vec3(7.2, 2.8, 1.8);\n"
"    vec3 ta = vec3(0.0, 0.0, -0.2);\n"
"    vec3 cw = normalize(ta - ro);\n"
"    vec3 cu = normalize(cross(cw, vec3(0.0, 1.0, 0.0)));\n"
"    vec3 cv = normalize(cross(cu, cw));\n"
"    vec3 rd = normalize(coord.x * cu + coord.y * cv + 2.5 * cw);\n"
"    \n"
"    vec3 sceneColor = vec3(0.015, 0.015, 0.018);\n"
"    float t = 0.0; int objType = 0; vec3 hitPos;\n"
"    \n"
"    for(int i=0; i<100; i++) {\n"
"        vec3 p = ro + rd * t;\n"
"        float res = mapScene(p, objType);\n"
"        if (res < 0.0005) { hitPos = p; break; }\n"
"        t += res;\n"
"        if (t > 15.0) { objType = 0; break; }\n"
"    }\n"
"    \n"
"    if (objType == 1) {\n"
"        vec3 n = getNormal(hitPos);\n"
"        float diff = max(dot(n, normalize(vec3(1.5, 3.0, 1.0))), 0.0);\n"
"        if (hitPos.z < -1.2) {\n"
"            sceneColor = vec3(0.68, 0.41, 0.18) * (diff + 0.1);\n"
"        } else if (hitPos.z > -1.2 && hitPos.z < -0.9) {\n"
"            sceneColor = vec3(0.82, 0.64, 0.22) * (diff + 0.3);\n"
"        } else if (hitPos.z < u_burn_z) {\n"
"            vec3 paperColor = vec3(0.94, 0.94, 0.95) * (diff + 0.08);\n"
"            float scorchEdge = smoothstep(u_burn_z - 0.18, u_burn_z, hitPos.z);\n"
"            sceneColor = mix(paperColor, vec3(0.22, 0.12, 0.06) * (diff + 0.02), scorchEdge);\n"
"        } else {\n"
"            float distFront = hitPos.z - u_burn_z;\n"
"            float oxygen = fbm(hitPos * 22.0 + vec3(0.0, 0.0, u_time * 4.0));\n"
"            float boundaryGlow = smoothstep(0.12, 0.0, distFront);\n"
"            float coreHeat = u_ember_temp * (boundaryGlow * 0.75 + 0.25) * (oxygen * 0.5 + 0.5);\n"
"            vec3 incandescentGlow = mix(vec3(0.45, 0.012, 0.0), vec3(4.5, 1.4, 0.06), coreHeat);\n"
"            sceneColor = mix(incandescentGlow, vec3(0.22, 0.22, 0.25) * (diff + 0.05), smoothstep(0.02, 0.45, distFront));\n"
"        }\n"
"    } else if (objType == 2) {\n"
"        vec3 n = getNormal(hitPos);\n"
"        float diff = max(dot(n, normalize(vec3(1.0, 2.5, 1.0))), 0.0);\n"
"        float spec = pow(max(dot(reflect(normalize(vec3(1.0, 2.5, 1.0)), n), rd), 0.0), 16.0);\n"
"        sceneColor = vec3(0.9, 0.03, 0.03) * (diff + 0.15) + vec3(0.4) * spec;\n"
"    } else if (objType == 3) {\n"
"        vec3 n = getNormal(hitPos);\n"
"        float diff = max(dot(n, normalize(vec3(1.0, 1.5, 2.0))), 0.0);\n"
"        sceneColor = vec3(0.62, 0.31, 0.33) * (diff + 0.1);\n"
"    }\n"
"    \n"
"    // --- FLUID SIMULATION ELEMENT ---\n"
"    float tm = 0.0; float trans = 1.0; vec3 volAccum = vec3(0.0);\n"
"    for(int i=0; i<75; i++) {\n"
"        vec3 p = ro + rd * tm;\n"
"        if(tm > t) break;\n"
"        \n"
"        if (u_flame_intensity > 0.0) {\n"
"            vec3 pFlame = p - vec3(u_lighter_x, -0.60 + 0.55, 2.50);\n"
"            float flicker = fbm(pFlame * 6.0 - vec3(0.0, u_time * 12.0, 0.0)) * 0.15;\n"
"            pFlame.y /= (1.6 + flicker); \n"
"            float dFlame = length(pFlame) - (0.16 * u_flame_intensity);\n"
"            if (dFlame < 0.0) {\n"
"                float flameDensity = smoothstep(0.16 * u_flame_intensity, -0.05, dFlame);\n"
"                volAccum += mix(vec3(0.02, 0.15, 1.4), vec3(2.8, 1.2, 0.15), smoothstep(-0.1, 0.28, pFlame.y / 0.6)) * flameDensity * trans * 0.35;\n"
"                trans *= exp(-flameDensity * 0.55);\n"
"            }\n"
"        }\n"
"        \n"
"        if (u_ember_temp > 0.05) {\n"
"            vec3 smokeOrigin = vec3(0.0, 0.0, u_burn_z);\n"
"            float distFromTip = max(0.0, smokeOrigin.z - p.z);\n"
"            vec3 currentFluidTrack = smokeOrigin + vec3(0.0, 1.0, 0.0) * pow(distFromTip, 1.1) * 0.6 + vec3(0.0, 0.0, -1.0) * distFromTip * u_suction * 1.2;\n"
"            float plumeRadius = mix(0.06, 0.5, distFromTip * 0.25);\n"
"            float dSmoke = length(p.xy - currentFluidTrack.xy) - plumeRadius;\n"
"            \n"
"            if (dSmoke < 0.0 && p.z < (u_burn_z + 0.05) && p.z > -2.8) {\n"
"                float turbulence = fbm(p * 4.5 + vec3(0.0, -u_time * 2.5 - u_suction * 7.0, u_time * 3.0));\n"
"                float smokeDensity = smoothstep(plumeRadius, -plumeRadius * 0.5, dSmoke) * turbulence * smoothstep(4.5, 0.2, distFromTip);\n"
"                volAccum += vec3(0.64, 0.67, 0.72) * mix(0.4, 1.0, turbulence) * smokeDensity * trans * 0.18;\n"
"                trans *= exp(-smokeDensity * 0.28);\n"
"            }\n"
"        }\n"
"        tm += 0.12;\n"
"    } \n"
"    vec3 finalComposition = sceneColor * trans + volAccum;\n"
"    fragColor = vec4(clamp((finalComposition*(2.51*finalComposition+0.03))/(finalComposition*(2.43*finalComposition+0.59)+0.14),0.0,1.0), 1.0);\n"
"}\n";


int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); 
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); 
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    GLFWwindow* window = glfwCreateWindow(W, H, "Structured Physics Array Engine", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    GLuint fbo, renderbuffer;
    glGenFramebuffers(1, &fbo); 
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &renderbuffer); 
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
    glViewport(0, 0, W, H);

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER); 
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL); 
    glCompileShader(vertexShader);
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER); 
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL); 
    glCompileShader(fragmentShader);
    GLuint shaderProgram = glCreateProgram(); 
    glAttachShader(shaderProgram, vertexShader); 
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram); glUseProgram(shaderProgram);
    float vertices[] = { -1.0f,-1.0f, 1.0f,-1.0f, -1.0f,1.0f, -1.0f,1.0f, 1.0f,-1.0f, 1.0f,1.0f };
    GLuint vao, vbo; 
    glGenVertexArrays(1, &vao); 
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo); 
    glBindBuffer(GL_ARRAY_BUFFER, vbo); 
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    GLint posAttrib = glGetAttribLocation(shaderProgram, "position"); 
    glEnableVertexAttribArray(posAttrib); 
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
    GLint resUniform = glGetUniformLocation(shaderProgram, "u_resolution");
    GLint timeUniform = glGetUniformLocation(shaderProgram, "u_time");
    GLint lighterXUniform = glGetUniformLocation(shaderProgram, "u_lighter_x");
    GLint flameUniform = glGetUniformLocation(shaderProgram, "u_flame_intensity");
    GLint emberUniform = glGetUniformLocation(shaderProgram, "u_ember_temp");
    GLint suctionUniform = glGetUniformLocation(shaderProgram, "u_suction");
    GLint burnZUniform = glGetUniformLocation(shaderProgram, "u_burn_z");
    glUniform2f(resUniform, (float)W, (float)H);
    char ffmpeg_cmd[512];
    snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd), "ffmpeg -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -c:v libx264 -crf 15 -pix_fmt yuv420p opengl_cig.mp4", W, H, FPS);
    FILE* ffmpeg = popen(ffmpeg_cmd, "w");
    unsigned char* pixel_buffer = malloc(W * H * 4);

    float burn_z = 2.38f;        
    float ember_temp = 0.0f;
    const float dt = 1.0f / (float)FPS;
    printf("[Static Structure Engine Active] Processing time steps...\n");

    for (int frame = 0; frame < TOTAL_FRAMES; frame++) {
        float time = (float)frame * dt;
        float lighter_x     = (time < 1.5f)  ? 3.5f * (1.0f - (time / 1.5f)) : ((time < 4.0f) ? 0.0f : fminf(4.5f, (time - 4.0f) * 3.0f));
        float flame_state   = (time >= 1.5f  && time < 4.0f) ? 1.0f : 0.0f;
        float suction = 0.0f;
        float start[] = {5.0f, 9.5f, 13.5f, 20.0f, 25.0f};
        for(int i = 0; i < 3; i++) 
            if(time >= start[i] && time < start[i] + 3.0f) suction = sinf(((time - start[i]) / 3.0f) * 3.14159265f);

        ember_temp += ((time < 4.0f) ? (time > 1.5f ? (time - 1.5f) / 2.5f * 0.4f : 0.0f) 
             : (time < 17.0f ? (0.22f + suction * 0.78f - ember_temp) * (ember_temp < (0.22f + suction * 0.78f) ? 8.0f : 1.5f) * dt 
             : (0.0f - ember_temp) * 0.8f * dt)) - (time < 4.0f ? ember_temp : 0.0f);

        if (burn_z > -0.9f) burn_z -= ((ember_temp > 0.08f ? 0.012f : 0.0f) + (suction * 0.24f)) * dt;

        glUniform1f(timeUniform, time);
        glUniform1f(lighterXUniform, lighter_x);
        glUniform1f(flameUniform, flame_state);
        glUniform1f(emberUniform, ember_temp);
        glUniform1f(suctionUniform, suction);
        glUniform1f(burnZUniform, burn_z);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixel_buffer);
        fwrite(pixel_buffer, 1, W * H * 4, ffmpeg);
    }

    pclose(ffmpeg); 
    free(pixel_buffer);
    glDeleteBuffers(1, &vbo); 
    glDeleteVertexArrays(1, &vao);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteProgram(shaderProgram); 
    glfwDestroyWindow(window); 
    glfwTerminate();
    return 0;
}