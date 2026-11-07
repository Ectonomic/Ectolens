# A made-up AI agent for the off-screen agent runs (run.sh SCENARIO=agent and agentoff): it starts
# ectolens-mcp as an agent would, speaks MCP to it on its stdin and stdout, and writes every
# answer to OUT/agent-client.json for check.py. inner.sh plays the person in the overlay (Send,
# Don't Send, Enter, Record) while this waits for the answers, as an agent waits.
# usage: python3 -I agent-client.py ECTOLENS_MCP OUT off|on|one
import json, os, subprocess, sys, time

mcp, out, mode = sys.argv[1], sys.argv[2], sys.argv[3]
proc = subprocess.Popen([mcp], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=open(os.path.join(out, 'ectolens-mcp.log'), 'w'), text=True, bufsize=1)
results = {}
next_id = [0]
MODERN = {'io.modelcontextprotocol/protocolVersion': '2026-07-28', 'io.modelcontextprotocol/clientInfo': {'name': 'Test Agent', 'version': '1'},
          'io.modelcontextprotocol/clientCapabilities': {}}


def send(method, params=None, modern=False):
    next_id[0] += 1
    params = dict(params or {})
    if modern:
        params['_meta'] = MODERN
    proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': next_id[0], 'method': method, 'params': params}) + '\n')
    proc.stdin.flush()
    line = proc.stdout.readline()
    return json.loads(line) if line else {'error': 'no answer'}


def tool(name, arguments=None, modern=False):
    answer = send('tools/call', {'name': name, 'arguments': arguments or {}}, modern)
    result = answer.get('result') or {}
    # the pictures themselves are big: keep their size, not their bytes
    for c in result.get('content', []):
        if c.get('type') == 'image':
            path = os.path.join(out, f'agent-image-{next_id[0]}.png')
            import base64
            open(path, 'wb').write(base64.b64decode(c['data']))
            c['data'] = f'<{len(c["data"])} base64 characters, written to {path}>'
            c['file'] = path
    return answer


def save():
    json.dump(results, open(os.path.join(out, 'agent-client.json'), 'w'), indent=1)


# AGENT_TITLE: the name the overlay shows (the README's picture uses Claude Code's)
info = {'name': 'test-agent', 'title': os.environ.get('AGENT_TITLE', 'Test Agent'), 'version': '1'}
results['initialize'] = send('initialize', {'protocolVersion': '2025-06-18', 'clientInfo': info, 'capabilities': {}})
proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
proc.stdin.flush()
results['tools'] = send('tools/list')
results['windows'] = tool('list_windows')
save()
if mode == 'one':
    # the README's picture of the overlay asking: one request, which the scene refuses with Esc
    results['capture_window'] = tool('capture', {'target': 'window', 'window': 'konsole', 'reason': 'See why the build failed', 'return': 'image'})
elif mode == 'off':
    results['capture_off'] = tool('capture', {'target': 'screen', 'reason': 'A test while agents are off'})
else:
    # the person sends it (a click on Send)
    results['capture_window'] = tool('capture', {'target': 'window', 'window': 'konsole', 'reason': 'Read the terminal', 'return': 'image_and_text'})
    save()
    # the person refuses (a click on Don't Send)
    results['capture_refused'] = tool('capture', {'target': 'area', 'x': 420, 'y': 220, 'width': 300, 'height': 160, 'reason': 'A part of the terminal'})
    save()
    # the person presses Enter; words only
    results['capture_text'] = tool('capture', {'target': 'screen', 'reason': 'The words on screen', 'return': 'text'})
    save()
    # the modern era: discovery, then a call that names its version
    results['discover'] = send('server/discover', {}, modern=True)
    results['windows_modern'] = tool('list_windows', modern=True)
    save()
    # a recording the person starts (a click on Record), stopped by the agent
    results['record_start'] = tool('record_start', {'target': 'window', 'window': 'konsole', 'reason': 'Record the terminal'})
    time.sleep(2.5)
    results['record_stop'] = tool('record_stop')
    save()
    # the person moves the frame after Adjust First, from the terminal's last lines up over its
    # email address and key, and sends: Hide Private Info reads the new part before it goes
    results['capture_adjusted'] = tool('capture', {'target': 'area', 'x': 400, 'y': 380, 'width': 420, 'height': 70, 'reason': 'The listing', 'return': 'image_and_text'})
    save()
    # the terminal's email and key lines: the person unchecks Blur private information and sends,
    # so the agent gets them as they are
    results['capture_unblurred'] = tool('capture', {'target': 'area', 'x': 400, 'y': 280, 'width': 420, 'height': 70, 'reason': 'The account lines', 'return': 'image_and_text'})
    save()
    # the same lines again: the box starts checked for this request, and unchecked then checked
    # again, the blurs are back when it goes
    results['capture_reblurred'] = tool('capture', {'target': 'area', 'x': 400, 'y': 280, 'width': 420, 'height': 70, 'reason': 'The account lines again', 'return': 'image_and_text'})
save()
proc.stdin.close()
proc.wait(timeout=10)
open(os.path.join(out, 'agent-client.done'), 'w').close()
