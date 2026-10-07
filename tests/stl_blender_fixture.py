"""Independent binary/ASCII STL producer with known millimetre dimensions."""
import bpy
import os
import sys

folder = sys.argv[sys.argv.index('--') + 1]
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.mesh.primitive_cube_add(size=2, location=(10, 20, 30))
bpy.context.object.scale = (-1, 1.5, 2)
for ascii_format, name in [(False, 'binary.stl'), (True, 'ascii.stl')]:
    bpy.ops.wm.stl_export(filepath=os.path.join(folder, name),
                          ascii_format=ascii_format, global_scale=1000,
                          forward_axis='Y', up_axis='Z')
print('SKETCHYUP_STL_FIXTURE_READY')
