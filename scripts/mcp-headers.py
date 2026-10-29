#!/usr/bin/env python3
"""Python equivalent of mcp-headers.cjs for machines without Node (macOS ships python3)."""
import json
import os
import re
import subprocess
import sys
from urllib.parse import urlparse


def run(args):
    return subprocess.run(args, capture_output=True, check=True, encoding='utf-8', errors='replace').stdout


def listening_pid(port):
    """PID of the process listening on the MCP port."""
    if sys.platform == 'win32':
        for line in run(['netstat', '-ano', '-p', 'tcp']).splitlines():
            cols = line.split()
            if len(cols) >= 5 and 'LISTEN' in cols[3].upper() and cols[1].endswith(':%d' % port):
                return int(cols[4])
    else:
        pids = run(['lsof', '-nP', '-iTCP:%d' % port, '-sTCP:LISTEN', '-t']).split()
        if pids:
            return int(pids[0])
    raise RuntimeError('Nothing is listening on port %d' % port)


def process_info(pid):
    """Executable name and full command line of a process."""
    if sys.platform == 'win32':
        ps = ('[Console]::OutputEncoding=[Text.Encoding]::UTF8;'
              '$p=Get-CimInstance Win32_Process -Filter "ProcessId=%d";$p.Name;$p.CommandLine' % pid)
        lines = run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', ps]).splitlines() + ['', '']
        return lines[0], lines[1]
    return (os.path.basename(run(['ps', '-p', str(pid), '-o', 'comm=']).strip()),
            run(['ps', '-p', str(pid), '-o', 'args=']).strip())


def project_from_command_line(cmdline):
    """The .uproject on the editor command line; paths may contain spaces or CJK characters."""
    exe = re.search(r'UnrealEditor(?:-Cmd)?(?:\.exe)?"?(?=\s)', cmdline, re.I)
    args = cmdline[exe.end():] if exe else cmdline
    m = re.search(r'"([^"]+\.uproject)"|(\S.*?\.uproject)(?=\s|$)', args, re.I)
    if not m:
        raise RuntimeError('Editor command line has no .uproject')
    return m.group(1) or m.group(2)


def auto_project():
    """With no project given, use the UnrealEditor listening on the MCP port, so the token matches the open project."""
    url = urlparse(os.environ.get('CLAUDE_CODE_MCP_SERVER_URL') or os.environ.get('UE_MCP_URL') or 'http://127.0.0.1:3000/mcp')
    name, cmdline = process_info(listening_pid(url.port or 80))
    if not name.lower().startswith('unrealeditor'):
        raise RuntimeError('Port owner is not UnrealEditor')
    return project_from_command_line(cmdline)


def headers(project):
    if not project or project == 'auto':
        project = auto_project()
    resolved = os.path.abspath(project)
    folder = os.path.dirname(resolved) if resolved.lower().endswith('.uproject') else resolved
    # Prefer the scoped token; the legacy all-or-nothing token is deprecated.
    mcp_dir = os.path.join(folder, 'Saved', 'MCP')
    scoped = os.path.join(mcp_dir, 'scoped-capability-token')
    path = scoped if os.path.isfile(scoped) else os.path.join(mcp_dir, 'capability-token')
    with open(path, encoding='utf-8') as f:
        token = f.read().strip()
    if not re.fullmatch(r'[a-fA-F0-9]{64}', token):
        raise ValueError('Invalid capability token format')
    return {'X-MCP-Capability-Token': token}


if __name__ == '__main__':
    try:
        sys.stdout.write(json.dumps(headers(sys.argv[1] if len(sys.argv) > 1 else os.environ.get('UE_PROJECT_PATH'))))
    except Exception as e:
        sys.stderr.write('Cannot read MCP capability token: %s\n' % e)
        sys.exit(1)
