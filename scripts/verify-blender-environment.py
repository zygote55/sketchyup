"""Verify packaged RGBE illumination, strength and world rotation in actual Cycles."""
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
root.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('worker', Path(__file__).resolve().parents[1] / 'src/integrations/blender_worker.py')
worker = importlib.util.module_from_spec(spec)
sys.dont_write_bytecode = True
spec.loader.exec_module(worker)
width, height = 64, 32
pixels = bytes(channel for y in range(height) for x in range(width)
               for channel in ((128, 8, 8, 129) if x < width // 2 else (8, 8, 128, 129)))
data = f'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n'.encode() + pixels
reports = []
for rotation, strength in ((0, 1), (180, 1), (0, .25), (0, 0)):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    (root / 'environment.hdr').write_bytes(data)
    metadata = {'file': 'environment.hdr', 'mediaType': 'image/vnd.radiance', 'projection': 'equirectangular',
                'colorSpace': 'Linear Rec.709', 'width': width, 'height': height, 'bytes': len(data),
                'sha256': hashlib.sha256(data).hexdigest(), 'strength': strength, 'rotationDegrees': rotation}
    manifest = {'environment': metadata}
    before = copy.deepcopy(manifest)
    lighting = worker.configure_lighting(manifest, root)
    assert lighting == {'mode': 'hdri-v1', 'worldStrength': strength, 'environment': metadata}
    assert manifest == before
    (root / 'environment.hdr').unlink()  # The packed Blender image must remain sufficient.
    scene = bpy.context.scene
    assert not [obj for obj in scene.objects if obj.type == 'LIGHT'], 'HDR replaces studio lights'
    bpy.ops.mesh.primitive_plane_add(size=2, rotation=(-math.pi / 2, 0, 0))
    material = bpy.data.materials.new('Diffuse environment oracle')
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    diffuse, surface = nodes.new('ShaderNodeBsdfDiffuse'), nodes.new('ShaderNodeOutputMaterial')
    diffuse.inputs['Color'].default_value = (.8, .8, .8, 1)
    material.node_tree.links.new(diffuse.outputs[0], surface.inputs['Surface'])
    bpy.context.object.data.materials.append(material)
    camera = bpy.data.objects.new('Camera', bpy.data.cameras.new('Camera'))
    scene.collection.objects.link(camera)
    camera.location = (0, 4, 0)
    camera.rotation_euler = Vector((0, -1, 0)).to_track_quat('-Z', 'Y').to_euler()
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 3
    scene.camera = camera
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 128
    scene.cycles.seed = 7
    scene.cycles.use_denoising = False
    scene.cycles.use_adaptive_sampling = False
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 2
    scene.render.resolution_x = scene.render.resolution_y = 64
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.film_transparent = True
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.render.filepath = str(root / f'environment-{rotation}-{strength}.png')
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(scene.render.filepath)
    colors = []
    for y in range(28, 36):
        for x in range(28, 36):
            index = 4 * (y * 64 + x)
            colors.append(list(image.pixels[index:index + 4]))
    mean = [sum(p[channel] for p in colors) / len(colors) for channel in range(4)]
    assert mean[3] > .99
    bpy.data.images.remove(image)
    reports.append({'rotationDegrees': rotation, 'strength': strength, 'mean': mean, 'originalRemovedBeforeRender': True})
a, b, dim, dark = (row['mean'] for row in reports)
assert max(a[0], a[2]) > min(a[0], a[2]) * 2, reports
assert max(b[0], b[2]) > min(b[0], b[2]) * 2, reports
assert (a[0] - a[2]) * (b[0] - b[2]) < 0, '180 degree rotation swaps dominant illumination'
assert sum(dim[:3]) < sum(a[:3]) * .7 and sum(dim[:3]) > 0, reports
assert max(dark[:3]) < .01, reports
(root / 'environment.hdr').write_bytes(data)
for key, value in [('sha256', '0' * 64), ('width', 8192), ('strength', float('nan')), ('rotationDegrees', 361), ('file', '../environment.hdr')]:
    bad = copy.deepcopy(metadata)
    bad[key] = value
    try:
        worker.configure_lighting({'environment': bad}, root)
        raise AssertionError('Invalid environment accepted')
    except worker.WorkerError as error:
        assert error.code == 'invalid_snapshot'
result = {'blender': bpy.app.version_string, 'reports': reports, 'invalidSnapshotsRejected': 5}
(root / 'environment-blender-validation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
