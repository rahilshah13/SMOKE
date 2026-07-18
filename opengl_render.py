import OpenGL.GL as gl
import glfw, sys, time, subprocess, imageio
import numpy as np
from OpenGL.GL.shaders import compileProgram, compileShader

# --- CONFIGURATION CONSTANTS ---
WIDTH = 480
HEIGHT = 480
NUM_FRAMES = 56

# --- SHADERS ---
VERTEX_SHADER = """
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in float aSize;

out vec3 vColor;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vColor = aColor / 255.0; // Normalize color to 0.0-1.0
    vec4 viewPos = view * model * vec4(aPos, 1.0);
    gl_Position = projection * viewPos;
    // Perspective Point Scaling: Closer points are larger
    gl_PointSize = max(1.0, aSize * (40.0 / -viewPos.z));
}
"""

FRAGMENT_SHADER = """
#version 330 core
in vec3 vColor;
out vec4 FragColor;

void main() {
    // Map square gl_PointCoord (0.0 to 1.0) into circular space (-1.0 to 1.0)
    vec2 pt = gl_PointCoord * 2.0 - 1.0;
    float r = dot(pt, pt);
    if (r > 1.0) discard;
    vec3 normal = vec3(pt.x, -pt.y, sqrt(1.0 - r));
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.8));
    float diff = max(dot(normal, lightDir), 0.0);
    float rim = smoothstep(0.5, 1.0, 1.0 - max(dot(normal, vec3(0.0, 0.0, 1.0)), 0.0));
    vec3 ambient = vColor * 0.3;
    vec3 diffuse = vColor * diff;
    vec3 finalColor = ambient + diffuse + vec3(rim * 0.4);    
    float alpha = 1.0 - (r * r);
    FragColor = vec4(finalColor, alpha * 0.9);
}
"""

def create_matrices(angle):
    rad = np.radians(angle)
    c, s = np.cos(rad), np.sin(rad)
    
    model = np.array([[c, 0, s, 0], [0, 1, 0, 0], [-s, 0, c, 0], [0, 0, 0, 1]], dtype=np.float32)
    view = np.array([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, -15.0], [0, 0, 0, 1]], dtype=np.float32)
    
    # Projection (Perspective)
    f = 1.0 / np.tan(np.radians(45.0) / 2.0)
    proj = np.array([
        [f, 0, 0, 0],
        [0, f, 0, 0],
        [0, 0, (100.0+0.1)/(0.1-100.0), (2*100.0*0.1)/(0.1-100.0)],
        [0, 0, -1, 0]
    ], dtype=np.float32)
    
    return model, view, proj

def run():
    num_scenes = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    
    glfw.init()
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
    glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, gl.GL_TRUE)
    glfw.window_hint(glfw.VISIBLE, glfw.FALSE)
    window = glfw.create_window(WIDTH, HEIGHT, "Shader Renderer", None, None)
    glfw.make_context_current(window)
    
    # MAC SPECIFIC FIX: Bind a global VAO before compiling/validating shaders
    global_vao = gl.glGenVertexArrays(1)
    gl.glBindVertexArray(global_vao)
    shader = compileProgram(compileShader(VERTEX_SHADER, gl.GL_VERTEX_SHADER),
                            compileShader(FRAGMENT_SHADER, gl.GL_FRAGMENT_SHADER))

    gl.glEnable(gl.GL_DEPTH_TEST)
    gl.glEnable(gl.GL_BLEND)
    gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE)
    gl.glEnable(gl.GL_PROGRAM_POINT_SIZE)

    for i in range(1, num_scenes + 1):
        seed = int(time.time() * 1000) + i
        subprocess.run(['tpl', 'scene.pl', '-g', f'emit_scenes({i}, {seed}), halt'])
        
        with open(f"scene_{i}.txt", "r") as f:
            raw_data = [float(x) for x in f.read().split()]
        
        vertices = np.array(raw_data, dtype=np.float32)
        stride = 7 * vertices.itemsize
        
        vbo = gl.glGenBuffers(1)
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, vbo)
        gl.glBufferData(gl.GL_ARRAY_BUFFER, vertices.nbytes, vertices, gl.GL_STATIC_DRAW)
        # Attribute 0: Position (X, Y, Z)
        gl.glEnableVertexAttribArray(0)
        gl.glVertexAttribPointer(0, 3, gl.GL_FLOAT, gl.GL_FALSE, stride, gl.ctypes.c_void_p(0))
        # Attribute 1: Color (R, G, B)
        gl.glEnableVertexAttribArray(1)
        gl.glVertexAttribPointer(1, 3, gl.GL_FLOAT, gl.GL_FALSE, stride, gl.ctypes.c_void_p(3 * vertices.itemsize))
        # Attribute 2: Size (S)
        gl.glEnableVertexAttribArray(2)
        gl.glVertexAttribPointer(2, 1, gl.GL_FLOAT, gl.GL_FALSE, stride, gl.ctypes.c_void_p(6 * vertices.itemsize))

        def render_frame(angle):
            gl.glClearColor(0.005, 0.01, 0.02, 1.0)
            gl.glClear(gl.GL_COLOR_BUFFER_BIT | gl.GL_DEPTH_BUFFER_BIT)
            gl.glUseProgram(shader)
            
            model, view, proj = create_matrices(angle)
            gl.glUniformMatrix4fv(gl.glGetUniformLocation(shader, "model"), 1, gl.GL_TRUE, model)
            gl.glUniformMatrix4fv(gl.glGetUniformLocation(shader, "view"), 1, gl.GL_TRUE, view)
            gl.glUniformMatrix4fv(gl.glGetUniformLocation(shader, "projection"), 1, gl.GL_TRUE, proj)
            
            gl.glBindVertexArray(global_vao)
            gl.glDrawArrays(gl.GL_POINTS, 0, len(vertices) // 7)
            
            buf = gl.glReadPixels(0, 0, WIDTH, HEIGHT, gl.GL_RGB, gl.GL_UNSIGNED_BYTE)
            return np.flipud(np.frombuffer(buf, dtype=np.uint8).reshape(HEIGHT, WIDTH, 3))
        
        # Generates a sequence of angles distributed perfectly across NUM_FRAMES
        frames = [render_frame(angle) for angle in np.linspace(0.0, 360.0, NUM_FRAMES, endpoint=False)]
        imageio.mimsave(f'opengl_shader_{i}.gif', frames, fps=24, loop=0)
        print(f"Generated render.")

    glfw.terminate()

if __name__ == "__main__":
    run()
