'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

function run(cmd, args) {
  return execFileSync(cmd, args, { encoding: 'utf8', windowsHide: true });
}

// PID of the process listening on the MCP port.
function listeningPid(port) {
  if (process.platform === 'win32') {
    for (const line of run('netstat', ['-ano', '-p', 'tcp']).split(/\r?\n/)) {
      const cols = line.trim().split(/\s+/);
      if (cols.length >= 5 && /LISTEN/i.test(cols[3]) && cols[1].endsWith(':' + port)) return Number(cols[4]);
    }
  } else {
    const pid = run('lsof', ['-nP', '-iTCP:' + port, '-sTCP:LISTEN', '-t']).trim().split(/\s+/)[0];
    if (pid) return Number(pid);
  }
  throw new Error('Nothing is listening on port ' + port);
}

// Executable name and full command line of a process.
function processInfo(pid) {
  if (process.platform === 'win32') {
    const ps = '[Console]::OutputEncoding=[Text.Encoding]::UTF8;' +
      '$p=Get-CimInstance Win32_Process -Filter "ProcessId=' + pid + '";$p.Name;$p.CommandLine';
    const [name = '', cmdline = ''] = run('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', ps]).split(/\r?\n/);
    return { name, cmdline };
  }
  return { name: path.basename(run('ps', ['-p', String(pid), '-o', 'comm=']).trim()),
    cmdline: run('ps', ['-p', String(pid), '-o', 'args=']).trim() };
}

// The .uproject on the editor command line; paths may contain spaces or CJK characters.
function projectFromCommandLine(cmdline) {
  const exe = cmdline.match(/UnrealEditor(?:-Cmd)?(?:\.exe)?"?(?=\s)/i);
  const args = exe ? cmdline.slice(exe.index + exe[0].length) : cmdline;
  const m = args.match(/"([^"]+\.uproject)"|(\S.*?\.uproject)(?=\s|$)/i);
  if (!m) throw new Error('Editor command line has no .uproject');
  return m[1] || m[2];
}

// With no project given, use the UnrealEditor that is listening on the MCP port,
// so the token always matches whichever project is open.
function autoProject() {
  const url = new URL(process.env.CLAUDE_CODE_MCP_SERVER_URL || process.env.UE_MCP_URL || 'http://127.0.0.1:3000/mcp');
  const { name, cmdline } = processInfo(listeningPid(url.port || 80));
  if (!/^UnrealEditor/i.test(name)) throw new Error('Port owner is not UnrealEditor');
  return projectFromCommandLine(cmdline);
}

function headers(project) {
  if (!project || project === 'auto') project = autoProject();
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
module.exports = { headers, autoProject, projectFromCommandLine };
if (require.main === module) {
  try { process.stdout.write(JSON.stringify(headers(process.argv[2] || process.env.UE_PROJECT_PATH))); }
  catch (e) { process.stderr.write('Cannot read MCP capability token: ' + e.message + '\n'); process.exitCode = 1; }
}
