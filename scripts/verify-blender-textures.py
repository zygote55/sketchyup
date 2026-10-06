"""Actual Cycles pixels for image orientation, independent sides and coverage."""
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import bpy
from mathutils import Vector

engine = 'BLENDER_EEVEE' if sys.argv[-1] == 'eevee' else 'CYCLES'
root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('worker', repo / 'src/integrations/blender_worker.py')
worker = importlib.util.module_from_spec(spec)
sys.dont_write_bytecode = True
spec.loader.exec_module(worker)
output = root / 'texture-renders'
output.mkdir(exist_ok=True)
reports = []


def expected(variant, side, x, y):
    if variant == 9 and .18 < x < .32 and .18 < y < .32:
        return (0, 0, 0, 0)
    if (variant == 3 and side == 1) or (variant == 4 and side == -1):
        return (.8, .2, .1, 1)
    use_b = variant in (2, 3, 8) and side == -1
    if variant in (1, 5, 8) and side == -1:
        x = 1 - x
    if variant == 7:
        x, y = (.125 + 1.5*x + .25*y) % 1, (.25 - .125*x + 2*y) % 1
    quadrant = (2 if y >= .5 else 0) + (1 if x >= .5 else 0)
    if use_b:
        colors = ((0, 1, 1, 1), (1, 0, 1, 128/255), (1, 1, 0, 0), (0, 0, 0, 1))
        rgb = colors[quadrant]
        return (*rgb[:3], rgb[3] * .5)
    colors = ((1, 0, 0, 1), (0, 1, 0, 1), (0, 0, 1, 1), (1, 1, 1, 1))
    rgb = colors[quadrant]
    alpha = (0 if quadrant == 0 else 128/255) if variant == 6 and quadrant < 2 else 1
    if variant == 10:
        return ((rgb[0]*.25, rgb[1], rgb[2]*.5, alpha) if side == 1
                else (rgb[0]*.5, rgb[1]*.25, rgb[2], alpha*.5))
    return (*rgb[:3], alpha)


for variant in range(12):
    source = root / f'texture-{variant}' / 'scene.glb'
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    worker.import_snapshot(data, output)
    scene = bpy.context.scene
    meshes = [obj for obj in scene.objects if obj.type == 'MESH']
    assert len(meshes) == (2 if variant == 5 else 1)
    assert all(len(obj.data.polygons) == (8 if variant == 9 else 2) for obj in meshes)
    if variant == 5:
        assert meshes[0].data is meshes[1].data, 'Mirrored instances still share one mesh'
    scene.render.engine = engine
    scene.cycles.samples = 32
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
    world = bpy.data.worlds.new('Texture test world')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (1, 1, 1, 1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1
    scene.world = world
    scene.camera = next(obj for obj in scene.objects if obj.type == 'CAMERA')
    scene.camera.parent = None
    scene.camera.rotation_mode = 'XYZ'
    scene.camera.data.type = 'ORTHO'
    scene.camera.data.ortho_scale = .02
    bpy.context.view_layer.update()
    points = [(.25,.25), (.75,.25), (.25,.75), (.75,.75)]
    if variant == 7:
        points += [(.4,.1), (.1,.1)]
    pixels = []
    for instance, obj in enumerate(meshes):
        # glTF importer converts local (x,y,z) to Blender (x,-z,y).
        normal = (obj.matrix_world.to_3x3().inverted().transposed() @ Vector((0,-1,0))).normalized()
        for side in (1, -1):
            for sample, (x, y) in enumerate(points):
                center = obj.matrix_world @ Vector((x,0,y))
                scene.camera.location = center + normal * side * 4
                scene.camera.rotation_euler = (center-scene.camera.location).to_track_quat('-Z','Y').to_euler()
                scene.render.filepath = str(output / f'texture-{variant}-{instance}-{side}-{sample}.png')
                bpy.ops.render.render(write_still=True)
                image = bpy.data.images.load(scene.render.filepath)
                values = image.pixels[:]
                pixel = [sum(values[4*(row*64+col)+c] for row in range(28,36) for col in range(28,36))/64
                         for c in range(4)]
                bpy.data.images.remove(image)
                oracle = expected(variant, side, x, y)
                assert abs(pixel[3]-oracle[3]) < .04, ('Coverage',variant,instance,side,sample,pixel,oracle)
                if oracle[3] > .05:
                    if max(oracle[:3]) == 0:
                        # A black dielectric still reflects the white environment specularly.
                        assert max(pixel[:3]) < .2 and max(pixel[:3])-min(pixel[:3]) < .02, ('Black texel',pixel)
                    else:
                        bright = [c for c in range(3) if oracle[c] == max(oracle[:3])]
                        dark = [c for c in range(3) if oracle[c] == 0]
                        assert min(pixel[c] for c in bright) > .1, ('Visible texel',pixel,oracle)
                        contrast = 1.3 if variant == 10 else 4
                        assert not dark or min(pixel[c] for c in bright) > contrast*max(pixel[c] for c in dark), (
                            'Independent texture quadrant', variant, instance, side, sample, pixel, oracle)
                        if variant in (3,4) and oracle[:3] == (.8,.2,.1):
                            assert pixel[0] > 2*pixel[1] and pixel[0] > 3*pixel[2], ('Plain swatch',pixel)
                if variant == 10 and oracle[:3] == (.25,1,.5):
                    assert pixel[1] > 1.6*pixel[2] and pixel[1] > 2.5*pixel[0], ('Linear swatch modulation',pixel)
                if variant == 10 and oracle[:3] == (.5,.25,1):
                    assert pixel[2] > 1.6*pixel[0] and pixel[2] > 2.5*pixel[1], ('Linear back swatch modulation',pixel)
                pixels.append({'instance':instance,'side':side,'point':[x,y],'pixel':pixel,'expected':oracle})
    assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
    assert not list(output.glob('cycles-import-*')), 'Private imports/images are cleaned up'
    reports.append({'fixture':f'texture-{variant}','passed':True,'samples':pixels})

# Malformed textures/UVs must reject before any geometry is removed or imported.
data = (root/'texture-2'/'scene.glb').read_bytes()
json_size = struct.unpack_from('<I',data,12)[0]
original = json.loads(data[20:20+json_size])
binary_chunk = data[20+json_size:]


def encode(root, binary=binary_chunk):
    encoded = json.dumps(root,separators=(',',':')).encode()
    encoded += b' '*(-len(encoded)%4)
    return struct.pack('<5I',0x46546c67,2,20+len(encoded)+len(binary),len(encoded),0x4e4f534a)+encoded+binary


def rejects(data):
    try:
        worker.prepare_sided_glb(data)
    except worker.WorkerError as error:
        assert error.code == 'invalid_snapshot'
    else:
        raise AssertionError('Malformed texture snapshot accepted')


mutations = ('uri','image_range','image_type','source','sampler','binding','uv_missing','uv_count','uv_type','uv_nan','dimension')
for mutation in mutations:
    value = copy.deepcopy(original)
    binary = binary_chunk
    if mutation == 'uri': value['images'][0]['uri'] = '/must-not-read.png'
    elif mutation == 'image_range': value['bufferViews'][value['images'][0]['bufferView']]['byteOffset'] = 2**31
    elif mutation == 'image_type': value['images'][0]['mimeType'] = 'image/svg+xml'
    elif mutation == 'source': value['textures'][0]['source'] = 9999
    elif mutation == 'sampler': value['samplers'][0]['wrapS'] = 33071
    elif mutation == 'binding': value['materials'][1]['pbrMetallicRoughness']['baseColorTexture']['texCoord'] = 1
    elif mutation == 'dimension':
        binary = bytearray(binary)
        offset = value['bufferViews'][value['images'][0]['bufferView']]['byteOffset']
        struct.pack_into('>I',binary,8+offset+16,4097)
    else:
        attrs = value['meshes'][0]['primitives'][1]['attributes']
        accessor = value['accessors'][attrs['TEXCOORD_0']]
        if mutation == 'uv_missing': del attrs['TEXCOORD_0']
        elif mutation == 'uv_count': accessor['count'] += 3
        elif mutation == 'uv_type': accessor['type'] = 'VEC3'
        else:
            binary = bytearray(binary)
            offset = value['bufferViews'][accessor['bufferView']]['byteOffset']
            struct.pack_into('<f',binary,8+offset,float('nan'))
    rejects(encode(value,binary))
report = {'blender':bpy.app.version_string,'reports':reports,'malformedTexturesRejected':len(mutations)}
(root/('texture-blender-eevee-validation.json' if engine == 'BLENDER_EEVEE' else 'texture-blender-validation.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
