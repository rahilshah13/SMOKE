import sys
import subprocess
import os
import trimesh
import numpy as np

class RiggedObject:
    def __init__(self, name, mesh):
        self.name = name
        self.base_mesh = mesh
        self.transform_funcs = []

    def add_transformation(self, func):
        self.transform_funcs.append(func)

    def get_mesh_at_time(self, t):
        mesh = self.base_mesh.copy()
        current_matrix = np.eye(4)
        for func in self.transform_funcs:
            current_matrix = current_matrix @ func(t)
        mesh.apply_transform(current_matrix)
        return mesh


def generate_parametric_human(mass_kg, height_m):
    density = 1000.0
    total_volume = mass_kg / density
    scale_factor = (total_volume / (height_m * 0.1**2)) ** (1.0 / 3.0)

    # Base geometries
    chest_mesh = trimesh.creation.capsule(radius=0.15 * scale_factor, height=0.4 * height_m)
    head_mesh = trimesh.creation.icosphere(radius=0.08 * scale_factor, subdivisions=2)
    head_mesh.apply_translation([0.0, (0.4 * height_m / 2.0) + (0.08 * scale_factor), 0.0])

    arm_mesh = trimesh.creation.capsule(radius=0.04 * scale_factor, height=0.3 * height_m)
    leg_mesh = trimesh.creation.capsule(radius=0.05 * scale_factor, height=0.4 * height_m)
    joint_mesh = trimesh.creation.icosphere(radius=0.03 * scale_factor, subdivisions=1)

    parts = {
        'Hips': RiggedObject('Hips', joint_mesh.copy()),
        'Spine': RiggedObject('Spine', joint_mesh.copy()),
        'Chest': RiggedObject('Chest', chest_mesh),
        'Neck': RiggedObject('Neck', joint_mesh.copy()),
        'Head': RiggedObject('Head', head_mesh),
        'LeftShoulder': RiggedObject('LeftShoulder', joint_mesh.copy()),
        'LeftUpperArm': RiggedObject('LeftUpperArm', arm_mesh.copy()),
        'LeftLowerArm': RiggedObject('LeftLowerArm', arm_mesh.copy()),
        'LeftHand': RiggedObject('LeftHand', joint_mesh.copy()),
        'RightShoulder': RiggedObject('RightShoulder', joint_mesh.copy()),
        'RightUpperArm': RiggedObject('RightUpperArm', arm_mesh.copy()),
        'RightLowerArm': RiggedObject('RightLowerArm', arm_mesh.copy()),
        'RightHand': RiggedObject('RightHand', joint_mesh.copy()),
        'LeftUpperLeg': RiggedObject('LeftUpperLeg', leg_mesh.copy()),
        'LeftLowerLeg': RiggedObject('LeftLowerLeg', leg_mesh.copy()),
        'LeftFoot': RiggedObject('LeftFoot', joint_mesh.copy()),
        'RightUpperLeg': RiggedObject('RightUpperLeg', leg_mesh.copy()),
        'RightLowerLeg': RiggedObject('RightLowerLeg', leg_mesh.copy()),
        'RightFoot': RiggedObject('RightFoot', joint_mesh.copy())
    }

    offsets = {
        'Hips': [0.0, 0.5 * height_m, 0.0],
        'Spine': [0.0, 0.65 * height_m, 0.0],
        'Chest': [0.0, 0.8 * height_m, 0.0],
        'Neck': [0.0, 1.05 * height_m, 0.0],
        'Head': [0.0, 1.15 * height_m, 0.0],
        'LeftShoulder': [-0.2 * scale_factor, 0.95 * height_m, 0.0],
        'LeftUpperArm': [-0.25 * scale_factor, 0.8 * height_m, 0.0],
        'LeftLowerArm': [-0.25 * scale_factor, 0.5 * height_m, 0.0],
        'LeftHand': [-0.25 * scale_factor, 0.2 * height_m, 0.0],
        'RightShoulder': [0.2 * scale_factor, 0.95 * height_m, 0.0],
        'RightUpperArm': [0.25 * scale_factor, 0.8 * height_m, 0.0],
        'RightLowerArm': [0.25 * scale_factor, 0.5 * height_m, 0.0],
        'RightHand': [0.25 * scale_factor, 0.2 * height_m, 0.0],
        'LeftUpperLeg': [-0.1 * scale_factor, 0.3 * height_m, 0.0],
        'LeftLowerLeg': [-0.1 * scale_factor, 0.0 * height_m, 0.0],
        'LeftFoot': [-0.1 * scale_factor, -0.2 * height_m, 0.0],
        'RightUpperLeg': [0.1 * scale_factor, 0.3 * height_m, 0.0],
        'RightLowerLeg': [0.1 * scale_factor, 0.0 * height_m, 0.0],
        'RightFoot': [0.1 * scale_factor, -0.2 * height_m, 0.0],
    }

    for name, obj in parts.items():
        if name in offsets:
            obj.base_mesh.apply_translation(offsets[name])

    return parts


def verify_dsl_with_trealla(dsl_text):
    prolog_file = "rig_parser.pl"
    if not os.path.exists(prolog_file):
        raise FileNotFoundError(f"Could not find Prolog file: {prolog_file}")

    escaped_dsl = dsl_text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '')
    query = f'verify_rigging_program("{escaped_dsl}"), halt.'

    try:
        subprocess.run(
            ["tpl", "-l", prolog_file, "-g", query],
            capture_output=True,
            text=True,
            check=True
        )
        return True
    except subprocess.CalledProcessError as e:
        print(f"Prolog verification failed:\n{e.stderr}")
        return False


def process_rigging_scene(rigged_objects, time_steps):
    output_dir = "rigged_glbs"
    os.makedirs(output_dir, exist_ok=True)
    os.makedirs("_frames", exist_ok=True)
    frame_files = []

    print(f"Processing {len(time_steps)} frames (GLBs + Image snapshots)...")
    
    for i, t in enumerate(time_steps):
        # Build a fresh scene for each time step so transformations don't accumulate incorrectly 
        # and camera bounds / node references stay stable without flickering.
        frame_scene = trimesh.Scene()
        
        for obj in rigged_objects.values():
            mesh_at_t = obj.get_mesh_at_time(t)
            frame_scene.add_geometry(mesh_at_t, node_name=obj.name, geom_name=obj.name)

        # 1. Export GLB for this exact time frame
        glb_path = os.path.join(output_dir, f"frame_{i:04d}_t{t:.2f}.glb")
        frame_scene.export(glb_path)

        # 2. Render PNG snapshot using pyrender or offscreen renderer if available, 
        # or fall back to trimesh's software/gl pipeline.
        png_data = None
        try:
            png_data = frame_scene.save_image(resolution=[800, 600], backend='gl')
        except Exception:
            try:
                png_data = frame_scene.save_image(resolution=[800, 600], backend='pyglet')
            except Exception:
                png_data = None

        if png_data:
            frame_path = f"_frames/frame_{i:04d}.png"
            with open(frame_path, "wb") as f:
                f.write(png_data)
            frame_files.append(frame_path)

    print(f"GLB export complete. Files saved in {output_dir}/")

    # Stitch frames into GIF
    if frame_files:
        output_filename = "rigged_scene.gif"
        print(f"Stitched {len(frame_files)} frames into {output_filename} using ffmpeg...")
        
        subprocess.run([
            "ffmpeg", "-y", "-framerate", "30", "-i", "_frames/frame_%04d.png",
            "-vf", "fps=30,scale=800:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse",
            output_filename
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        for f in frame_files:
            os.remove(f)
        os.rmdir("_frames")
        print(f"Animated GIF successfully exported to {output_filename}")
    else:
        print("Warning: Headless image backend unavailable; skipped GIF creation, but GLB sequence generated successfully.")
        if os.path.exists("_frames"):
            os.rmdir("_frames")


def parse_and_apply_dsl(dsl_text, rigged_objects_map):
    lines = [line.strip() for line in dsl_text.split('\n') if line.strip()]
    for line in lines:
        if ":" not in line:
            continue
        obj_name_part, cmd_part = line.split(":", 1)
        obj_name = obj_name_part.strip()
        cmd = cmd_part.strip().rstrip(";")

        if obj_name not in rigged_objects_map:
            raise ValueError(f"Unknown object target: {obj_name}")

        obj = rigged_objects_map[obj_name]

        if "_translate(" in cmd:
            inner = cmd.split("_translate(", 1)[1].rstrip(")")
            parts = [float(p.strip()) for p in inner.split(",")]
            x, y, z = parts[0], parts[1], parts[2]
            def make_trans(tx, ty, tz):
                return lambda t: trimesh.transformations.translation_matrix([tx, ty, tz])
            obj.add_transformation(make_trans(x, y, z))

        elif "_uniform_scale(" in cmd:
            inner = cmd.split("_uniform_scale(", 1)[1].rstrip(")")
            s = float(inner.strip())
            def make_scale(factor):
                return lambda t: trimesh.transformations.scale_matrix(factor)
            obj.add_transformation(make_scale(s))

        elif "_oscillate(" in cmd:
            inner = cmd.split("_oscillate(", 1)[1].rstrip(")")
            parts = [p.strip() for p in inner.split(",")]
            axis, freq, amp = parts[0], float(parts[1]), float(parts[2])
            def make_osc(ax, f, a):
                return lambda t: trimesh.transformations.translation_matrix(
                    [a * np.sin(2 * np.pi * f * t) if ax in ('x', 'X') else 0,
                     a * np.sin(2 * np.pi * f * t) if ax in ('y', 'Y') else 0,
                     a * np.sin(2 * np.pi * f * t) if ax in ('z', 'Z') else 0]
                )
            obj.add_transformation(make_osc(axis, freq, amp))

        elif "_orbit(" in cmd:
            inner = cmd.split("_orbit(", 1)[1].rstrip(")")
            parts = [p.strip() for p in inner.split(",")]
            radius, speed, axis = float(parts[0]), float(parts[1]), parts[2]
            def make_orb(r, s, ax):
                return lambda t: trimesh.transformations.translation_matrix(
                    [r * np.cos(s * t) if ax in ('x', 'X') else 0,
                     r * np.sin(s * t) if ax in ('y', 'Y') else 0,
                     0]
                )
            obj.add_transformation(make_orb(radius, speed, axis))


if __name__ == "__main__":
    positional_args = [arg for arg in sys.argv[1:] if not arg.startswith("--")]

    if positional_args:
        script_path = positional_args[0]
        if not os.path.exists(script_path):
            print(f"Error: Rigging script file '{script_path}' not found.")
            sys.exit(1)
        with open(script_path, "r") as f:
            sample_dsl = f.read()
    else:
        sample_dsl = (
            "Chest: Chest_translate(0.0, 1.13, 0.0);\n"
            "Chest: Chest_uniform_scale(1.05);\n"
            "LeftUpperArm: LeftUpperArm_oscillate(x, 0.5, 2.0);\n"
            "Head: Head_orbit(1.2, 0.2, y);\n"
        )

    scene_map = generate_parametric_human(mass_kg=75.0, height_m=1.75)

    print("Verifying DSL syntax via rig_parser.pl...")
    if not verify_dsl_with_trealla(sample_dsl):
        print("Prolog verification failed. Aborting execution.")
        sys.exit(1)
    print("Prolog verification passed.")

    parse_and_apply_dsl(sample_dsl, scene_map)
    
    time_steps = np.linspace(0, 3.0, 90)
    process_rigging_scene(scene_map, time_steps)