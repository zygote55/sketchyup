"""Independent Blender consumer of SketchyUp's OBJ/MTL package."""
import bpy
import sys
from mathutils import Vector

path = sys.argv[sys.argv.index('--') + 1]
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.obj_import(filepath=path, forward_axis='NEGATIVE_Z', up_axis='Y',
                      global_scale=.001, use_split_objects=True, use_split_groups=False)
meshes = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
assert len(meshes) == 1
obj = meshes[0]
assert len(obj.data.polygons) == 1 and len(obj.data.vertices) == 6
points = [obj.matrix_world @ v.co for v in obj.data.vertices]
for expected in [(10, 20, 30), (6, 20, 30), (10, 26, 30)]:
    assert min((p-Vector(expected)).length for p in points) < 1e-4
normal = obj.matrix_world.to_3x3().inverted().transposed() @ obj.data.polygons[0].normal
assert normal.normalized().z > .9999
assert obj.data.uv_layers.active is not None
material = obj.data.materials[0]
textures = [node.image for node in material.node_tree.nodes if node.type == 'TEX_IMAGE' and node.image]
assert len(textures) == 1 and textures[0].size[:] == (1, 1)
shader = material.node_tree.nodes.get('Principled BSDF')
assert abs(shader.inputs['Alpha'].default_value - .8) < 1e-5
print('SKETCHYUP_OBJ_CONSUMER_VERIFIED')
