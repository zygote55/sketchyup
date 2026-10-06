"""Check scoped sections after actual Blender import and Cycles rendering."""
import hashlib
import importlib.util
import json
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
for fixture in ('nested', 'texture'):
    source = root / fixture / 'scene.glb'
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    worker.import_snapshot(data, output)
    scene = bpy.context.scene
    objects = [obj for obj in scene.objects if obj.type == 'MESH']
    assert len(objects) == (2 if fixture == 'nested' else 1)
    vertices = [obj.matrix_world @ v.co for obj in objects for v in obj.data.vertices]
    if fixture == 'texture':
        assert all(-4.00001 <= p.x <= -1.99999 and -.00001 <= p.y <= 1.50001 and abs(p.z) < 1e-5 for p in vertices), 'Reflected textured half in native world coordinates'
        assert all(obj.data.uv_layers.active for obj in objects), 'Clipped UV layer survives worker import'
        reports.append({'fixture': fixture, 'boundedWorldVertices': len(vertices), 'uvImported': True, 'passed': True})
        continue
    assert all(-1e-5 <= p.z <= 1.00001 for p in vertices), 'Model cut removes all upper geometry'
    for p in vertices:
        assert ((-1e-5 <= p.x <= 1.00001 and .99999 <= p.y <= 2.00001) or
                (3.49999 <= p.x <= 5.50001 and -1e-5 <= p.y <= 2.00001)), 'Nested quarter and independently clipped sibling'
    cap_area = 0
    for obj in objects:
        for polygon in obj.data.polygons:
            points = [obj.matrix_world @ obj.data.vertices[i].co for i in polygon.vertices]
            if all(abs(p.z - 1) < 1e-5 for p in points):
                assert len(points) == 3
                cap_area += (points[1] - points[0]).cross(points[2] - points[0]).length * .5
                for loop in polygon.loop_indices:
                    normal = (obj.matrix_world.to_3x3().inverted().transposed() @ obj.data.corner_normals[loop].vector).normalized()
                    assert (normal - Vector((0, 0, 1))).length < 1e-4, 'Imported mirrored cap normal points out of retained volume'
    assert abs(cap_area - 5) < 1e-5, ('Independent imported cap area', cap_area)
    scene.render.engine = engine
    scene.cycles.samples = 16
    scene.cycles.seed = 7
    scene.cycles.use_denoising = False
    scene.cycles.use_adaptive_sampling = False
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 2
    scene.render.resolution_x, scene.render.resolution_y = 260, 100
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
    light = bpy.data.lights.new('Section oracle', 'SUN')
    light.energy = 2
    light.angle = 0
    sun = bpy.data.objects.new('Section oracle', light)
    scene.collection.objects.link(sun)
    sun.rotation_euler = Vector((0, 0, -1)).to_track_quat('-Z', 'Y').to_euler()
    camera = next(obj for obj in scene.objects if obj.type == 'CAMERA')
    scene.camera = camera
    camera.parent = None
    camera.rotation_mode = 'XYZ'
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 6.5
    camera.data.sensor_fit = 'HORIZONTAL'
    camera.location = (2.75, 1, 8)
    camera.rotation_euler = Vector((0, 0, -1)).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(output / 'sections.png')
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(scene.render.filepath)
    def pixel(x, y):
        px = round(((x - 2.75) / 6.5 + .5) * 260)
        py = round(((y - 1) / 2.5 + .5) * 100)
        i = 4 * (py * 260 + px)
        return list(image.pixels[i:i+4])
    probes = {'nested': pixel(.5, 1.5), 'removed': pixel(1.5, 1.5), 'sibling': pixel(4.5, 1)}
    for name in ('nested', 'sibling'):
        color = probes[name]
        assert color[3] > .99 and color[1] > .1 and color[1] > color[0] * 3 and color[1] > color[2] * 3, ('Cycles displays green cap', name, color)
    assert probes['removed'][3] < .01, ('Removed geometry is transparent in real render', probes)
    bpy.data.images.remove(image)
    assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
    reports.append({'fixture': fixture, 'capArea': cap_area, 'worldCapsOutward': True, 'pixels': probes, 'passed': True})
assert not list(output.glob('cycles-import-*'))
result = {'blender': bpy.app.version_string, 'engine': engine, 'reports': reports}
(root / ('section-blender-eevee-validation.json' if engine == 'BLENDER_EEVEE' else 'section-blender-validation.json')).write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
