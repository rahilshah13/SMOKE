// untested 
import json
import base64
import numpy as np

rgb = np.fromfile("render-C/frames.rgb", dtype=np.uint8).reshape(-1, 3)
mask = ~((rgb[:, 0] >= 8) & (rgb[:, 0] <= 26) & (rgb[:, 0] == rgb[:, 1]) & (rgb[:, 1] == rgb[:, 2]))

y, x = np.divmod(np.where(mask)[0], 720)
num_v = len(x)

vbo = np.column_stack((x / 360.0 - 1.0, 1.0 - y / 360.0, np.zeros(num_v), x / 720.0, y / 720.0)).astype(np.float32)
indices = np.arange(num_v, dtype=np.uint32)

vbo_bytes = vbo.tobytes()
idx_bytes = indices.tobytes()
b64_str = base64.b64encode(vbo_bytes + idx_bytes).decode("utf-8")

gltf = {
    "asset": {"version": "2.0"},
    "scenes": [{"nodes": [0]}],
    "nodes": [{"mesh": 0}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "indices": 2, "mode": 0}]}],
    "accessors": [
        {"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": num_v, "type": "VEC3", "max": [1.0, 1.0, 0.0], "min": [-1.0, -1.0, 0.0]},
        {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": num_v, "type": "VEC2", "max": [1.0, 1.0], "min": [0.0, 0.0]},
        {"bufferView": 1, "byteOffset": 0, "componentType": 5125, "count": num_v, "type": "SCALAR", "max": [max(0, num_v - 1)], "min": [0]}
    ],
    "bufferViews": [
        {"buffer": 0, "byteOffset": 0, "byteLength": len(vbo_bytes), "byteStride": 20, "target": 34962},
        {"buffer": 0, "byteOffset": len(vbo_bytes), "byteLength": len(idx_bytes), "target": 34963}
    ],
    "buffers": [{"byteLength": len(vbo_bytes) + len(idx_bytes), "uri": f"data:application/octet-stream;base64,{b64_str}"}]
}

with open("mesh.gltf", "w") as f:
    json.dump(gltf, f)

with open("shader.frag", "w") as f:
    f.write('''#version 330 core
uniform sampler2D tex;
in vec2 v_uv;
out vec4 f_color;
void main() {
    f_color = texture(tex, v_uv);
}''')
