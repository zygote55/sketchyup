"""Actual Cycles pixels and malformed-pair rejection for R057 sided GLB export."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Vector

root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
spec = importlib.util.spec_from_file_location('worker', Path(__file__).resolve().parents[1] / 'src/integrations/blender_worker.py')
worker = importlib.util.module_from_spec(spec)
sys.dont_write_bytecode = True
spec.loader.exec_module(worker)
output = root / 'sided-renders'
output.mkdir(exist_ok=True)
reports = []
for variant in range(6):
    source = root / f'sides-{variant}' / 'scene.glb'
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    worker.import_snapshot(data, output)
    scene = bpy.context.scene
    meshes = [obj for obj in scene.objects if obj.type == 'MESH']
    assert len(meshes) == 1
    obj = meshes[0]
    assert len(obj.data.polygons) == (8 if variant == 5 else 2), 'Exactly one physical surface after import'
    assert len(bpy.data.materials) == 1, 'Redundant material is not imported'
    material = bpy.data.materials[0]
    assert not material.use_backface_culling
    assert len([node for node in material.node_tree.nodes if node.type == 'BSDF_PRINCIPLED']) == 2
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 16
    scene.cycles.seed = 42
    scene.cycles.use_denoising = False
    scene.cycles.use_adaptive_sampling = False
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 2
    scene.render.resolution_x = scene.render.resolution_y = 64
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    world = bpy.data.worlds.new('Test world')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (1, 1, 1, 1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1
    scene.world = world
    scene.camera = next(obj for obj in scene.objects if obj.type == 'CAMERA')
    scene.camera.parent = None
    scene.camera.rotation_mode = 'XYZ'
    bpy.context.view_layer.update()
    scene.camera.data.type = 'ORTHO'
    scene.camera.data.ortho_scale = 1
    # Sample an interior point outside the hole. Independent physical-front oracle
    # uses original native +Z, converted by Blender's local Y-up -> Z-up import
    # rotation to (0,-1,0), independently of polygon or shader metadata.
    center = obj.matrix_world @ Vector((1, 0, 0))
    normal = (obj.matrix_world.to_3x3().inverted().transposed() @ Vector((0, -1, 0))).normalized()
    pixels = []
    for side in (1, -1):
        scene.camera.location = center + normal * side * 4
        scene.camera.rotation_euler = (center - scene.camera.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = str(output / f'sides-{variant}-{side}.png')
        bpy.ops.render.render(write_still=True)
        image = bpy.data.images.load(scene.render.filepath)
        index = 4 * (32 * 64 + 32)
        pixel = list(image.pixels[index:index+4])
        if variant == 4 and side == 1:
            assert pixel[3] < .01, ('Transparent front', variant, pixel)
        else:
            channel = 0 if side == 1 else 2
            assert pixel[3] > .99 and pixel[channel] > .2 and all(
                pixel[channel] > 3 * pixel[other] for other in (0, 1, 2) if other != channel
            ), ('Physical side color', variant, side, pixel)
        pixels.append(pixel)
        bpy.data.images.remove(image)
    assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
    reports.append({'fixture': f'sides-{variant}', 'frontBackPixels': pixels, 'passed': True})

# Reversed material roles remain independent and reflected component instances
# retain shared mesh storage after removing the redundant back primitives.
bpy.ops.wm.read_factory_settings(use_empty=True)
worker.import_snapshot((root / 'mixed-sides' / 'scene.glb').read_bytes(), output)
meshes = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
assert len(meshes) == 2 and meshes[0].data is meshes[1].data
assert len(meshes[0].data.polygons) == 12 and len(bpy.data.materials) == 2
for obj in meshes:
    center = sum((v.co for v in obj.data.vertices), Vector()) / len(obj.data.vertices)
    normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
    for polygon in obj.data.polygons:
        outward = obj.matrix_world @ polygon.center - obj.matrix_world @ center
        front_outside = (normal_matrix @ polygon.normal).dot(outward) > 0
        material = obj.data.materials[polygon.material_index]
        mix = next(n for n in material.node_tree.nodes if n.type == 'MIX_SHADER')
        shader = mix.inputs[1 if front_outside else 2].links[0].from_node
        assert shader.inputs['Base Color'].default_value[:3] == (1, 0, 0), 'Physical exterior stays red'
reports.append({'fixture': 'mixed-sides', 'sharedMesh': True, 'physicalExteriorRed': True, 'passed': True})

# Mutated copies must be rejected before the adapter removes any geometry.
data = (root / 'sides-0' / 'scene.glb').read_bytes()
json_size = struct.unpack_from('<I', data, 12)[0]
original = json.loads(data[20:20+json_size])
binary_chunk = data[20+json_size:]

def encode(value, binary=binary_chunk):
    encoded = json.dumps(value, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    return struct.pack('<5I', 0x46546c67, 2, 20 + len(encoded) + len(binary), len(encoded), 0x4e4f534a) + encoded + binary


def rejects(value):
    try:
        worker.prepare_sided_glb(value)
    except worker.WorkerError as error:
        assert error.code == 'invalid_snapshot'
    else:
        raise AssertionError('Malformed pair accepted')

for mutation in ('version', 'duplicate', 'absent', 'color', 'normal', 'position', 'appearance'):
    value = copy.deepcopy(original)
    binary = binary_chunk
    pairs = value['extras']['sketchyupSidedMaterials']
    if mutation == 'version':
        pairs['version'] = 2
    elif mutation == 'duplicate':
        pairs['pairs'].append(pairs['pairs'][0])
    elif mutation == 'absent':
        value['meshes'][0]['primitives'].pop()
    elif mutation == 'color':
        value['materials'][1]['pbrMetallicRoughness']['baseColorFactor'][0] = 2
    elif mutation == 'appearance':
        value['materials'][0]['extras']['sketchyupAppearance'] = 1
    else:
        attribute = 'NORMAL' if mutation == 'normal' else 'POSITION'
        index = value['meshes'][0]['primitives'][1]['attributes'][attribute]
        view = value['bufferViews'][value['accessors'][index]['bufferView']]
        binary = bytearray(binary)
        struct.pack_into('<f', binary, 8 + view['byteOffset'], .123)
    rejects(encode(value, binary))
plain = (root / 'box' / 'scene.glb').read_bytes()
assert worker.prepare_sided_glb(plain) == (plain, {})
# Historical v1 snapshots without sided metadata are still accepted unchanged.
old = copy.deepcopy(original)
del old['extras']['sketchyupSidedMaterials']
old_bytes = encode(old)
assert worker.prepare_sided_glb(old_bytes) == (old_bytes, {})
assert not list(output.glob('cycles-import-*')), 'Private import copies are cleaned up'
result = {'blender': bpy.app.version_string, 'reports': reports, 'malformedPairsRejected': 7}
(root / 'sided-blender-validation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
