import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
const [moduleRoot, fixtureRoot] = process.argv.slice(2);
if (!moduleRoot || !fixtureRoot) throw new Error('Usage: node verify-glb.mjs VALIDATOR_DIRECTORY FIXTURE_DIRECTORY');
const require = createRequire(path.resolve(moduleRoot, 'package.json'));
const validator = require('gltf-validator');
const reports = [];
for (const name of fs.readdirSync(fixtureRoot).sort()) {
  const file = path.join(fixtureRoot, name, 'scene.glb');
  if (!fs.existsSync(file)) continue;
  const report = await validator.validateBytes(new Uint8Array(fs.readFileSync(file)), {
    uri: `${name}/scene.glb`, writeTimestamp: false, maxIssues: 100,
    externalResourceFunction: async () => { throw new Error('External resources are prohibited'); }
  });
  reports.push({fixture:name, ...report});
  if (report.issues.numErrors || report.issues.numWarnings) process.exitCode = 1;
}
if (reports.length !== 4) throw new Error('Expected four GLB interoperability fixtures');
console.log(JSON.stringify({validator:validator.version(), reports}, null, 2));
