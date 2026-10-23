#!/usr/bin/env python3
"""Python equivalent of mcp-headers.cjs for machines without Node (macOS ships python3)."""
import json
import os
import re
import sys


def headers(project):
    if not project:
        raise ValueError('Specify a project path or UE_PROJECT_PATH')
    resolved = os.path.abspath(project)
    folder = os.path.dirname(resolved) if resolved.lower().endswith('.uproject') else resolved
    with open(os.path.join(folder, 'Saved', 'MCP', 'capability-token'), encoding='utf-8') as f:
        token = f.read().strip()
    if not re.fullmatch(r'[a-fA-F0-9]{64}', token):
        raise ValueError('Invalid capability token format')
    return {'X-MCP-Capability-Token': token}


if __name__ == '__main__':
    try:
        sys.stdout.write(json.dumps(headers(sys.argv[1] if len(sys.argv) > 1 else os.environ.get('UE_PROJECT_PATH'))))
    except Exception:
        sys.stderr.write('Cannot read MCP capability token. Check project path and Native MCP startup.\n')
        sys.exit(1)
