"""Independent Blender consumer verifies both STL encodings and reflected winding."""
import bpy
import bmesh
import os
import sys
folder = sys.argv[sys.argv.index('--') + 1]
for name in ['binary.stl', 'ascii.stl']:
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.wm.stl_import(filepath=os.path.join(folder, name), global_scale=.001,
                          forward_axis='Y', up_axis='Z', use_mesh_validate=True)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
    assert len(meshes) == 1
    obj = meshes[0]
    points = [obj.matrix_world @ v.co for v in obj.data.vertices]
    assert len(points) == 8 and len(obj.data.polygons) == 12
    for axis, low, high in [(0, 8, 10), (1, 20, 23), (2, 30, 34)]:
        assert abs(min(p[axis] for p in points) - low) < 1e-4
        assert abs(max(p[axis] for p in points) - high) < 1e-4
    mesh = bmesh.new()
    mesh.from_mesh(obj.data)
    mesh.transform(obj.matrix_world)
    assert all(edge.is_manifold for edge in mesh.edges)
    assert abs(mesh.calc_volume(signed=True) - 24) < 1e-4
    mesh.free()
print('SKETCHYUP_STL_CONSUMER_VERIFIED')
