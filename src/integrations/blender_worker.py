"""SketchyUp's fixed Blender adapter. No model-provided code is evaluated."""
import hashlib
import json
import math
import os
from pathlib import Path
import sys

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
    del scene_bytes
    progress('loading')
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(scene_path))
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
