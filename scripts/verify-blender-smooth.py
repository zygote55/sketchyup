"""Verify imported smooth normals and actual Cycles shading from immutable GLBs."""
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

engine = 'BLENDER_EEVEE' if sys.argv[-1] == 'eevee' else 'CYCLES'
root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
spec = importlib.util.spec_from_file_location('worker', Path(__file__).resolve().parents[1] / 'src/integrations/blender_worker.py')
worker = importlib.util.module_from_spec(spec)
sys.dont_write_bytecode = True
spec.loader.exec_module(worker)
output = root / ('renders-eevee' if engine == 'BLENDER_EEVEE' else 'renders')
output.mkdir(exist_ok=True)
reports = []
pixels = {}
for variant in range(4):
    source = root / f'smooth-{variant}' / 'scene.glb'
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    worker.import_snapshot(data, output)
    scene = bpy.context.scene
    objects = [obj for obj in scene.objects if obj.type == 'MESH']
    assert len(objects) == (2 if variant == 2 else 1)
    if variant == 2:
        assert objects[0].data is objects[1].data, 'Mirrored instances retain shared mesh'
    obj = next((o for o in objects if o.matrix_world.determinant() > 0), objects[0])
    mesh = obj.data
    assert len(mesh.polygons) == 12 and len(mesh.loops) == 36, 'One physical cube surface'
    assert mesh.has_custom_normals, 'GLB split normals are imported'
    maximum_error = 0
    for polygon in mesh.polygons:
        top = all(abs(mesh.vertices[mesh.loops[i].vertex_index].co.y + 1) < 1e-6 for i in polygon.loop_indices)
        for index in polygon.loop_indices:
            p = mesh.vertices[mesh.loops[index].vertex_index].co
            # Blender converts glTF local (x,y,z) to (x,-z,y). Native GLB positions
            # are unbaked, so the unit cube has local center (.5,-.5,.5).
            expected = p - Vector((.5, -.5, .5))
            if variant == 0:
                expected = polygon.normal.copy()
            elif variant == 3 and abs(p.y + 1) < 1e-6:
                expected = Vector((0, -1, 0)) if top else Vector((expected.x, 0, expected.z))
            error = (mesh.corner_normals[index].vector - expected.normalized()).length
            maximum_error = max(maximum_error, error)
            assert error < 5e-4, ('Analytical imported corner normal', variant, index, error)
    if variant == 2:
        reflected = next(o for o in objects if o.matrix_world.determinant() < 0)
        matrix = reflected.matrix_world.to_3x3().inverted().transposed()
        for index, loop in enumerate(mesh.loops):
            p = mesh.vertices[loop.vertex_index].co
            # Native coordinates are (local.x, local.z, -local.y). Apply the
            # independently specified inverse scale and Z rotation of the fixture.
            n = Vector(((p.x - .5) / -1.5, (p.z - .5) / .75, (-p.y - .5) / 2))
            c, s = math.cos(.41), math.sin(.41)
            expected = Vector((c*n.x - s*n.y, s*n.x + c*n.y, n.z)).normalized()
            actual = (matrix @ mesh.corner_normals[index].vector).normalized()
            assert (actual - expected).length < 5e-4, 'Physical mirrored world normal'
    scene.render.engine = engine
    scene.cycles.samples = 32
    scene.cycles.seed = 7
    scene.cycles.use_denoising = False
    scene.cycles.use_adaptive_sampling = False
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 2
    scene.render.resolution_x = scene.render.resolution_y = 96
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    world = bpy.data.worlds.new('Dark test world')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0
    scene.world = world
    light = bpy.data.lights.new('Directional oracle', 'SUN')
    light.energy = 2
    light.angle = 0
    sun = bpy.data.objects.new('Directional oracle', light)
    scene.collection.objects.link(sun)
    # Surface-to-light direction expressed in original native axes.
    direction = Vector((.3, -.5, .8)).normalized()
    sun.rotation_euler = (-direction).to_track_quat('-Z', 'Y').to_euler()
    camera = next(obj for obj in scene.objects if obj.type == 'CAMERA')
    scene.camera = camera
    camera.parent = None
    camera.rotation_mode = 'XYZ'
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 1.2
    camera.location = (.5, .5, 4)
    camera.rotation_euler = (Vector((.5, .5, 1)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(output / f'smooth-{variant}.png')
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(scene.render.filepath)
    # An off-center interior sample makes the difference in interpolated normal
    # observable under the directional light; no seam or silhouette is sampled.
    index = 4 * (38 * 96 + 64)
    pixel = list(image.pixels[index:index+4])
    assert pixel[3] > .99 and pixel[0] > .05, ('Opaque illuminated top', variant, pixel)
    pixels[variant] = pixel
    bpy.data.images.remove(image)
    assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
    reports.append({'fixture': f'smooth-{variant}', 'normalError': maximum_error, 'pixel': pixel,
                    'sharedMesh': variant == 2, 'passed': True})
assert abs(pixels[0][0] - pixels[1][0]) > .025, ('Smooth normals change actual Cycles shading', pixels)
assert abs(pixels[0][0] - pixels[3][0]) < .015, ('Explicit hard top retains flat shading', pixels)
assert abs(pixels[1][0] - pixels[2][0]) < .015, ('Adding mirrored shared instance leaves original shading intact', pixels)
assert not list(output.glob('cycles-import-*')), 'Private worker import copies are removed'
result = {'blender': bpy.app.version_string, 'engine': engine, 'reports': reports}
(root / ('smooth-blender-eevee-validation.json' if engine == 'BLENDER_EEVEE' else 'smooth-blender-validation.json')).write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
