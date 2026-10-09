#!/usr/bin/env python3
"""Authenticated, loopback-only browser terminal for one trusted local user.

Uses only Python's standard library. It executes the real C shell in a PTY.
The shell has the server user's permissions: this is not a sandbox.
"""
import argparse
import codecs
import fcntl
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import json
import mimetypes
import os
from pathlib import Path
import pty
import secrets
import signal
import struct
import subprocess
import sys
import termios
import threading
import time
from urllib.parse import parse_qs, unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
STATIC = ROOT / 'web/static'
MAX_OUTPUT = 1_000_000

class TerminalSession:
    def __init__(self, binary, workspace):
        self.binary, self.workspace = str(binary), str(workspace)
        self.lock = threading.RLock()
        self.condition = threading.Condition(self.lock)
        self.writer = threading.Lock()
        self.process = None
        self.master = None
        self.output = ''
        self.offset = 0
        self.last_input = 0
        self.reader = None
        self.session_id = 0

    def start(self):
        with self.lock:
            if self.process and self.process.poll() is None:
                return self.info()
            if self.reader and self.reader.is_alive():
                raise ValueError('Previous session is closing; try again.')
            if self.master is not None:
                os.close(self.master)
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 28, 100, 0, 0))
            env = dict(os.environ, TERM='xterm-256color', COLORTERM='truecolor')
            # Do not pass server credentials or unrelated deployment secrets to commands.
            for key in list(env):
                if any(part in key.upper() for part in ('TOKEN', 'SECRET', 'PASSWORD', 'API_KEY')):
                    env.pop(key, None)
            try:
                self.process = subprocess.Popen(
                    [sys.executable, str(ROOT/'web/pty_child.py'), self.binary],
                    stdin=slave, stdout=slave, stderr=slave, cwd=self.workspace,
                    env=env, start_new_session=True, close_fds=True)
            except Exception:
                os.close(master)
                raise
            finally:
                os.close(slave)
            self.master = master
            self.output, self.offset = '', 0
            self.last_input = time.monotonic()
            self.session_id += 1
            self.reader = threading.Thread(target=self._read, args=(master,), daemon=True)
            self.reader.start()
            return self.info()

    def _read(self, master):
        decoder = codecs.getincrementaldecoder('utf-8')('replace')
        while True:
            try:
                data = os.read(master, 16384)
                if not data: break
            except OSError: break
            text = decoder.decode(data)
            with self.condition:
                self.output += text
                if len(self.output) > MAX_OUTPUT:
                    trim = len(self.output)-MAX_OUTPUT
                    self.output = self.output[trim:]
                    self.offset += trim
                self.condition.notify_all()
        with self.condition:
            self.condition.notify_all()

    def info(self):
        with self.lock:
            return {'running': bool(self.process and self.process.poll() is None),
                    'pid': self.process.pid if self.process else None,
                    'workspace': self.workspace, 'session': self.session_id}

    def read(self, cursor, session):
        with self.condition:
            if session != self.session_id: cursor = 0
            end = self.offset+len(self.output)
            if cursor >= end and self.info()['running']:
                self.condition.wait(timeout=1)
            start = max(0, cursor-self.offset)
            return {**self.info(), 'output': self.output[start:],
                    'cursor': self.offset+len(self.output), 'truncated': cursor < self.offset}

    def write(self, data):
        if not isinstance(data, str) or len(data.encode()) > 8192:
            raise ValueError('Input must be a string of at most 8192 bytes.')
        with self.writer, self.lock:
            if not self.info()['running']: raise ValueError('Start a session first.')
            raw = data.encode()
            # Split large pastes: the PTY input queue may fill.
            if raw: os.write(self.master, raw)
            self.last_input = time.monotonic()

    def resize(self, rows, cols):
        if not isinstance(rows, int) or not isinstance(cols, int) or not (2 <= rows <= 200 and 10 <= cols <= 400):
            raise ValueError('Invalid terminal size.')
        with self.lock:
            if self.master is not None:
                fcntl.ioctl(self.master, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))

    def close(self):
        with self.lock:
            process = self.process
            if not process: return
            if process.poll() is None:
                try: os.write(self.master, b'\x03')
                except OSError: pass
        if process.poll() is None:
            time.sleep(0.1)
            try: os.write(self.master, b'exit\n')
            except OSError: pass
            try: process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                # Find descendants before stopping the session leader.
                listing = subprocess.check_output(['ps','-axo','pid=,ppid='], text=True)
                pairs = [tuple(map(int, line.split())) for line in listing.splitlines() if len(line.split()) == 2]
                descendants = {process.pid}
                for _ in range(len(pairs)):
                    added = {pid for pid, parent in pairs if parent in descendants}
                    if added <= descendants: break
                    descendants |= added
                for pid in sorted(descendants, reverse=True):
                    try: os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError: pass
                process.wait(timeout=2)
        if self.reader: self.reader.join(timeout=1)
        with self.lock:
            if self.master is not None:
                os.close(self.master)
                self.master = None
            self.condition.notify_all()

class TerminalServer(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, address, token, session, origins):
        self.token, self.session, self.origins = token, session, set(origins)
        super().__init__(address, Handler)
        self.origins.add(f'http://127.0.0.1:{self.server_port}')
        self.origins.add(f'http://localhost:{self.server_port}')
        self.hosts = {f'127.0.0.1:{self.server_port}', f'localhost:{self.server_port}'}

class Handler(BaseHTTPRequestHandler):
    server_version = 'ShellForge/3.0'
    def log_message(self, fmt, *args):
        # Avoid logging authentication data and user commands.
        pass

    def send_headers(self, status=200, content_type='application/json', length=None):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Referrer-Policy', 'no-referrer')
        self.send_header('X-Frame-Options', 'DENY')
        origin = self.headers.get('Origin')
        if origin in self.server.origins:
            self.send_header('Access-Control-Allow-Origin', origin)
            self.send_header('Vary', 'Origin')
        if length is not None: self.send_header('Content-Length', str(length))
        self.end_headers()

    def reply(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_headers(status, length=len(body))
        try: self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError): pass

    def authorized(self):
        if self.headers.get('Host') not in self.server.hosts:
            self.reply({'error':'Unrecognized host.'}, 403); return False
        origin = self.headers.get('Origin')
        if origin and origin not in self.server.origins:
            self.reply({'error':'Origin is not allowed.'}, 403); return False
        supplied = self.headers.get('Authorization', '')
        if not hmac.compare_digest(supplied.encode(), ('Bearer '+self.server.token).encode()):
            self.reply({'error':'Enter the access token printed by the local server.'}, 401); return False
        return True

    def do_OPTIONS(self):
        origin = self.headers.get('Origin')
        if self.headers.get('Host') not in self.server.hosts or origin not in self.server.origins:
            self.reply({'error':'Origin is not allowed.'}, 403); return
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', origin)
        self.send_header('Access-Control-Allow-Methods','GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers','Authorization, Content-Type')
        self.send_header('Access-Control-Allow-Private-Network','true')
        self.send_header('Vary','Origin')
        self.end_headers()

    def do_GET(self):
        parsed = urlsplit(self.path)
        if parsed.path.startswith('/api/'):
            if not self.authorized(): return
            if parsed.path == '/api/status': self.reply(self.server.session.info())
            elif parsed.path == '/api/output':
                try:
                    query = parse_qs(parsed.query)
                    cursor = max(0, int(query.get('cursor',['0'])[0]))
                    session = int(query.get('session',['0'])[0])
                    self.reply(self.server.session.read(cursor, session))
                except ValueError: self.reply({'error':'Invalid output cursor.'}, 400)
            else: self.reply({'error':'Not found.'},404)
            return
        path = (STATIC / unquote(parsed.path).lstrip('/')).resolve()
        if parsed.path == '/': path = STATIC/'index.html'
        if not path.is_relative_to(STATIC) or not path.is_file():
            self.reply({'error':'Not found.'},404); return
        body = path.read_bytes()
        mime = mimetypes.guess_type(path)[0] or 'application/octet-stream'
        self.send_headers(content_type=mime, length=len(body))
        try: self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError): pass

    def do_POST(self):
        if not self.authorized(): return
        try:
            length = int(self.headers.get('Content-Length','0'))
            if not 0 <= length <= 40000: raise ValueError('Request is too large.')
            self.connection.settimeout(5)
            data = json.loads(self.rfile.read(length) or b'{}')
            if not isinstance(data, dict): raise ValueError('Expected a JSON object.')
            route = urlsplit(self.path).path
            if route == '/api/start': self.reply(self.server.session.start())
            elif route == '/api/input':
                self.server.session.write(data.get('data','')); self.reply({'ok':True})
            elif route == '/api/resize':
                self.server.session.resize(data.get('rows'), data.get('cols')); self.reply({'ok':True})
            elif route == '/api/stop':
                self.server.session.close(); self.reply({'ok':True})
            else: self.reply({'error':'Not found.'},404)
        except (ValueError, OSError) as error:
            self.reply({'error':str(error)},400)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--host',choices=['127.0.0.1','0.0.0.0'],default='127.0.0.1',help='Use 0.0.0.0 only inside a private container.')
    parser.add_argument('--workspace',type=Path,default=ROOT/'web/workspace')
    parser.add_argument('--allow-origin',action='append',default=[],help='Exact trusted hosted frontend origin.')
    args = parser.parse_args()
    binary = ROOT/'bin/shellforge'
    if not binary.is_file(): parser.error('Build ShellForge first with make.')
    args.workspace.mkdir(parents=True,exist_ok=True)
    token = secrets.token_urlsafe(32)
    session = TerminalSession(binary, args.workspace.resolve())
    server = TerminalServer((args.host,args.port),token,session,args.allow_origin)
    def idle_reaper():
        while True:
            time.sleep(5)
            if session.info()['running'] and time.monotonic()-session.last_input > 1800:
                session.close()
    threading.Thread(target=idle_reaper,daemon=True).start()
    print(f'ShellForge terminal: http://127.0.0.1:{server.server_port}/#token={token}',flush=True)
    print(f'Access token: {token}',flush=True)
    print('Local access only. Commands run with your account permissions. Idle sessions close after 30 minutes.',flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally:
        session.close()
        server.server_close()

if __name__ == '__main__': main()
