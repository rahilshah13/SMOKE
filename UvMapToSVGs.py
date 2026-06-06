// untested
import cv2, json, numpy as np

img = cv2.imread("uv_map_sheet.png", cv2.IMREAD_GRAYSCALE)
contours, _ = cv2.findContours(img, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

svg_paths = []
for c in contours:
    p = c.reshape(-1, 2)
    d = f"M {p[0][0]} {p[0][1]} " + " ".join(f"L {pt[0]} {pt[1]}" for pt in p[1:]) + " Z"
    svg_paths.append(f'<svg viewBox="0 0 {img.shape[1]} {img.shape[0]}"><path d="{d}" fill="white" stroke="black"/></svg>')

print(json.dumps(svg_paths))
