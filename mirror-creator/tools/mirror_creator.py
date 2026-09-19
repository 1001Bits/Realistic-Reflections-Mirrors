#!/usr/bin/env python3
"""Run the local Mirror Creator preview: python tools/mirror_creator.py --open."""
from __future__ import annotations

import argparse
import json
import mimetypes
import threading
import webbrowser
from collections import OrderedDict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from mirror_creator_core import (CreatorError, MAX_NIF_BYTES, MAX_PROJECT_BYTES,
                                 load_nif, load_project, save_project, select_surface)
from mirror_creator_export import export_nif
from mirror_creator_areas import toggle_area

ROOT = Path(__file__).resolve().parents[1]
WEB = Path(__file__).resolve().with_name('mirror_creator_web')
SAMPLES = {
    'standing': ('Gothic standing mirror', 'placeable', ROOT/'dist/Data/meshes/mirrors_of_skyrim/mirror01.nif'),
}


class CreatorServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, port=8765):
        super().__init__(('127.0.0.1', port), Handler)
        self.documents = OrderedDict()
        self.lock = threading.RLock()
        self.operation = threading.BoundedSemaphore(1)
        self.origin = f'http://127.0.0.1:{self.server_port}'

    def retain(self, doc):
        with self.lock:
            self.documents[doc.digest] = doc
            self.documents.move_to_end(doc.digest)
            while len(self.documents) > 4 or sum(len(d.data) for d in self.documents.values()) > 128*1024*1024:
                self.documents.popitem(last=False)

    def document(self, key):
        if not isinstance(key, str):
            raise CreatorError('Load a NIF first.')
        with self.lock:
            doc = self.documents.get(key)
            if doc is None:
                raise CreatorError('This model is no longer in the preview cache. Reopen its NIF or saved project.')
            self.documents.move_to_end(key)
            return doc


class Handler(BaseHTTPRequestHandler):
    server_version = 'MirrorCreator/0.5'

    def setup(self):
        super().setup()
        self.connection.settimeout(30)

    def respond(self, status, body, mime='application/json; charset=utf-8'):
        if not isinstance(body, bytes):
            body = json.dumps(body, ensure_ascii=True, allow_nan=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Referrer-Policy', 'no-referrer')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self'; img-src 'self' data: blob:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(body)

    def local_request(self):
        if self.headers.get('Host') != urlsplit(self.server.origin).netloc:
            self.respond(403, dict(error='Use the local Mirror Creator address shown by the launcher.'))
            return False
        origin = self.headers.get('Origin')
        if origin is not None and origin != self.server.origin:
            self.respond(403, dict(error='Only this local preview can access the builder.'))
            return False
        return True

    def do_GET(self):
        if not self.local_request():
            return
        path = urlsplit(self.path).path
        if path == '/api/health':
            self.respond(200, dict(app='Mirror Creator', version='0.5', playableAddonExport=False,
                                   preparedNifExport=True, nifSurfaceSchema=1, equippedHandRuntime=False,
                                   multiAreaSelection=True, ckPartIdentification=True))
            return
        if path == '/api/samples':
            self.respond(200, [dict(id=k, name=v[0], mirrorType=v[1]) for k, v in SAMPLES.items() if v[2].is_file()])
            return
        relative = 'index.html' if path == '/' else path.lstrip('/')
        target = (WEB/relative).resolve()
        if not target.is_relative_to(WEB) or not target.is_file() or target.suffix not in ('.html', '.css', '.js', '.txt', '.json', '.svg'):
            self.respond(404, dict(error='Page not found.'))
            return
        mime = 'text/javascript' if target.suffix == '.js' else mimetypes.guess_type(target.name)[0] or 'application/octet-stream'
        self.respond(200, target.read_bytes(), mime)

    def do_POST(self):
        if not self.local_request():
            return
        if self.headers.get('X-Mirror-Creator') != '1':
            self.respond(403, dict(error='Open the local preview to use this operation.'))
            return
        # Bound concurrent parsing and selection work as well as request sizes.
        if not self.server.operation.acquire(blocking=False):
            self.respond(429, dict(error='The preview is still processing a model. Try again in a moment.'))
            return
        try:
            route = urlsplit(self.path)
            path = route.path
            limit = MAX_PROJECT_BYTES if path == '/api/open-project' else MAX_NIF_BYTES if path == '/api/import' else 8*1024*1024
            try:
                size = int(self.headers.get('Content-Length', '-1'))
            except ValueError:
                size = -1
            if not 0 < size <= limit or self.headers.get('Transfer-Encoding'):
                raise CreatorError('The request is empty or too large for this preview.')
            raw = self.rfile.read(size)
            if len(raw) != size:
                raise CreatorError('The file transfer was incomplete. Please try again.')
            if path == '/api/import':
                name = parse_qs(route.query).get('name', ['mirror.nif'])[0]
                doc = load_nif(raw, name)
                self.server.retain(doc)
                self.respond(200, dict(document=doc.public()))
                return
            if path == '/api/open-project':
                doc, kind, surface = load_project(raw)
                self.server.retain(doc)
                self.respond(200, dict(document=doc.public(), mirrorType=kind, selection=surface))
                return
            request = json.loads(raw)
            if not isinstance(request, dict):
                raise CreatorError('The preview request is invalid.')
            if path == '/api/sample':
                sample = SAMPLES.get(str(request.get('sample')))
                if sample is None or not sample[2].is_file():
                    raise CreatorError('This example model is not installed.')
                doc = load_nif(sample[2].read_bytes(), sample[2].name)
                self.server.retain(doc)
                self.respond(200, dict(document=doc.public(), mirrorType=sample[1]))
                return
            doc = self.server.document(request.get('id'))
            if path == '/api/select':
                selection = select_surface(doc, request.get('block'), seed=request.get('seed'), mode=request.get('mode', 'connected'), faces=request.get('faces'), flip=request.get('flip', False))
                additive = request.get('additive', False)
                if type(additive) is not bool:
                    raise CreatorError('The area selection operation is invalid.')
                if additive:
                    selection = toggle_area(doc, request.get('selection'), selection)
                self.respond(200, selection)
            elif path == '/api/save':
                self.respond(200, save_project(doc, request.get('mirrorType'), request.get('selection')), 'application/zip')
            elif path == '/api/export-nif':
                prepared, _ = export_nif(doc, request.get('mirrorType'), request.get('selection'))
                self.respond(200, prepared, 'application/octet-stream')
            else:
                self.respond(404, dict(error='Operation not found.'))
        except (CreatorError, ValueError, TypeError, UnicodeError) as exc:
            self.respond(400, dict(error=str(exc)))
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            pass
        finally:
            self.server.operation.release()

    def log_message(self, fmt, *args):
        # A detached Windows launcher can close its inherited stdout pipe.
        # Request handling must never depend on that pipe remaining writable.
        # Uploaded filenames and request data are not access-logged either.
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765, help='Local port; 0 chooses an available port.')
    parser.add_argument('--open', action='store_true', help='Open the preview in your default browser.')
    parser.add_argument('--state-file', type=Path, help='Write the local address for a launcher or browser test.')
    args = parser.parse_args()
    with CreatorServer(args.port) as server:
        print(f'Mirror Creator: {server.origin}/', flush=True)
        if args.state_file:
            args.state_file.parent.mkdir(parents=True, exist_ok=True)
            args.state_file.write_text(json.dumps(dict(url=server.origin+'/', port=server.server_port)), encoding='utf-8')
        if args.open:
            webbrowser.open(server.origin+'/')
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
