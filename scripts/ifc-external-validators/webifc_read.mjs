// Reads an IFC file with web-ifc (WASM) and prints what it sees: schema, mesh count, triangles, bounds.
import { createRequire } from 'module';
import fs from 'fs';
const require = createRequire(process.argv[2] + '/');   // directory that holds node_modules/web-ifc
const WebIFC = require('web-ifc');
const api = new WebIFC.IfcAPI();
await api.Init();
const data = new Uint8Array(fs.readFileSync(process.argv[3]));
const id = api.OpenModel(data);
let meshes = 0, geoms = 0, triangles = 0, vertices = 0;
const minmax = [Infinity, Infinity, Infinity, -Infinity, -Infinity, -Infinity];
api.StreamAllMeshes(id, (mesh) => {
  meshes++;
  const placed = mesh.geometries;
  for (let i = 0; i < placed.size(); i++) {
    const pg = placed.get(i);
    const g = api.GetGeometry(id, pg.geometryExpressID);
    const idx = api.GetIndexArray(g.GetIndexData(), g.GetIndexDataSize());
    const vtx = api.GetVertexArray(g.GetVertexData(), g.GetVertexDataSize());
    geoms++;
    triangles += idx.length / 3;
    vertices += vtx.length / 6;
    const m = pg.flatTransformation;
    for (let v = 0; v < vtx.length; v += 6) {
      const x = vtx[v], y = vtx[v+1], z = vtx[v+2];
      const X = m[0]*x + m[4]*y + m[8]*z + m[12], Y = m[1]*x + m[5]*y + m[9]*z + m[13], Z = m[2]*x + m[6]*y + m[10]*z + m[14];
      minmax[0] = Math.min(minmax[0], X); minmax[1] = Math.min(minmax[1], Y); minmax[2] = Math.min(minmax[2], Z);
      minmax[3] = Math.max(minmax[3], X); minmax[4] = Math.max(minmax[4], Y); minmax[5] = Math.max(minmax[5], Z);
    }
  }
});
const count = (t) => api.GetLineIDsWithType(id, t).size();
console.log(JSON.stringify({ schema: api.GetModelSchema(id), meshes, geoms, triangles, vertices, projects: count(WebIFC.IFCPROJECT), sites: count(WebIFC.IFCSITE), walls: count(WebIFC.IFCWALL), bounds: minmax.map(v => Math.round(v*1e6)/1e6) }));
api.CloseModel(id);
