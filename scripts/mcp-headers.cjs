'use strict';
const fs = require('node:fs');
const path = require('node:path');
function headers(project) {
  if (!project) throw new Error('Specify a project path or UE_PROJECT_PATH');
  const resolved = path.resolve(project);
  const dir = resolved.toLowerCase().endsWith('.uproject') ? path.dirname(resolved) : resolved;
  // Prefer the scoped token; the legacy all-or-nothing token is deprecated.
  const mcpDir = path.join(dir, 'Saved', 'MCP');
  const scoped = path.join(mcpDir, 'scoped-capability-token');
  const file = fs.existsSync(scoped) ? scoped : path.join(mcpDir, 'capability-token');
  const token = fs.readFileSync(file, 'utf8').trim();
  if (!/^[a-f0-9]{64}$/i.test(token)) throw new Error('Invalid capability token format');
  return { 'X-MCP-Capability-Token': token };
}
module.exports = { headers };
if (require.main === module) {
  try { process.stdout.write(JSON.stringify(headers(process.argv[2] || process.env.UE_PROJECT_PATH))); }
  catch (_) { process.stderr.write('Cannot read MCP capability token. Check project path and Native MCP startup.\n'); process.exitCode = 1; }
}
