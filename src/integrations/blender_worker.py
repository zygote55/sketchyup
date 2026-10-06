"""SketchyUp's fixed Blender adapter. No model-provided code is evaluated."""
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import struct
import tempfile

import bpy
from mathutils import Vector

ADAPTER = 'sketchyup-blender-v1'
BACKENDS = {'CPU', 'CUDA', 'OPTIX', 'HIP', 'ONEAPI', 'METAL'}


class WorkerError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def load_json(path, limit):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > limit:
        raise WorkerError('invalid_snapshot', 'Expected a bounded regular JSON file')
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise WorkerError('invalid_snapshot', 'JSON input exceeds its bound')
    return json.loads(data), data


def sha(data):
    return hashlib.sha256(data).hexdigest()


def progress(phase):
    print('SKETCHYUP_PROGRESS ' + json.dumps({'phase': phase}), flush=True)


def publish(directory, result):
    encoded = json.dumps(result, allow_nan=False).encode('utf-8')
    if len(encoded) > 64 * 1024:
        raise RuntimeError('Worker result exceeds 64 KiB')
    temporary = directory / 'result.json.tmp'
    temporary.write_bytes(encoded)
    os.replace(temporary, directory / 'result.json')


def capabilities(backend):
    preferences = bpy.context.preferences.addons['cycles'].preferences
    compiled = ['CPU'] + [item[0] for item in preferences.get_device_types(bpy.context) if item[0] != 'NONE']
    if backend == 'CPU':
        devices = [{'id': 'CPU', 'name': 'CPU', 'backend': 'CPU'}]
    elif backend not in compiled:
        devices = []
    else:
        # Enumerate only the explicitly requested backend. Probing every driver
        # can crash Blender on unsupported systems, even when CPU was requested.
        devices = [{'id': device.id, 'name': device.name, 'backend': device.type}
                   for device in preferences.get_devices_for_type(backend) if device.type == backend]
    return {'backends': compiled, 'devices': devices, 'backendAvailable': bool(devices)}


def configure_device(backend, device_id):
    if backend == 'CPU':
        bpy.context.scene.cycles.device = 'CPU'
        return {'backend': 'CPU', 'id': 'CPU', 'name': 'CPU'}
    info = capabilities(backend)
    chosen = next((device for device in info['devices'] if device['id'] == device_id), None)
    if chosen is None:
        raise WorkerError('device_unavailable', 'The explicitly selected GPU device is unavailable')
    preferences = bpy.context.preferences.addons['cycles'].preferences
    preferences.compute_device_type = backend
    for device in preferences.devices:
        device.use = device.type == backend and device.id == device_id
    bpy.context.scene.cycles.device = 'GPU'
    return chosen


def prepare_sided_glb(data):
    """Validate our paired triangles and remove only their redundant back primitives.

    The published GLB stays standard glTF. Cycles uses one surface with a
    Backfacing shader, avoiding ambiguous coincident ray intersections.
    """
    def require(condition, message):
        if not condition:
            raise WorkerError('invalid_snapshot', message)

    require(28 <= len(data) <= 256 * 1024 * 1024, 'Invalid GLB length')
    magic, version, length, json_size, json_type = struct.unpack_from('<5I', data)
    require((magic, version, length, json_type) == (0x46546c67, 2, len(data), 0x4e4f534a)
            and json_size <= 16 * 1024 * 1024 and json_size % 4 == 0
            and json_size + 28 <= len(data), 'Invalid GLB header')
    root = json.loads(data[20:20 + json_size])
    binary_size, binary_type = struct.unpack_from('<2I', data, 20 + json_size)
    require(binary_type == 0x004e4942 and binary_size % 4 == 0
            and json_size + 28 + binary_size == len(data), 'Invalid GLB binary chunk')
    sides = root.get('extras', {}).get('sketchyupSidedMaterials')
    if sides is None:
        return data, {}  # Original v1 snapshots remain readable.
    require(type(sides) is dict and sides.get('version') == 1
            and type(sides.get('pairs')) is list and len(sides['pairs']) <= 2048,
            'Unsupported sided material metadata')
    materials = root.get('materials', [])
    require(type(materials) is list and len(materials) <= 4096, 'Too many materials')
    back_to_front, colors, used = {}, {}, set()
    for pair in sides['pairs']:
        require(type(pair) is dict and set(pair) == {'front', 'back'}, 'Invalid material pair')
        front, back = pair['front'], pair['back']
        for index in (front, back):
            require(type(index) is int and 0 <= index < len(materials) and index not in used,
                    'Invalid or reused sided material index')
            used.add(index)
            material = materials[index]
            pbr = material.get('pbrMetallicRoughness', {})
            rgba = pbr.get('baseColorFactor')
            require(material.get('doubleSided') is False
                    and material.get('extras', {}).get('sketchyupAppearance') == index
                    and set(pbr) == {'baseColorFactor', 'metallicFactor', 'roughnessFactor'}
                    and pbr['metallicFactor'] == 0 and pbr['roughnessFactor'] == .8
                    and not material.get('extensions')
                    and type(rgba) is list and len(rgba) == 4
                    and all(type(c) in (int, float) and math.isfinite(c) and 0 <= c <= 1 for c in rgba)
                    and material.get('alphaMode') == ('BLEND' if rgba[3] < 1 else 'OPAQUE'),
                    'Unsupported sided material appearance')
        back_to_front[back] = front
        colors[front] = materials[back]['pbrMetallicRoughness']['baseColorFactor']
    if not colors:
        return data, {}
    binary = memoryview(data)[json_size + 28:]
    accessors, views = root['accessors'], root['bufferViews']

    def attribute(index):
        require(type(index) is int and 0 <= index < len(accessors), 'Invalid paired accessor')
        accessor = accessors[index]
        count, view_index = accessor.get('count'), accessor.get('bufferView')
        require(type(count) is int and 0 < count <= 3000000 and count % 3 == 0
                and accessor.get('componentType') == 5126 and accessor.get('type') == 'VEC3'
                and not accessor.get('sparse') and not accessor.get('normalized')
                and accessor.get('byteOffset', 0) == 0
                and type(view_index) is int and 0 <= view_index < len(views),
                'Unsupported paired accessor')
        view = views[view_index]
        offset = view.get('byteOffset', 0)
        require(type(offset) is int and offset >= 0 and offset % 4 == 0
                and view.get('buffer') == 0 and view.get('byteLength') == count * 12
                and view.get('byteStride', 12) == 12 and offset + count * 12 <= len(binary),
                'Invalid paired buffer range')
        return binary[offset:offset + count * 12]

    def attributes(primitive):
        require(primitive.get('mode', 4) == 4 and 'indices' not in primitive
                and not primitive.get('targets') and not primitive.get('extensions')
                and set(primitive.get('attributes', {})) == {'POSITION', 'NORMAL'},
                'Unsupported paired primitive')
        positions = attribute(primitive['attributes']['POSITION'])
        normals = attribute(primitive['attributes']['NORMAL'])
        require(len(positions) == len(normals), 'Paired normal count mismatch')
        return positions, normals

    front_to_back = {front: back for back, front in back_to_front.items()}
    count, seen = 0, set()
    for mesh in root['meshes']:
        groups = {}
        for primitive in mesh['primitives']:
            index = primitive.get('material')
            if index in used:
                require(index not in groups, 'Duplicate paired primitive')
                groups[index] = primitive
        present = {back_to_front.get(index, index) for index in groups}
        for front in present:
            back = front_to_back[front]
            require(front in groups and back in groups, 'Missing paired primitive')
            seen.add(front)
            fp, fn = attributes(groups[front])
            bp, bn = attributes(groups[back])
            require(len(fp) == len(bp), 'Paired triangle count mismatch')
            count += len(fp) // 18  # Two sides, 36 bytes per triangle.
            require(count <= 1000000, 'Too many paired triangles')
            # Compare actual geometry before removing anything; equal counts alone
            # must never authorize dropping unrelated surfaces.
            for start in range(0, len(fp), 36):
                for a, b in ((0, 0), (12, 24), (24, 12)):
                    require(fp[start+a:start+a+12] == bp[start+b:start+b+12],
                            'Paired positions do not have opposite winding')
                    n = struct.unpack_from('<3f', fn, start+a)
                    reverse = struct.unpack_from('<3f', bn, start+b)
                    require(all(math.isfinite(x) and x == -y for x, y in zip(n, reverse)),
                            'Paired normals are not opposite')
        mesh['primitives'] = [p for p in mesh['primitives'] if p.get('material') not in back_to_front]
        require(bool(mesh['primitives']), 'Sided conversion removed a complete mesh')
    require(seen == set(colors), 'Unused sided material pair')
    encoded = json.dumps(root, separators=(',', ':'), allow_nan=False).encode('utf-8')
    encoded += b' ' * (-len(encoded) % 4)
    require(len(encoded) <= 16 * 1024 * 1024, 'Derived GLB JSON exceeds its bound')
    converted = (struct.pack('<5I', magic, version, len(encoded) + binary_size + 28,
                             len(encoded), json_type) + encoded
                 + struct.pack('<2I', binary_size, binary_type) + binary.tobytes())
    require(len(converted) <= 256 * 1024 * 1024, 'Derived GLB exceeds its bound')
    return converted, colors


def import_snapshot(scene_bytes, directory):
    converted, back_colors = prepare_sided_glb(scene_bytes)
    # Only a private derived copy is imported; original bytes and their hash stay intact.
    with tempfile.TemporaryDirectory(prefix='cycles-import-', dir=directory) as temporary:
        path = Path(temporary) / 'scene.glb'
        path.write_bytes(converted)
        bpy.ops.import_scene.gltf(filepath=str(path))
    found = set()
    for material in bpy.data.materials:
        index = material.get('sketchyupAppearance')
        if index not in back_colors:
            continue
        found.add(index)
        nodes, links = material.node_tree.nodes, material.node_tree.links
        front = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
        output = next(n for n in nodes if n.type == 'OUTPUT_MATERIAL')
        back = nodes.new('ShaderNodeBsdfPrincipled')
        back.label = 'Native back appearance'
        back.inputs['Base Color'].default_value = back_colors[index]
        back.inputs['Alpha'].default_value = back_colors[index][3]
        back.inputs['Metallic'].default_value = 0
        back.inputs['Roughness'].default_value = .8
        geometry = nodes.new('ShaderNodeNewGeometry')
        mix = nodes.new('ShaderNodeMixShader')
        links.new(geometry.outputs['Backfacing'], mix.inputs[0])
        links.new(front.outputs['BSDF'], mix.inputs[1])
        links.new(back.outputs['BSDF'], mix.inputs[2])
        links.new(mix.outputs[0], output.inputs['Surface'])
        material.use_backface_culling = False
    if found != set(back_colors):
        raise WorkerError('invalid_snapshot', 'Importer did not preserve sided appearance identities')


def render(request, directory):
    source = Path(request['sourceDirectory'])
    manifest, manifest_bytes = load_json(source / 'manifest.json', 16 * 1024 * 1024)
    if sha(manifest_bytes) != request['manifestSha256']:
        raise WorkerError('invalid_snapshot', 'Snapshot manifest changed')
    scene_path = source / 'scene.glb'
    if scene_path.is_symlink() or not scene_path.is_file() or scene_path.stat().st_size > 256 * 1024 * 1024:
        raise WorkerError('invalid_snapshot', 'Expected a bounded regular GLB file')
    scene_bytes = scene_path.read_bytes()
    if sha(scene_bytes) != manifest['scene']['sha256'] or len(scene_bytes) != manifest['scene']['bytes']:
        raise WorkerError('invalid_snapshot', 'Snapshot GLB changed')
    settings = manifest['settings']
    if (manifest['apiVersion'] != 1 or manifest['adapter'] != 'sketchyup-glb-v1' or
            manifest['scene']['file'] != 'scene.glb' or
            any(type(settings[key]) is not int for key in ('width', 'height', 'samples', 'seed')) or
            not 64 <= settings['width'] <= 4096 or not 64 <= settings['height'] <= 4096 or
            not 1 <= settings['samples'] <= 1024 or not 0 <= settings['seed'] <= 1000000):
        raise WorkerError('invalid_snapshot', 'Unsupported snapshot settings')
    progress('loading')
    bpy.ops.wm.read_factory_settings(use_empty=True)
    import_snapshot(scene_bytes, directory)
    del scene_bytes
    scene = bpy.context.scene
    cameras = [obj for obj in scene.objects if obj.type == 'CAMERA']
    if len(cameras) != 1:
        raise WorkerError('invalid_snapshot', 'Snapshot requires exactly one render camera')
    scene.camera = cameras[0]
    scene.render.engine = 'CYCLES'
    actual_device = configure_device(request['backend'], request['deviceId'])
    scene.cycles.samples = settings['samples']
    scene.cycles.seed = settings['seed']
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    scene.cycles.max_bounces = 8
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 4
    scene.render.resolution_x = settings['width']
    scene.render.resolution_y = settings['height']
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '8'
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    world = bpy.data.worlds.new('SketchyUp studio world')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (1, 1, 1, 1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = .25
    scene.world = world
    bounds = manifest['nativeBounds']
    low, high = Vector(bounds['min']), Vector(bounds['max'])
    center = (low + high) * .5
    radius = max(.01, (high - low).length * .5)
    for name, direction, power in [('Key', (1, -1, 2), 80), ('Fill', (-1, -.2, 1), 25)]:
        light = bpy.data.lights.new('SketchyUp ' + name, 'AREA')
        light.energy = power * radius * radius
        light.shape = 'DISK'
        light.size = radius * 1.5
        obj = bpy.data.objects.new(light.name, light)
        scene.collection.objects.link(obj)
        obj.location = center + Vector(direction) * radius * 2
        obj.rotation_euler = (center - obj.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(directory / 'image.png')
    progress('rendering')
    bpy.ops.render.render(write_still=True)
    progress('verifying-output')
    image = directory / 'image.png'
    if image.is_symlink() or not image.is_file() or not 0 < image.stat().st_size <= 64 * 1024 * 1024:
        raise WorkerError('render_error', 'Renderer did not produce a bounded PNG')
    image_bytes = image.read_bytes()
    return {'status': 'succeeded', 'documentId': manifest['documentId'], 'revision': manifest['revision'],
            'manifestSha256': request['manifestSha256'], 'sceneSha256': manifest['scene']['sha256'],
            'settings': settings, 'device': actual_device, 'losses': manifest['losses'],
            'preset': {'name': 'studio-v1', 'engine': 'CYCLES', 'threads': 4, 'adaptiveSampling': False,
                       'denoising': False, 'maxBounces': 8, 'viewTransform': 'Standard', 'look': 'None',
                       'exposure': 0, 'gamma': 1, 'worldStrength': .25},
            'image': {'file': 'image.png', 'width': settings['width'], 'height': settings['height'],
                      'bytes': len(image_bytes), 'sha256': sha(image_bytes)}}


def main():
    request_path = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
    directory = request_path.parent
    base = {'apiVersion': 1, 'adapter': ADAPTER, 'blenderVersion': list(bpy.app.version),
            'blenderVersionString': bpy.app.version_string,
            'blenderBuildHash': bpy.app.build_hash.decode('ascii', errors='replace')}
    try:
        request, _ = load_json(request_path, 64 * 1024)
        if (request.get('apiVersion') != 1 or request.get('operation') not in ('probe', 'render') or
                request.get('backend') not in BACKENDS or not isinstance(request.get('deviceId'), str) or
                len(request['deviceId']) > 512):
            raise WorkerError('invalid_request', 'Unsupported worker request')
        if bpy.app.version[:2] != (5, 2):
            raise WorkerError('unsupported_version', 'This adapter supports Blender 5.2 LTS')
        if not bpy.app.build_options.cycles or 'cycles' not in bpy.context.preferences.addons:
            raise WorkerError('cycles_unavailable', 'This Blender build does not include Cycles')
        if request['operation'] == 'probe':
            result = {'status': 'available', **capabilities(request['backend'])}
        else:
            result = render(request, directory)
        publish(directory, {**base, **result})
        progress('completed')
    except Exception as error:
        publish(directory, {**base, 'status': 'failed', 'code': getattr(error, 'code', 'render_error'),
                            'message': str(error)[:4096]})
        raise


if __name__ == '__main__':
    main()
