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
    
    chest_radius = 0.15 * scale_factor
    chest_height = 0.4 * height_m
    chest_mesh = trimesh.creation.capsule(radius=chest_radius, height=chest_height)
    
    head_radius = 0.08 * scale_factor
    head_mesh = trimesh.creation.icosphere(radius=head_radius, subdivisions=2)
    head_mesh.apply_translation([0.0, (chest_height / 2.0) + head_radius, 0.0])
    
    arm_radius = 0.04 * scale_factor
    arm_length = 0.3 * height_m
    arm_mesh = trimesh.creation.capsule(radius=arm_radius, height=arm_length)
    arm_mesh.apply_translation([-(chest_radius + arm_radius), 0.0, 0.0])

    parts = {
        "Chest": RiggedObject("Chest", chest_mesh),
        "Head": RiggedObject("Head", head_mesh),
        "LeftUpperArm": RiggedObject("LeftUpperArm", arm_mesh)
    }
    return parts


def verify_dsl_with_trealla(dsl_text):
    prolog_file = "verify_rig.pl"
    if not os.path.exists(prolog_file):
        raise FileNotFoundError(f"Could not find Prolog file: {prolog_file}")

    escaped_dsl = dsl_text.replace('"', '\\"')
    query = f"verify_rigging_program(\"{escaped_dsl}\"), halt."

    try:
        subprocess.run(
            ["tpl", "-f", prolog_file, "-g", query],
            capture_output=True,
            text=True,
            check=True
        )
        return True
    except subprocess.CalledProcessError as e:
        print(f"Prolog verification failed:\n{e.stderr}")
        return False


def validate_rigging_scene(rigged_objects, time_steps):
    collision_manager = trimesh.collision.CollisionManager()
    
    for obj in rigged_objects.values():
        collision_manager.add_object(obj.name, obj.base_mesh)

    for t in time_steps:
        transforms = {}
        for obj in rigged_objects.values():
            current_matrix = np.eye(4)
            for func in obj.transform_funcs:
                current_matrix = current_matrix @ func(t)
            transforms[obj.name] = current_matrix

        collision_manager.set_transform_batch(transforms)
        
        if collision_manager.in_collision():
            return False, t

    return True, None


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
    if len(sys.argv) > 1:
        script_path = sys.argv[1]
        if not os.path.exists(script_path):
            print(f"Error: Rigging script file '{script_path}' not found.")
            sys.exit(1)
        with open(script_path, "r") as f:
            sample_dsl = f.read()
    else:
        sample_dsl = """
            Chest: Chest_translate(0.0, 1.13, 0.0)
            Chest: Chest_uniform_scale(1.05)
            LeftUpperArm: LeftUpperArm_oscillate(x, 0.5, 2.0)
            Head: Head_orbit(1.2, 0.2, y)
        """

    scene_map = generate_parametric_human(mass_kg=75.0, height_m=1.75)

    print("Verifying DSL syntax via verify_rig.pl...")
    if not verify_dsl_with_trealla(sample_dsl):
        print("Prolog verification failed. Aborting execution.")
        sys.exit(1)
    print("Prolog verification passed.")

    parse_and_apply_dsl(sample_dsl, scene_map)
    
    time_steps = np.linspace(0, 10, 100)
    is_valid, collision_time = validate_rigging_scene(scene_map, time_steps)
    
    if is_valid:
        print("Rigging validation passed: No collisions detected across time steps.")
    else:
        print(f"Rigging validation failed: Collision detected at time t={collision_time}")
