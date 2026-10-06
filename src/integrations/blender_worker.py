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
BACKENDS = {'CPU', 'CUDA', 'OPTIX', 'HIP', 'ONEAPI', 'METAL', 'OPENGL'}


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


def raster_device():
    import gpu
    gpu.init()
    identity = {key: getattr(gpu.platform, key + '_get')() for key in
                ('backend_type', 'device_type', 'renderer', 'vendor', 'version')}
    if identity['backend_type'] != 'OPENGL':
        raise WorkerError('device_unavailable', 'Eevee preview requires the probed OpenGL renderer')
    digest = sha(json.dumps(identity, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode())
    return {'backend': 'OPENGL', 'id': 'opengl:' + digest,
            'name': identity['renderer'], 'graphics': identity}


def capabilities(backend):
    if backend == 'OPENGL':
        device = raster_device()
        return {'backends': ['OPENGL'], 'devices': [device], 'backendAvailable': True,
                'engines': ['eevee'], 'deviceSelection': 'active-context-fingerprint'}
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
    return {'backends': compiled, 'devices': devices, 'backendAvailable': bool(devices), 'engines': ['cycles'],
            'deviceSelection': 'explicit-cycles-device'}


def configure_device(backend, device_id):
    if backend == 'OPENGL':
        device = raster_device()
        if device['id'] != device_id:
            raise WorkerError('device_unavailable', 'OpenGL renderer changed since the explicit device selection')
        return device
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
    require(type(sides) is dict and type(sides.get('version')) is int and sides['version'] in (1, 2)
            and type(sides.get('pairs')) is list and len(sides['pairs']) <= 2048,
            'Unsupported sided material metadata')
    materials = root.get('materials', [])
    require(type(materials) is list and len(materials) <= 4096, 'Too many materials')
    binary = memoryview(data)[json_size + 28:]
    accessors, views = root['accessors'], root['bufferViews']
    extra = bytearray()
    images = {}
    image_pixels = 0
    if sides['version'] == 2:
        records = root.get('images', [])
        require(type(records) is list and len(records) <= 1024, 'Too many texture images')
        for index, record in enumerate(records):
            require(type(record) is dict and record.get('mimeType') == 'image/png'
                    and 'uri' not in record and not record.get('extensions'),
                    'Texture image must be an embedded normalized PNG')
            view_index = record.get('bufferView')
            require(type(view_index) is int and 0 <= view_index < len(views),
                    'Invalid texture image view')
            view = views[view_index]
            offset, size = view.get('byteOffset', 0), view.get('byteLength')
            require(type(offset) is int and offset >= 0 and offset % 4 == 0
                    and type(size) is int and size >= 33 and offset + size <= len(binary)
                    and view.get('buffer') == 0 and 'byteStride' not in view,
                    'Invalid texture image range')
            pixels = binary[offset:offset + size]
            require(pixels[:8] == b'\x89PNG\r\n\x1a\n'
                    and pixels[8:16] == b'\x00\x00\x00\x0dIHDR', 'Invalid texture PNG header')
            width, height, depth = struct.unpack_from('>IIB', pixels, 16)
            require(0 < width <= 4096 and 0 < height <= 4096 and depth == 8,
                    'Texture PNG exceeds supported dimensions or depth')
            image_pixels += width * height * 4
            require(image_pixels <= 256 * 1024 * 1024, 'Decoded texture images exceed 256 MiB')
            images[index] = pixels
        samplers = root.get('samplers', [])
        require(type(samplers) is list and len(samplers) <= 1024, 'Invalid texture sampler table')
        require(type(root.get('textures')) is list and len(root['textures']) <= 1024,
                'Invalid texture table')
        for texture in root['textures']:
            require(type(texture) is dict and set(texture) == {'source', 'sampler'}
                    and type(texture['source']) is int and texture['source'] in images
                    and type(texture['sampler']) is int
                    and 0 <= texture['sampler'] < len(samplers),
                    'Unsupported texture binding')
            require(samplers[texture['sampler']] == {
                'magFilter': 9729, 'minFilter': 9729, 'wrapS': 10497, 'wrapT': 10497},
                'Unsupported texture sampling')

    def texture_source(pbr):
        if 'baseColorTexture' not in pbr:
            return None
        binding = pbr['baseColorTexture']
        require(sides['version'] == 2 and type(binding) is dict
                and set(binding) == {'index', 'texCoord'} and binding['texCoord'] == 0
                and type(binding['index']) is int
                and 0 <= binding['index'] < len(root['textures']),
                'Unsupported base color texture')
        return root['textures'][binding['index']]['source']

    back_to_front, colors, used, sources = {}, {}, set(), {}
    for pair in sides['pairs']:
        require(type(pair) is dict and set(pair) == {'front', 'back'}, 'Invalid material pair')
        front, back = pair['front'], pair['back']
        for index in (front, back):
            require(type(index) is int and 0 <= index < len(materials) and index not in used,
                    'Invalid or reused sided material index')
            used.add(index)
            material = materials[index]
            require(type(material) is dict, 'Invalid sided material')
            pbr = material.get('pbrMetallicRoughness', {})
            require(type(pbr) is dict, 'Invalid sided PBR appearance')
            rgba = pbr.get('baseColorFactor')
            require(material.get('doubleSided') is False
                    and material.get('extras', {}).get('sketchyupAppearance') == index
                    and set(pbr) in ({'baseColorFactor', 'metallicFactor', 'roughnessFactor'},
                                     {'baseColorFactor', 'metallicFactor', 'roughnessFactor', 'baseColorTexture'})
                    and pbr['metallicFactor'] == 0 and pbr['roughnessFactor'] == .8
                    and not material.get('extensions')
                    and type(rgba) is list and len(rgba) == 4
                    and all(type(c) in (int, float) and math.isfinite(c) and 0 <= c <= 1 for c in rgba)
                    and material.get('alphaMode') in ({'BLEND'} if rgba[3] < 1 else
                        {'OPAQUE', 'BLEND'} if 'baseColorTexture' in pbr else {'OPAQUE'}),
                    'Unsupported sided material appearance')
            sources[index] = texture_source(pbr)
        back_to_front[back] = front
        colors[front] = {'rgba': materials[back]['pbrMetallicRoughness']['baseColorFactor']}
        if sources[back] is not None:
            colors[front].update({'image': sources[back], 'png': images[sources[back]]})
    if not colors:
        return data, {}

    def attribute(index, dimensions=3):
        require(type(index) is int and 0 <= index < len(accessors), 'Invalid paired accessor')
        accessor = accessors[index]
        count, view_index = accessor.get('count'), accessor.get('bufferView')
        require(type(count) is int and 0 < count <= 3000000 and count % 3 == 0
                and accessor.get('componentType') == 5126
                and accessor.get('type') == ('VEC2' if dimensions == 2 else 'VEC3')
                and not accessor.get('sparse') and not accessor.get('normalized')
                and accessor.get('byteOffset', 0) == 0
                and type(view_index) is int and 0 <= view_index < len(views),
                'Unsupported paired accessor')
        view = views[view_index]
        offset = view.get('byteOffset', 0)
        require(type(offset) is int and offset >= 0 and offset % 4 == 0
                and view.get('buffer') == 0 and view.get('byteLength') == count * dimensions * 4
                and view.get('byteStride', dimensions * 4) == dimensions * 4
                and offset + count * dimensions * 4 <= len(binary),
                'Invalid paired buffer range')
        value = binary[offset:offset + count * dimensions * 4]
        require(all(math.isfinite(x[0]) for x in struct.iter_unpack('<f', value)),
                'Paired attributes must be finite')
        return value

    def attributes(primitive):
        require(primitive.get('mode', 4) == 4 and 'indices' not in primitive
                and not primitive.get('targets') and not primitive.get('extensions')
                and set(primitive.get('attributes', {})) ==
                    ({'POSITION', 'NORMAL', 'TEXCOORD_0'} if sources[primitive['material']] is not None
                     else {'POSITION', 'NORMAL'}),
                'Unsupported paired primitive')
        positions = attribute(primitive['attributes']['POSITION'])
        normals = attribute(primitive['attributes']['NORMAL'])
        require(len(positions) == len(normals), 'Paired normal count mismatch')
        uv = (attribute(primitive['attributes']['TEXCOORD_0'], 2)
              if sources[primitive['material']] is not None else None)
        require(uv is None or len(uv) * 3 == len(positions) * 2, 'Paired UV count mismatch')
        return positions, normals, uv

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
            fp, fn, fu = attributes(groups[front])
            bp, bn, bu = attributes(groups[back])
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
            if bu is not None:
                reordered = bytearray(len(bu))
                for start in range(0, len(bu), 24):
                    reordered[start:start+8] = bu[start:start+8]
                    reordered[start+8:start+16] = bu[start+16:start+24]
                    reordered[start+16:start+24] = bu[start+8:start+16]
                offset = len(binary) + len(extra)
                require(offset + len(reordered) <= 256 * 1024 * 1024,
                        'Derived UV buffers exceed GLB bound')
                views.append({'buffer': 0, 'byteOffset': offset,
                              'byteLength': len(reordered), 'target': 34962})
                accessors.append({'bufferView': len(views) - 1, 'componentType': 5126,
                                  'count': len(reordered) // 8, 'type': 'VEC2'})
                extra.extend(reordered)
                attrs = groups[front]['attributes']
                # Blender imports contiguous UV sets. A plain front gets an unused
                # set zero so the independent back is always UVMap.001.
                attrs.setdefault('TEXCOORD_0', len(accessors) - 1)
                attrs['TEXCOORD_1'] = len(accessors) - 1
        mesh['primitives'] = [p for p in mesh['primitives'] if p.get('material') not in back_to_front]
        require(bool(mesh['primitives']), 'Sided conversion removed a complete mesh')
    require(seen == set(colors), 'Unused sided material pair')
    root['buffers'][0]['byteLength'] = len(binary) + len(extra)
    encoded = json.dumps(root, separators=(',', ':'), allow_nan=False).encode('utf-8')
    encoded += b' ' * (-len(encoded) % 4)
    require(len(encoded) <= 16 * 1024 * 1024, 'Derived GLB JSON exceeds its bound')
    converted = (struct.pack('<5I', magic, version, len(encoded) + binary_size + len(extra) + 28,
                             len(encoded), json_type) + encoded
                 + struct.pack('<2I', binary_size + len(extra), binary_type)
                 + binary.tobytes() + extra)
    require(len(converted) <= 256 * 1024 * 1024, 'Derived GLB exceeds its bound')
    return converted, colors


def import_snapshot(scene_bytes, directory):
    converted, back_colors = prepare_sided_glb(scene_bytes)
    back_images = {}
    # Only a private derived copy is imported; original bytes and their hash stay intact.
    with tempfile.TemporaryDirectory(prefix='cycles-import-', dir=directory) as temporary:
        path = Path(temporary) / 'scene.glb'
        path.write_bytes(converted)
        bpy.ops.import_scene.gltf(filepath=str(path))
        for appearance in back_colors.values():
            if 'image' not in appearance or appearance['image'] in back_images:
                continue
            image_path = Path(temporary) / f"back-{appearance['image']}.png"
            image_path.write_bytes(appearance['png'])
            image = bpy.data.images.load(str(image_path), check_existing=False)
            image.colorspace_settings.name = 'sRGB'
            image.alpha_mode = 'STRAIGHT'
            image.pack()
            back_images[appearance['image']] = image
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
        appearance = back_colors[index]
        rgba = appearance['rgba']
        back.inputs['Base Color'].default_value = rgba
        back.inputs['Alpha'].default_value = rgba[3]
        back.inputs['Metallic'].default_value = 0
        back.inputs['Roughness'].default_value = .8
        if 'image' in appearance:
            uv = nodes.new('ShaderNodeUVMap')
            uv.uv_map = 'UVMap.001'
            texture = nodes.new('ShaderNodeTexImage')
            texture.image = back_images[appearance['image']]
            texture.interpolation = 'Linear'
            texture.extension = 'REPEAT'
            links.new(uv.outputs['UV'], texture.inputs['Vector'])
            multiply = nodes.new('ShaderNodeMixRGB')
            multiply.blend_type = 'MULTIPLY'
            multiply.inputs[0].default_value = 1
            multiply.inputs[2].default_value = rgba
            links.new(texture.outputs['Color'], multiply.inputs[1])
            links.new(multiply.outputs[0], back.inputs['Base Color'])
            alpha = nodes.new('ShaderNodeMath')
            alpha.operation = 'MULTIPLY'
            alpha.inputs[1].default_value = rgba[3]
            links.new(texture.outputs['Alpha'], alpha.inputs[0])
            links.new(alpha.outputs[0], back.inputs['Alpha'])
        geometry = nodes.new('ShaderNodeNewGeometry')
        mix = nodes.new('ShaderNodeMixShader')
        links.new(geometry.outputs['Backfacing'], mix.inputs[0])
        links.new(front.outputs['BSDF'], mix.inputs[1])
        links.new(back.outputs['BSDF'], mix.inputs[2])
        links.new(mix.outputs[0], output.inputs['Surface'])
        material.use_backface_culling = False
    if found != set(back_colors):
        raise WorkerError('invalid_snapshot', 'Importer did not preserve sided appearance identities')


def environment_world(world, metadata, source):
    if (type(metadata) is not dict or metadata.get('file') != 'environment.hdr'
            or metadata.get('mediaType') != 'image/vnd.radiance'
            or metadata.get('projection') != 'equirectangular'
            or metadata.get('colorSpace') != 'Linear Rec.709'
            or type(metadata.get('width')) is not int or type(metadata.get('height')) is not int
            or not 1 <= metadata['height'] <= 2048
            or metadata['width'] != metadata['height'] * 2
            or type(metadata.get('bytes')) is not int or not 0 < metadata['bytes'] <= 40 * 1024 * 1024
            or any(type(metadata.get(key)) not in (int, float) or not math.isfinite(metadata[key])
                   for key in ('strength', 'rotationDegrees'))
            or not 0 <= metadata['strength'] <= 100 or not -360 <= metadata['rotationDegrees'] <= 360):
        raise WorkerError('invalid_snapshot', 'Invalid frozen HDR environment metadata')
    path = source / 'environment.hdr'
    if path.is_symlink() or not path.is_file() or path.stat().st_size != metadata['bytes']:
        raise WorkerError('invalid_snapshot', 'Expected the captured bounded HDR environment')
    data = path.read_bytes()
    if sha(data) != metadata.get('sha256'):
        raise WorkerError('invalid_snapshot', 'Captured HDR environment changed')
    # Decode exactly the verified bytes from a private temporary path, then pack.
    with tempfile.NamedTemporaryFile(suffix='.hdr') as temporary:
        temporary.write(data)
        temporary.flush()
        image = bpy.data.images.load(temporary.name, check_existing=False)
        if tuple(image.size) != (metadata['width'], metadata['height']) or not image.is_float:
            raise WorkerError('invalid_snapshot', 'Decoded environment dimensions or format mismatch')
        image.colorspace_settings.name = 'Linear Rec.709'
        image.pack()
    nodes, links = world.node_tree.nodes, world.node_tree.links
    texture = nodes.new('ShaderNodeTexEnvironment')
    texture.image = image
    texture.projection = 'EQUIRECTANGULAR'
    texture.interpolation = 'Linear'
    coordinates = nodes.new('ShaderNodeTexCoord')
    mapping = nodes.new('ShaderNodeVectorRotate')
    mapping.rotation_type = 'Z_AXIS'
    mapping.inputs['Angle'].default_value = math.radians(-metadata['rotationDegrees'])
    links.new(coordinates.outputs['Generated'], mapping.inputs['Vector'])
    links.new(mapping.outputs['Vector'], texture.inputs['Vector'])
    links.new(texture.outputs['Color'], nodes['Background'].inputs['Color'])
    nodes['Background'].inputs['Strength'].default_value = metadata['strength']


def configure_lighting(manifest, source=None):
    """Use the frozen native sun direction; never consult location/time on this host."""
    solar = manifest.get('solar')
    position = manifest.get('solarPosition')
    if solar is None and position is None:
        enabled = False  # Pre-sun snapshots retain their original studio lighting.
    else:
        if (type(solar) is not dict or type(position) is not dict
                or type(solar.get('enabled')) is not bool
                or type(solar.get('shadows')) is not bool
                or solar.get('algorithm') != 'noaa-meeus-geometric-v1'
                or position.get('algorithm') != solar['algorithm']):
            raise WorkerError('invalid_snapshot', 'Invalid frozen sun study')
        enabled = solar['enabled']
        direction = position.get('direction')
        if (type(direction) is not list or len(direction) != 3
                or any(type(value) not in (int, float) or not math.isfinite(value)
                       for value in direction)
                or abs(sum(value * value for value in direction) - 1) > 1e-9
                or type(position.get('aboveHorizon')) is not bool
                or position['aboveHorizon'] != (direction[2] > 0)
                or type(position.get('directLightActive')) is not bool
                or position['directLightActive'] != (enabled and position['aboveHorizon'])
                or type(position.get('shadowsActive')) is not bool
                or position['shadowsActive'] != (enabled and solar['shadows'] and position['aboveHorizon'])):
            raise WorkerError('invalid_snapshot', 'Invalid frozen sun direction or daylight state')
    scene = bpy.context.scene
    world = bpy.data.worlds.new('SketchyUp ambient world')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (1, 1, 1, 1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = .25
    scene.world = world
    environment = manifest.get('environment')
    if environment is not None:
        if source is None:
            raise WorkerError('invalid_snapshot', 'Missing packaged environment directory')
        environment_world(world, environment, source)
    world_strength = environment['strength'] if environment is not None else .25
    if enabled:
        energy = 3.0 if position['directLightActive'] else 0.0
        report = {'mode': 'solar-hdri-v1' if environment is not None else 'solar-v1',
                  'worldStrength': world_strength, 'energy': energy,
                  'angle': .00935, 'shadows': position['shadowsActive'],
                  'settings': solar, 'position': position}
        if environment is not None:
            report['environment'] = environment
        if energy:
            light = bpy.data.lights.new('SketchyUp Sun', 'SUN')
            light.energy = energy
            light.angle = report['angle']
            light.use_shadow = report['shadows']
            obj = bpy.data.objects.new(light.name, light)
            scene.collection.objects.link(obj)
            obj.rotation_euler = (-Vector(direction)).to_track_quat('-Z', 'Y').to_euler()
        return report
    if environment is not None:
        return {'mode': 'hdri-v1', 'worldStrength': world_strength, 'environment': environment}
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
    return {'mode': 'studio-v1', 'worldStrength': .25}


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
    engine = settings.get('engine', 'cycles')
    if engine not in ('cycles', 'eevee') or (engine == 'eevee') != (request['backend'] == 'OPENGL'):
        raise WorkerError('invalid_request', 'Render engine and selected device backend do not match')
    progress('loading')
    bpy.ops.wm.read_factory_settings(use_empty=True)
    import_snapshot(scene_bytes, directory)
    del scene_bytes
    scene = bpy.context.scene
    cameras = [obj for obj in scene.objects if obj.type == 'CAMERA']
    if len(cameras) != 1:
        raise WorkerError('invalid_snapshot', 'Snapshot requires exactly one render camera')
    scene.camera = cameras[0]
    scene.render.engine = 'BLENDER_EEVEE' if engine == 'eevee' else 'CYCLES'
    actual_device = configure_device(request['backend'], request['deviceId'])
    if engine == 'eevee':
        scene.eevee.taa_render_samples = settings['samples']
        scene.eevee.use_raytracing = False
        scene.eevee.use_fast_gi = True
        scene.eevee.use_shadows = True
        scene.eevee.shadow_resolution_scale = 1
        scene.eevee.shadow_ray_count = 1
        scene.eevee.shadow_step_count = 6
    else:
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
    lighting = configure_lighting(manifest, source)
    scene.render.filepath = str(directory / 'image.png')
    progress('rendering')
    bpy.ops.render.render(write_still=True)
    progress('verifying-output')
    image = directory / 'image.png'
    if image.is_symlink() or not image.is_file() or not 0 < image.stat().st_size <= 64 * 1024 * 1024:
        raise WorkerError('render_error', 'Renderer did not produce a bounded PNG')
    image_bytes = image.read_bytes()
    losses = dict(manifest['losses'])
    if manifest.get('solar', {}).get('enabled'):
        losses['solarLightingOmitted'] = 0
    if 'environment' in manifest:
        losses['environmentLightingOmitted'] = 0
    if engine == 'eevee':
        losses['indirectLightingApproximated'] = 1
    preset = {'name': lighting['mode'], 'engine': scene.render.engine, 'threads': 4,
              'samplingPolicy': 'eevee-preview-v1' if engine == 'eevee' else 'cycles-fixed-v1',
              'samples': settings['samples'], 'viewTransform': 'Standard', 'look': 'None',
              'exposure': 0, 'gamma': 1, 'worldStrength': lighting['worldStrength']}
    if engine == 'eevee':
        preset.update({'rayTracing': False, 'fastGI': True, 'shadows': True,
                       'shadowResolutionScale': 1, 'shadowRayCount': 1, 'shadowStepCount': 6})
        if settings['seed']:
            losses['samplingSeedNotApplied'] = 1
    else:
        preset.update({'adaptiveSampling': False, 'denoising': False, 'maxBounces': 8,
                       'seed': settings['seed']})
    return {'status': 'succeeded', 'documentId': manifest['documentId'], 'revision': manifest['revision'],
            'manifestSha256': request['manifestSha256'], 'sceneSha256': manifest['scene']['sha256'],
            'settings': settings, 'device': actual_device, 'losses': losses, 'lighting': lighting,
            'preset': preset,
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
        if request['backend'] != 'OPENGL' and (not bpy.app.build_options.cycles or 'cycles' not in bpy.context.preferences.addons):
            raise WorkerError('cycles_unavailable', 'This Blender build does not include Cycles')
        if request['operation'] == 'probe':
            result = {'status': 'available', 'lightingPolicies': ['studio-v1', 'solar-v1', 'hdri-v1', 'solar-hdri-v1'],
                      **capabilities(request['backend'])}
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
