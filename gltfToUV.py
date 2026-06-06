// untested 
import gl_gltf, matplotlib.pyplot as plt, numpy as np

mesh = gl_gltf.load("mesh.gltf")
uv_accessor = mesh.meshes[0].primitives[0].attributes.TEXCOORD_0
uvs = np.array(uv_accessor.read(), dtype=np.float32)

plt.imsave("uv_map_sheet.png", np.histogram2d(uvs[:, 1], uvs[:, 0], bins=720, range=[[0, 1], [0, 1]])[0] > 0, cmap="gray")
