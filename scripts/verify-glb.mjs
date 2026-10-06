import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
const [moduleRoot, fixtureRoot, count = '11'] = process.argv.slice(2);
const expectedCount = Number(count);
if (!Number.isSafeInteger(expectedCount) || expectedCount < 1) throw new Error('Expected a positive fixture count');
if (!moduleRoot || !fixtureRoot) throw new Error('Usage: node verify-glb.mjs VALIDATOR_DIRECTORY FIXTURE_DIRECTORY [EXPECTED_COUNT]');
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
if (reports.length !== expectedCount) throw new Error(`Expected ${expectedCount} GLB interoperability fixtures`);
console.log(JSON.stringify({validator:validator.version(), reports}, null, 2));
