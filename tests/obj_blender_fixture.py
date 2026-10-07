"""Independent Blender producer for the bounded OBJ importer acceptance test."""
import bpy
import os
import sys

folder = sys.argv[sys.argv.index('--') + 1]
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
mesh = bpy.data.meshes.new('Metre triangle')
mesh.from_pydata([(0, 0, 0), (2, 0, 0), (0, 3, 0)], [], [(0, 1, 2)])
mesh.uv_layers.new()
for loop, uv in zip(mesh.uv_layers.active.data, [(0, 0), (1, 0), (0, 1)]):
    loop.uv = uv
image = bpy.data.images.new('Checker', width=2, height=2, alpha=True)
image.pixels = [1, 0, 0, 1, 0, 1, 0, .5, 0, 0, 1, 1, 1, 1, 1, 1]
image.filepath_raw = os.path.join(folder, 'checker.png')
image.file_format = 'PNG'
image.save()
material = bpy.data.materials.new('Textured triangle')
material.use_nodes = True
principled = material.node_tree.nodes.get('Principled BSDF')
principled.inputs['Metallic'].default_value = 0
principled.inputs['Roughness'].default_value = 1
texture = material.node_tree.nodes.new('ShaderNodeTexImage')
texture.image = image
material.node_tree.links.new(texture.outputs['Color'], principled.inputs['Base Color'])
mesh.materials.append(material)
parent = bpy.data.objects.new('Parent', None)
bpy.context.collection.objects.link(parent)
parent.location = (10, 20, 30)
for name, scale in [('Ordinary', (1, 1, 1)), ('Mirrored', (-1, 1, 1))]:
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.parent = parent
    obj.scale = scale
bpy.ops.wm.obj_export(filepath=os.path.join(folder, 'blender.obj'),
                      forward_axis='NEGATIVE_Z', up_axis='Y',
                      export_materials=True, export_uv=True, export_normals=True,
                      path_mode='RELATIVE', export_triangulated_mesh=False)
print('SKETCHYUP_OBJ_FIXTURE_READY')
