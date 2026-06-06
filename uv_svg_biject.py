# untested
import cv2
import json
import numpy as np
import xml.etree.ElementTree as ET

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

def svg_to_uv_map_sheet(svg_paths_json, output_path="uv_map_sheet.png", resolution=RESOLUTION):
    svg_paths = json.loads(svg_paths_json)
    canvas = np.zeros((resolution, resolution), dtype=np.uint8)
    
    for svg_str in svg_paths:
        root = ET.fromstring(svg_str)
        path_element = root.find(".//{http://www.w3.org/2000/svg}path") or root.find(".//path")
        if path_element is None:
            continue
            
        d_attr = path_element.attrib.get("d", "")
        tokens = d_attr.replace(",", " ").split()
        
        points = []
        i = 0
        while i < len(tokens):
            if tokens[i] in ["M", "L"]:
                points.append([float(tokens[i+1]), float(tokens[i+2])])
                i += 3
            elif tokens[i] == "Z":
                i += 1
            else:
                points.append([float(tokens[i]), float(tokens[i+1])])
                i += 2
                
        if points:
            pts = np.array(points, dtype=np.int32).reshape((-1, 1, 2))
            cv2.fillPoly(canvas, [pts], 255)
            
    cv2.imwrite(output_path, canvas)

img = cv2.imread("uv_map_sheet.png", cv2.IMREAD_GRAYSCALE)
contours, _ = cv2.findContours(img, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

svg_paths = []
for c in contours:
    p = c.reshape(-1, 2)
    d = f"M {p[0][0]} {p[0][1]} " + " ".join(f"L {pt[0]} {pt[1]}" for pt in p[1:]) + " Z"
    svg_paths.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {img.shape[1]} {img.shape[0]}"><path d="{d}" fill="white" stroke="black"/></svg>')

json_data = json.dumps(svg_paths)
print(json_data)

svg_to_uv_map_sheet(json_data, "regenerated_uv_map_sheet.png")
