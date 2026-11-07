#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ectolens contributors
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Starting the MCP server must not start Ectolens: only a tool call may.

Agents start ectolens-mcp when their session starts (an agent set up for every project starts it
in every session). On 2026-11-04 each such start launched Ectolens over D-Bus, because the server
made a QDBusInterface up front and a QDBusInterface introspects its service when it is made. A
background Ectolens left running across an update then held a replaced program, and KWin refused
its screenshots.

The check runs ectolens-mcp on a private session bus whose only service is a stand-in for Ectolens
(com.ectonomic.Ectolens, the name the server asks): the stand-in writes a file when the bus starts
it. The server is put through what a session does at its start (initialize, the initialized
notice, tools/list, server/discover) and the stand-in must not have started. Then one tool call,
which must start it: so the stand-in is proven to be reachable, and its absence before means
something.

Usage: McpStartsNothingTest.py PATH/TO/ectolens-mcp
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

mcp = os.path.abspath(sys.argv[1])
work = tempfile.mkdtemp(prefix='ectolens-mcp-test.')
try:
    services = os.path.join(work, 'services')
    os.mkdir(services)
    marker = os.path.join(work, 'started')
    stand_in = os.path.join(work, 'stand-in.sh')
    with open(stand_in, 'w') as f:
        # writes the marker and exits without taking the name: the call that started it then fails,
        # which the server reports as an error, and that is fine here
        f.write(f'#!/bin/sh\ndate +%s.%N >> {marker}\n')
    os.chmod(stand_in, 0o755)
    with open(os.path.join(services, 'com.ectonomic.Ectolens.service'), 'w') as f:
        f.write(f'[D-BUS Service]\nName=com.ectonomic.Ectolens\nExec={stand_in}\n')
    conf = os.path.join(work, 'session.conf')
    with open(conf, 'w') as f:
        f.write('<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" '
                '"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">'
                f'<busconfig><type>session</type><listen>unix:dir={work}</listen>'
                f'<servicedir>{services}</servicedir>'
                '<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>')
    bus = subprocess.Popen(['dbus-daemon', f'--config-file={conf}', '--nofork', '--print-address=1'],
                           stdout=subprocess.PIPE, text=True)
    address = bus.stdout.readline().strip()
    env = dict(os.environ, DBUS_SESSION_BUS_ADDRESS=address)
    env.pop('DISPLAY', None)
    env.pop('WAYLAND_DISPLAY', None)
    server = subprocess.Popen([mcp], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1, env=env)
    n = 0

    def send(method, params, notice=False):
        global n
        message = {'jsonrpc': '2.0', 'method': method, 'params': params}
        if not notice:
            n += 1
            message['id'] = n
        server.stdin.write(json.dumps(message) + '\n')
        server.stdin.flush()
        return None if notice else json.loads(server.stdout.readline())

    failures = []
    send('initialize', {'protocolVersion': '2025-06-18', 'clientInfo': {'name': 'test-agent'}, 'capabilities': {}})
    send('notifications/initialized', {}, notice=True)
    tools = send('tools/list', {})
    send('server/discover', {})
    time.sleep(1.5)  # a start the bus asked for has run by now
    if os.path.exists(marker):
        failures.append('the session start (initialize, tools/list, server/discover) started Ectolens')
    if 'list_windows' not in json.dumps(tools):
        failures.append(f'tools/list did not answer with the tools: {tools}')
    send('tools/call', {'name': 'list_windows', 'arguments': {}})
    for _ in range(50):
        if os.path.exists(marker):
            break
        time.sleep(0.1)
    if not os.path.exists(marker):
        failures.append('a tool call did not start Ectolens either, so the stand-in proves nothing')
    server.stdin.close()
    server.wait(timeout=10)
    bus.terminate()
    bus.wait(timeout=10)
    for f in failures:
        print('FAIL', f)
    if not failures:
        print('PASS starting ectolens-mcp started nothing; its first tool call started Ectolens')
    sys.exit(1 if failures else 0)
finally:
    shutil.rmtree(work, ignore_errors=True)
