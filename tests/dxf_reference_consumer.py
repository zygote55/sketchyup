"""Independent ezdxf consumer of SketchyUp's documented 2D export."""
import sys
import math
import ezdxf
from ezdxf import bbox
assert ezdxf.__version__ == '1.4.4'
doc = ezdxf.readfile(sys.argv[1])
assert doc.dxfversion == 'AC1032' and doc.units == 4
model = doc.modelspace()
assert len(model) == 5
assert len(model.query('LINE')) == 1
assert len(model.query('LWPOLYLINE')) == 1
assert len(model.query('ARC')) == 2
assert len(model.query('CIRCLE')) == 1
assert doc.layers.get('Walls').is_off()
line = model.query('LINE')[0]
assert line.dxf.layer == 'Walls'
assert line.dxf.start.isclose((0, 0, 0)) and line.dxf.end.isclose((6000, 4000, 0))
assert abs(model.query('CIRCLE')[0].dxf.radius - 500) < 1e-6
sweeps = sorted((arc.dxf.end_angle-arc.dxf.start_angle) % 360 for arc in model.query('ARC'))
assert abs(sweeps[0]-20) < 1e-6 and abs(sweeps[1]-180) < 1e-6
extents = bbox.extents(model, fast=False)
assert extents.extmin.isclose((0, -1000, 0), abs_tol=1e-5)
assert extents.extmax.isclose((6000, 4000, 0), abs_tol=1e-5)
print('SKETCHYUP_DXF_CONSUMER_VERIFIED')
