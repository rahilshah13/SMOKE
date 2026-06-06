# untested
import gl_gltf
import matplotlib.pyplot as plt
import numpy as np

RESOLUTION = 720

def uv_to_index(uvs, resolution=RESOLUTION):
    u_pixels = np.clip(np.floor(uvs[:, 0] * resolution), 0, resolution - 1).astype(np.int32)
    v_pixels = np.clip(np.floor(uvs[:, 1] * resolution), 0, resolution - 1).astype(np.int32)
    flat_indices = v_pixels * resolution + u_pixels
    return flat_indices

def index_to_uv(indices, resolution=RESOLUTION):
    v_pixels = indices // resolution
    u_pixels = indices % resolution
    u = (u_pixels + 0.5) / resolution
    v = (v_pixels + 0.5) / resolution
    return np.stack([u, v], axis=-1)

mesh = gl_gltf.load("mesh.gltf")
uv_accessor = mesh.meshes[0].primitives[0].attributes.TEXCOORD_0
uvs = np.array(uv_accessor.read(), dtype=np.float32)

flat_indices = uv_to_index(uvs)

image_grid = np.zeros(RESOLUTION * RESOLUTION, dtype=bool)
image_grid[flat_indices] = True
image_grid = image_grid.reshape((RESOLUTION, RESOLUTION))

plt.imsave("uv_map_sheet.png", image_grid, cmap="gray")

reconstructed_uvs = index_to_uv(flat_indices)

print(f"Original UV shape: {uvs.shape}")
print(f"Flattened indices shape: {flat_indices.shape}")
print(f"Reconstructed UV shape: {reconstructed_uvs.shape}")
