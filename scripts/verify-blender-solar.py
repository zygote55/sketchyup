"""Exercise frozen sun conversion and analytical shadow locations in actual Cycles."""
import copy
import importlib.util
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

engine = 'BLENDER_EEVEE' if sys.argv[-1] == 'eevee' else 'CYCLES'
output = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
output.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('worker', Path(__file__).resolve().parents[1] / 'src/integrations/blender_worker.py')
worker = importlib.util.module_from_spec(spec)
sys.dont_write_bytecode = True
spec.loader.exec_module(worker)
reports = []
for variant in ('east', 'north', 'no-shadows', 'night', 'disabled', 'legacy'):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    bpy.ops.mesh.primitive_plane_add(size=12)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, .5))
    material = bpy.data.materials.new('Neutral diffuse oracle')
    material.use_nodes = True
    material.node_tree.nodes.clear()
    diffuse = material.node_tree.nodes.new('ShaderNodeBsdfDiffuse')
    diffuse.inputs['Color'].default_value = (.6, .6, .6, 1)
    surface = material.node_tree.nodes.new('ShaderNodeOutputMaterial')
    material.node_tree.links.new(diffuse.outputs[0], surface.inputs['Surface'])
    for obj in scene.objects:
        obj.data.materials.append(material)
    direction = ([0, 1, 1] if variant == 'north' else [1, 0, -1] if variant == 'night' else [1, 0, 1])
    direction = [value / math.sqrt(2) for value in direction]
    enabled, daylight, shadows = variant != 'disabled', variant != 'night', variant != 'no-shadows'
    manifest = {'nativeBounds': {'min': [-6, -6, 0], 'max': [6, 6, 1]},
                'solar': {'enabled': enabled, 'shadows': shadows, 'algorithm': 'noaa-meeus-geometric-v1'},
                'solarPosition': {'algorithm': 'noaa-meeus-geometric-v1', 'direction': direction,
                                  'aboveHorizon': daylight, 'directLightActive': enabled and daylight,
                                  'shadowsActive': enabled and daylight and shadows}}
    if variant == 'legacy':
        del manifest['solar'], manifest['solarPosition']
    before = copy.deepcopy(manifest)
    lighting = worker.configure_lighting(manifest)
    assert manifest == before, 'Frozen input is immutable'
    lights = [obj for obj in scene.objects if obj.type == 'LIGHT']
    if variant in ('disabled', 'legacy'):
        assert lighting == {'mode': 'studio-v1', 'worldStrength': .25}
        assert len(lights) == 2 and all(obj.data.type == 'AREA' for obj in lights)
        reports.append({'variant': variant, 'studioRetained': True})
        continue
    assert lighting['mode'] == 'solar-v1'
    if variant == 'night':
        assert not lights and lighting['energy'] == 0 and not lighting['shadows']
    else:
        assert len(lights) == 1 and lights[0].data.type == 'SUN'
        actual = lights[0].rotation_euler.to_matrix() @ Vector((0, 0, 1))
        assert (actual - Vector(direction)).length < 1e-6, 'Sun local +Z points toward frozen sun'
        assert lights[0].data.use_shadow == shadows
    camera = bpy.data.objects.new('Oracle camera', bpy.data.cameras.new('Oracle camera'))
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera.location = (0, 0, 10)
    camera.rotation_euler = (0, 0, 0)
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 6
    scene.render.engine = engine
    scene.cycles.samples = 64
    scene.cycles.seed = 7
    scene.cycles.use_denoising = False
    scene.cycles.use_adaptive_sampling = False
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 2
    scene.render.resolution_x = scene.render.resolution_y = 120
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.render.filepath = str(output / (variant + '.png'))
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(scene.render.filepath)
    def sample(x, y):
        px, py = round((x / 6 + .5) * 120), round((y / 6 + .5) * 120)
        values = []
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                index = 4 * ((py + dy) * 120 + px + dx)
                values.append(sum(image.pixels[index:index + 3]) / 3)
        return sum(values) / len(values)
    probes = {'west': sample(-1, 0), 'south': sample(0, -1), 'lit': sample(2, 2)}
    if variant == 'east':
        assert probes['west'] < probes['lit'] * .7 and probes['south'] > probes['lit'] * .85, probes
    elif variant == 'north':
        assert probes['south'] < probes['lit'] * .7 and probes['west'] > probes['lit'] * .85, probes
    elif variant == 'no-shadows':
        assert probes['west'] > probes['lit'] * .85, probes
    elif variant == 'night':
        assert probes['lit'] < reports[0]['pixels']['lit'] * .7, probes
    bpy.data.images.remove(image)
    reports.append({'variant': variant, 'pixels': probes, 'lighting': lighting, 'inputUnchanged': True})
# Reject inconsistent/malformed frozen data before creating lights.
for field, value in [('direction', [1, 0, 1]), ('directLightActive', False), ('shadowsActive', False), ('aboveHorizon', False)]:
    bad = copy.deepcopy(before)
    bad['solar'] = {'enabled': True, 'shadows': True, 'algorithm': 'noaa-meeus-geometric-v1'}
    bad['solarPosition'] = {'algorithm': 'noaa-meeus-geometric-v1', 'direction': [math.sqrt(.5), 0, math.sqrt(.5)], 'aboveHorizon': True, 'directLightActive': True, 'shadowsActive': True}
    bad['solarPosition'][field] = value
    try:
        worker.configure_lighting(bad)
        raise AssertionError('Invalid frozen solar input accepted')
    except worker.WorkerError as error:
        assert error.code == 'invalid_snapshot'
result = {'blender': bpy.app.version_string, 'engine': engine, 'reports': reports, 'invalidSnapshotsRejected': 4}
(output / ('solar-blender-eevee-validation.json' if engine == 'BLENDER_EEVEE' else 'solar-blender-validation.json')).write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
