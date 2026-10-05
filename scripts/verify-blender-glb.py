"""Read-only interoperability check. Run with Blender --python-exit-code 1."""
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

root = Path(sys.argv[sys.argv.index('--') + 1])
reports = []
for name in ('box', 'mirrored', 'materials', 'orthographic'):
    directory = root / name
    manifest = json.loads((directory / 'manifest.json').read_text())
    assert hashlib.sha256((directory / 'scene.glb').read_bytes()).hexdigest() == manifest['scene']['sha256']
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(directory / 'scene.glb'))
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
    cameras = [obj for obj in bpy.context.scene.objects if obj.type == 'CAMERA']
    assert len(meshes) == (2 if name == 'mirrored' else 1)
    assert len(cameras) == 1
    points = [obj.matrix_world @ vertex.co for obj in meshes for vertex in obj.data.vertices]
    low = [min(point[i] for point in points) for i in range(3)]
    high = [max(point[i] for point in points) for i in range(3)]
    for actual, expected in zip(low + high, manifest['nativeBounds']['min'] + manifest['nativeBounds']['max']):
        assert abs(actual - expected) < 1e-4, (name, low, high)
    if name == 'mirrored':
        assert meshes[0].data is meshes[1].data
        assert sum(obj.matrix_world.determinant() < 0 for obj in meshes) == 1
    for obj in meshes:
        center = sum((vertex.co for vertex in obj.data.vertices), Vector()) / len(obj.data.vertices)
        normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
        for polygon in obj.data.polygons:
            normal = normal_matrix @ polygon.normal
            outward = obj.matrix_world @ polygon.center - obj.matrix_world @ center
            assert normal.dot(outward) > 0, 'Imported box normals point outward, including mirrors'
    camera = cameras[0]
    settings = manifest['settings']
    bpy.context.scene.render.resolution_x = settings['width']
    bpy.context.scene.render.resolution_y = settings['height']
    bpy.context.scene.render.resolution_percentage = 100
    expected_camera = manifest['camera']
    assert (camera.matrix_world.translation - Vector(expected_camera['position'])).length < 1e-4
    direction = (camera.matrix_world.to_3x3() @ Vector((0, 0, -1))).normalized()
    expected_direction = (Vector(expected_camera['target']) - Vector(expected_camera['position'])).normalized()
    assert (direction - expected_direction).length < 1e-5
    frame = camera.data.view_frame(scene=bpy.context.scene)
    if expected_camera['projection'] == 'orthographic':
        assert camera.data.type == 'ORTHO'
        assert abs(max(v.y for v in frame) - min(v.y for v in frame) - 2 * expected_camera['yMag']) < 1e-4
    else:
        assert camera.data.type == 'PERSP'
        assert abs(2 * math.atan(abs(frame[0].y / frame[0].z)) - expected_camera['verticalFov']) < 1e-5
    if name == 'materials':
        assert len(bpy.data.materials) == 1
        bsdf = next(node for node in bpy.data.materials[0].node_tree.nodes if node.type == 'BSDF_PRINCIPLED')
        assert abs(bsdf.inputs['Alpha'].default_value - .35) < 1e-5
        assert abs(bsdf.inputs['Base Color'].default_value[0] - .21404114) < 1e-5
    reports.append({'fixture': name, 'bounds': [low, high], 'meshObjects': len(meshes), 'camera': camera.data.type, 'passed': True})
result = {'blender': bpy.app.version_string, 'reports': reports}
(root / 'blender-validation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
