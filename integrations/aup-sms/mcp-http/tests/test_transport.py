"""Real native binary HTTP qualification; Python standard library only.

python test_transport.py /absolute/path/codebase-memory-mcp[.exe]
Artifacts and complete unittest diagnostics stay under build/cr27-test-*.
"""
import concurrent.futures
import hashlib
import hmac
import http.client
import json
import os
from pathlib import Path
import secrets
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())
ROOT = Path(__file__).resolve().parents[4]
PROTO = "2025-06-18"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fixture(path, symbol):
    with sqlite3.connect(path) as db:
        db.executescript("""
        CREATE TABLE projects(name TEXT PRIMARY KEY,indexed_at TEXT NOT NULL,root_path TEXT NOT NULL);
        CREATE TABLE nodes(id INTEGER PRIMARY KEY,project TEXT,label TEXT,name TEXT,
          qualified_name TEXT,file_path TEXT,start_line INTEGER,end_line INTEGER,properties TEXT);
        CREATE TABLE edges(id INTEGER PRIMARY KEY,project TEXT,source_id INTEGER,target_id INTEGER,type TEXT,properties TEXT);
        CREATE TABLE store_meta(k TEXT PRIMARY KEY,v TEXT NOT NULL);
        INSERT INTO store_meta VALUES('db_uid','0123456789abcdef'),('mutation_gen','1');
        INSERT INTO projects VALUES('private-native','2026-10-18','/private/source/root');
        """)
        db.execute("INSERT INTO nodes VALUES(1,?,?,?,?,?,?,?,?)",
                   ("private-native", "Function", symbol, symbol, "src/example.c", 1, 3, "{}"))
    return dict(repository_index_id=symbol + "-index", cbm_project="private-native",
                generation="u0123456789abcdefg1", artifact_sha256=sha(path.read_bytes()), path=str(path))


class Server:
    def __init__(self, root, project="project-a", indexes=None):
        self.root = root
        root.mkdir(parents=True, exist_ok=True)
        os.chmod(root, 0o700)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]
        self.project = project
        self.base = f"/integrations/aup-sms/v1/projects/{project}"
        self.data = f"asms1.tenant-a.{project}.data." + secrets.token_hex(32)
        self.control = f"asms1.tenant-a.{project}.control." + secrets.token_hex(32)
        self.key = secrets.token_hex(32)
        self.config = dict(tenant_id="tenant-a", project_scope=project, port=self.port,
                           data_token_sha256=sha(self.data.encode()),
                           control_token_sha256=sha(self.control.encode()),
                           attestation_key=self.key, runtime_dir=str(root), indexes=indexes or [])
        self.path = root / "config.json"
        self.write_config()
        self.proc = None
        self.log = None

    def write_config(self):
        temp = self.path.with_suffix(".tmp")
        temp.write_text(json.dumps(self.config), encoding="utf-8")
        os.chmod(temp, 0o600)
        temp.replace(self.path)

    def start(self):
        self.log = open(self.root / "process.log", "ab")
        options = {}
        if os.name != "nt":
            import resource
            options["preexec_fn"] = lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        self.proc = subprocess.Popen([BINARY, "asms-mcp-http", str(self.path)],
                                     stdout=self.log, stderr=self.log, **options)
        last_response = None
        for _ in range(150):
            if self.proc.poll() is not None:
                raise RuntimeError((self.root / "process.log").read_text())
            try:
                last_response = self.request("GET", self.base + "/health", token=self.control)
                if last_response[0] == 200:
                    return
            except OSError:
                pass
            time.sleep(.05)
        raise RuntimeError("Server readiness timeout: " + repr(last_response) + "\n" + (self.root / "process.log").read_text())

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait(timeout=10)
        if self.log:
            self.log.close()

    def request(self, method, path, body=None, token=None, extra=None):
        raw = body if isinstance(body, bytes) else json.dumps(body).encode() if body is not None else b""
        headers = {"Authorization": "Bearer " + (token or self.data),
                   "Content-Type": "application/json", "Accept": "application/json, text/event-stream",
                   "MCP-Protocol-Version": PROTO}
        headers.update(extra or {})
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20)
        try:
            conn.request(method, path, raw, headers)
            response = conn.getresponse()
            data = response.read()
            return response.status, json.loads(data) if data else None, dict(response.getheaders())
        finally:
            conn.close()

    def rpc(self, method, params=None, **kwargs):
        return self.request("POST", self.base + "/mcp",
                            dict(jsonrpc="2.0", id="test-id", method=method, params=params or {}), **kwargs)

    def attestation(self, index, **changes):
        body = {k: v for k, v in index.items() if k != "path"}
        body.update(binding_version=1, tenant_id="tenant-a", project_scope=self.project,
                    state="READY", activated_at=int(time.time()) - 5, expires_at=int(time.time()) + 600)
        body.update(changes)
        return body

    def mutate(self, source, body=None, revision=0, key=None, method="PUT", **kwargs):
        path = self.base + "/sources/" + source
        raw = json.dumps(body).encode() if body is not None else b""
        key = key or secrets.token_hex(12)
        signed = f"{method}\n{path}\n{key}\n{revision}\n".encode() + raw
        headers = {"Idempotency-Key": key, "If-Match": str(revision),
                   "X-ASMS-Signature": hmac.new(self.key.encode(), signed, "sha256").hexdigest()}
        headers.update(kwargs.pop("extra", {}))
        return self.request(method, path, raw, token=kwargs.pop("token", self.control), extra=headers, **kwargs)


class Transport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_root = Path(os.environ.get("CBM_HTTP_TEST_ROOT", ROOT / "build"))
        test_root.mkdir(parents=True, exist_ok=True)
        cls.work = Path(tempfile.mkdtemp(prefix="cr27-test-", dir=test_root))
        cls.one = fixture(cls.work / "one.db", "alpha-only")
        cls.two = fixture(cls.work / "two.db", "beta-only")

    def setUp(self):
        self.s = Server(self.work / secrets.token_hex(6), indexes=[self.one, self.two])
        try:
            self.s.start()
        except BaseException:
            self.s.stop()
            raise

    def tearDown(self):
        self.s.stop()

    def activate(self, source="source-a", index=None, **changes):
        body = self.s.attestation(index or self.one, **changes)
        response = self.s.mutate(source, body)
        self.assertEqual(response[0], 200, response)
        return body

    def call(self, source="source-a", **args):
        args.update(source_id=source)
        return self.s.rpc("tools/call", dict(name="query_graph", arguments=dict(query="MATCH (n:Function) RETURN n.name", **args)))

    def test_initialize_and_catalog_native_schemas(self):
        status, result, headers = self.s.rpc("initialize", dict(protocolVersion=PROTO, capabilities={}, clientInfo=dict(name="qualification", version="1")))
        self.assertEqual(status, 200)
        self.assertEqual(result["result"]["protocolVersion"], PROTO)
        self.assertNotIn("Mcp-Session-Id", headers)
        tools = self.s.rpc("tools/list")[1]["result"]["tools"]
        self.assertEqual({t["name"] for t in tools}, {"query_graph", "search_graph", "get_graph_schema", "trace_path", "get_file_outline", "get_architecture"})
        for tool in tools:
            self.assertIn("source_id", tool["inputSchema"]["required"])
            self.assertNotIn("project", tool["inputSchema"]["properties"])

    def test_standard_mcp_sdk(self):
        self.activate()
        env = dict(os.environ, CBM_TEST_MCP_URL=f"http://127.0.0.1:{self.s.port}{self.s.base}/mcp",
                   CBM_TEST_DATA_TOKEN=self.s.data)
        result = subprocess.run(["node", str(Path(__file__).with_name("standard-client.mjs"))],
                                env=env, capture_output=True, text=True, timeout=30, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_stdio_catalog_unchanged(self):
        cache = self.s.root / "stdio-cache"
        cache.mkdir(mode=0o700)
        env = dict(os.environ, CBM_CACHE_DIR=str(cache))
        messages = [dict(jsonrpc="2.0", id=1, method="initialize", params=dict(protocolVersion=PROTO, capabilities={}, clientInfo=dict(name="stdio-regression", version="1"))),
                    dict(jsonrpc="2.0", id=2, method="tools/list")]
        try:
            process = subprocess.run([BINARY], input="".join(json.dumps(m) + "\n" for m in messages),
                                     capture_output=True, text=True, env=env, timeout=45)
            self.assertEqual(process.returncode, 0, process.stderr)
            responses = [json.loads(line) for line in process.stdout.splitlines() if line.startswith("{")]
            catalog = next(r["result"]["tools"] for r in responses if r.get("id") == 2)
            self.assertGreater(len(catalog), 6)
            self.assertIn("index_repository", {t["name"] for t in catalog})
            self.assertIn("project", next(t for t in catalog if t["name"] == "query_graph")["inputSchema"]["properties"])
        finally:
            subprocess.run([BINARY, "daemon", "stop"], env=env, capture_output=True, timeout=30)

    def test_supervisor_deadline_cause_and_cleanup(self):
        self.activate()
        self.s.config["timeout_ms"] = 1; self.s.write_config()
        result = self.call()[1]
        self.assertIn(result["error"]["data"]["code"], {"WORKER_TIMEOUT", "WORKER_FAILED"})
        self.assertIn("tree_quiesced", result["error"]["data"]["causes"][0])
        self.assertFalse(list(self.s.root.glob("*.in")))
        self.assertFalse(list(self.s.root.glob("*.out")))
        self.assertFalse(list(self.s.root.glob("*.err")))

    def test_native_worker_crash_keeps_classification_and_correlation(self):
        path = self.work / (secrets.token_hex(6) + ".db")
        index = fixture(path, "crash-fixture")
        with sqlite3.connect(path) as db:
            # Windows accounts user CPU in coarse clock ticks. Keep verification
            # alive long enough to observe execution after the suspended phase.
            padding_mb = 256 if os.name == "nt" else 32
            db.execute("UPDATE nodes SET properties=?", (json.dumps({"padding": "0" * (padding_mb * 1024 * 1024)}),))
        index["artifact_sha256"] = sha(path.read_bytes())
        self.s.config["indexes"].append(index); self.s.write_config()
        self.activate(index=index)
        def crash_owned_worker():
            until = time.monotonic() + 10
            while time.monotonic() < until:
                if os.name != "nt":
                    children = Path(f"/proc/{self.s.proc.pid}/task/{self.s.proc.pid}/children").read_text().split()
                    if children:
                        os.kill(int(children[0]), signal.SIGABRT)
                        return
                else:
                    import ctypes
                    from ctypes import wintypes as wt
                    class Entry(ctypes.Structure):
                        _fields_ = [("size", wt.DWORD), ("usage", wt.DWORD), ("pid", wt.DWORD),
                                    ("heap", ctypes.c_size_t), ("module", wt.DWORD), ("threads", wt.DWORD),
                                    ("parent", wt.DWORD), ("priority", wt.LONG), ("flags", wt.DWORD),
                                    ("name", wt.WCHAR * 260)]
                    api = ctypes.WinDLL("kernel32", use_last_error=True)
                    api.CreateToolhelp32Snapshot.restype = wt.HANDLE
                    api.OpenProcess.restype = wt.HANDLE
                    api.CloseHandle.argtypes = [wt.HANDLE]
                    api.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]
                    api.GetProcessTimes.argtypes = [wt.HANDLE] + [ctypes.POINTER(wt.FILETIME)] * 4
                    api.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
                    snap = api.CreateToolhelp32Snapshot(2, 0)
                    entry = Entry(); entry.size = ctypes.sizeof(entry)
                    api.Process32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(Entry)]
                    api.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(Entry)]
                    try:
                        valid = api.Process32FirstW(snap, ctypes.byref(entry))
                        while valid:
                            if entry.parent == self.s.proc.pid and entry.name.lower() == Path(BINARY).name.lower():
                                handle = api.OpenProcess(0x1001, False, entry.pid)
                                if handle:
                                    try:
                                        created, exited, kernel, user = (wt.FILETIME() for _ in range(4))
                                        ready = api.GetProcessTimes(handle, ctypes.byref(created),
                                                                   ctypes.byref(exited), ctypes.byref(kernel),
                                                                   ctypes.byref(user))
                                        exit_code = wt.DWORD()
                                        alive = api.GetExitCodeProcess(handle, ctypes.byref(exit_code)) and exit_code.value == 259
                                        # CBM creates workers suspended, then assigns the Job and
                                        # resumes them. Kill only a worker that has executed user code.
                                        if alive and ready and (user.dwLowDateTime or user.dwHighDateTime):
                                            if api.TerminateProcess(handle, 0xC0000005):
                                                return
                                            # A worker can finish between observation and kill.
                                            failure = ctypes.get_last_error()
                                            if api.GetExitCodeProcess(handle, ctypes.byref(exit_code)) and exit_code.value == 259:
                                                raise AssertionError(f"TerminateProcess failed: Win32 {failure}")
                                    finally:
                                        api.CloseHandle(handle)
                            valid = api.Process32NextW(snap, ctypes.byref(entry))
                    finally:
                        api.CloseHandle(snap)
                time.sleep(.001)
            raise AssertionError("Owned native worker was not observed")
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            pending = pool.submit(crash_owned_worker)
            result = self.call()[1]
            pending.result()
        self.assertEqual(result["error"]["data"]["code"], "WORKER_FAILED", result)
        self.assertEqual(result["error"]["data"]["causes"][0]["outcome"], 2, result)
        self.assertTrue(result["error"]["data"]["correlation_id"])

    def test_cross_source_native_cursor_rejected(self):
        self.activate()
        self.activate("source-b", self.two)
        self.assertEqual(self.call("source-b", cursor="native-account-cache-cursor")[0], 400)

    def test_multi_project_artifact_rejected(self):
        path = self.work / (secrets.token_hex(6) + ".db")
        index = fixture(path, "mixed-project")
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO projects VALUES('foreign-project','2026-10-18','/foreign')")
        index["artifact_sha256"] = sha(path.read_bytes())
        self.s.config["indexes"].append(index); self.s.write_config()
        self.assertEqual(self.s.mutate("source-a", self.s.attestation(index))[0], 409)

    def test_notifications_get_and_batch(self):
        status, data, _ = self.s.request("POST", self.s.base + "/mcp", dict(jsonrpc="2.0", method="notifications/initialized"))
        self.assertEqual((status, data), (202, None))
        self.assertEqual(self.s.request("GET", self.s.base + "/mcp")[0], 405)
        self.assertEqual(self.s.request("POST", self.s.base + "/mcp", [dict(jsonrpc="2.0", id=1, method="ping")])[0], 400)

    def test_two_sources_exact_artifacts_and_private_redaction(self):
        self.activate()
        self.activate("source-b", self.two)
        for source, yes, no in [("source-a", "alpha-only", "beta-only"), ("source-b", "beta-only", "alpha-only")]:
            status, result, _ = self.call(source)
            self.assertEqual(status, 200, result)
            self.assertNotIn("error", result, result)
            self.assertFalse(result["result"].get("isError"), result)
            raw = json.dumps(result)
            self.assertIn(yes, raw)
            self.assertNotIn(no, raw)
            self.assertNotIn("private-native", raw)
            self.assertNotIn(self.one["path"], raw)
            self.assertNotIn(self.one["artifact_sha256"], raw)

    def test_missing_source_and_selectors_are_rejected(self):
        for args in [{}, {"source_id": "source-a", "project": "private-native"},
                     {"source_id": "source-a", "repo_path": "/etc"},
                     {"source_id": "source-a", "tenant_id": "other"}]:
            result = self.s.rpc("tools/call", dict(name="get_graph_schema", arguments=args))
            self.assertEqual(result[0], 400, result)
        self.assertEqual(self.call()[1]["error"]["data"]["code"], "SOURCE_NOT_ACTIVE")

    def test_native_file_outline_exact_source_relative_path(self):
        self.activate()
        self.activate("source-b", self.two)
        for source, yes, no in [("source-a", "alpha-only", "beta-only"), ("source-b", "beta-only", "alpha-only")]:
            result = self.s.rpc("tools/call", dict(name="get_file_outline", arguments=dict(source_id=source, file_path="src/example.c", format="json")))
            self.assertEqual(result[0], 200, result)
            self.assertFalse(result[1]["result"].get("isError"), result)
            self.assertIn(yes, json.dumps(result[1]))
            self.assertNotIn(no, json.dumps(result[1]))
        for path in ["/etc/passwd", "C:/private/source", "../foreign", "src/../../foreign", "src/..", "\\\\server\\share"]:
            self.assertEqual(self.s.rpc("tools/call", dict(name="get_file_outline", arguments=dict(source_id="source-a", file_path=path)))[0], 400)
        self.s.config["max_results"] = 1000; self.s.write_config()
        result = self.s.rpc("tools/call", dict(name="get_file_outline", arguments=dict(source_id="source-a", file_path="src/example.c")))
        self.assertFalse(result[1]["result"].get("isError"), result)

    def test_native_architecture_scoped_synchronous_profile(self):
        self.activate()
        result = self.s.rpc("tools/call", dict(name="get_architecture", arguments=dict(source_id="source-a", aspects=["structure"], format="json")))
        self.assertEqual(result[0], 200, result)
        self.assertFalse(result[1]["result"].get("isError"), result)
        self.assertNotIn("private-native", json.dumps(result[1]))
        for options in [dict(path="/private/source"), dict(path="../foreign"), dict(aspects=["system_structure"]), dict(include_behavior_evidence=True)]:
            self.assertEqual(self.s.rpc("tools/call", dict(name="get_architecture", arguments=dict(source_id="source-a", **options)))[0], 400)

    def test_project_and_plane_credentials_isolated(self):
        other = Server(self.work / secrets.token_hex(6), "project-b", indexes=[self.two])
        other.start()
        try:
            self.assertEqual(other.rpc("ping", token=self.s.data)[0], 401)
            self.assertEqual(self.s.rpc("ping", token=self.s.control)[0], 401)
            self.assertEqual(self.s.request("GET", self.s.base + "/health")[0], 401)
            self.assertNotEqual(other.port, 9749)
            self.assertEqual(self.s.request("POST", other.base + "/mcp", {})[0], 401)
        finally:
            other.stop()

    def test_revocation_and_restart_tombstone(self):
        body = self.activate()
        status, result, _ = self.s.mutate("source-a", revision=1, method="DELETE")
        self.assertEqual(status, 200, result)
        self.assertEqual(result["details"]["state"], "REVOKED")
        self.assertEqual(self.call()[1]["error"]["data"]["code"], "SOURCE_NOT_ACTIVE")
        self.s.stop(); self.s.start()
        self.assertEqual(self.call()[1]["error"]["data"]["code"], "SOURCE_NOT_ACTIVE")
        self.assertEqual(self.s.mutate("source-a", body, revision=1)[0], 412)
        self.assertEqual(self.s.mutate("source-a", body, revision=2)[0], 200)

    def test_cas_concurrency_one_winner(self):
        body = self.activate()
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            results = list(pool.map(lambda _: self.s.mutate("source-a", body, revision=1)[0], range(2)))
        self.assertEqual(sorted(results), [200, 412])

    def test_idempotency_full_bytes_and_signature(self):
        body = self.s.attestation(self.one)
        first = self.s.mutate("source-a", body, key="same-key")
        self.assertEqual(first[0], 200, first)
        self.assertEqual(self.s.mutate("source-a", body, key="same-key")[:2], first[:2])
        changed = dict(body, expires_at=body["expires_at"] + 1)
        self.assertEqual(self.s.mutate("source-a", changed, key="same-key")[0], 409)
        self.assertEqual(self.s.mutate("source-x", body, extra={"X-ASMS-Signature": "0" * 64})[0], 400)

    def test_not_ready_cross_scope_generation_checksum(self):
        for change in [dict(state="INSTALLING"), dict(project_scope="project-b"), dict(tenant_id="tenant-b"),
                       dict(generation="wrong"), dict(artifact_sha256="0" * 64), dict(expires_at=int(time.time()) - 1),
                       dict(binding_version=4294967297), dict(debug_token="must-not-persist")]:
            response = self.s.mutate("source-a", self.s.attestation(self.one, **change))
            self.assertEqual(response[0], 409, response)

    def artifact_with_projects(self, names=(), generation=None, symbol="shadow-test"):
        path = self.work / (secrets.token_hex(6) + ".db")
        index = fixture(path, symbol)
        with sqlite3.connect(path) as db:
            for name in names:
                db.execute("INSERT INTO projects VALUES(?,?,?)", (name, "2026-10-22", "/private/root"))
        index["artifact_sha256"] = sha(path.read_bytes())
        if generation is not None:
            index["generation"] = generation
        self.s.config["indexes"] = [index]
        self.s.write_config()
        return index

    def assert_identity_failure(self, response, stage):
        self.assertEqual(response[0], 409, response)
        self.assertEqual(response[1]["contract"], "asms-cbm-http/v1", response)
        self.assertEqual(response[1]["type"], "error", response)
        self.assertEqual(response[1]["code"], "ATTESTATION_UNVERIFIABLE", response)
        self.assertEqual(response[1]["message"],
                         "READY attestation, validity or exact local artifact verification failed", response)
        cause = response[1]["causes"][0]["result"]["structuredContent"]
        self.assertEqual(cause["code"], "ARTIFACT_IDENTITY_OR_INTEGRITY_FAILED", response)
        self.assertEqual(cause["verification_stage"], stage, response)
        self.assertIn("message", cause, response)
        self.assertIn("errno", cause, response)
        self.assertIn("os_message", cause, response)
        diagnostic = json.dumps(response[1])
        for field in ["ARTIFACT_IDENTITY_OR_INTEGRITY_FAILED", "sqlite_code", "sqlite_message",
                      "verification_stage", stage]:
            self.assertIn(field, diagnostic, response)
        self.assertTrue(response[1]["request_id"], response)
        self.assertTrue(response[1]["correlation_id"], response)
        self.assertTrue(response[1]["causes"], response)
        self.assertEqual(response[2]["X-Request-Id"], response[1]["request_id"], response)

    def test_exact_coverage_shadow_attests_and_tools_stay_project_scoped(self):
        index = self.artifact_with_projects(["private-native::missed"])
        path = Path(index["path"])
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO nodes VALUES(2,?,?,?,?,?,?,?,?)",
                       ("private-native::missed", "Function", "shadow-only", "shadow-only", "missed.c", 1, 1, "{}"))
        index["artifact_sha256"] = sha(path.read_bytes())
        self.s.config["indexes"] = [index]; self.s.write_config()
        self.activate(index=index)
        result = self.call()[1]
        self.assertNotIn("error", result, result)
        self.assertIn("shadow-test", json.dumps(result))
        self.assertNotIn("shadow-only", json.dumps(result))
        self.assertEqual(index["artifact_sha256"], sha(path.read_bytes()))

    def test_unrelated_projects_and_shadows_are_rejected(self):
        for names in [["another-project"], ["another-project::missed"],
                      ["private-native::other"], ["private-native::missed::missed"],
                      ["private-native::missed", "another-project"]]:
            with self.subTest(projects=names):
                index = self.artifact_with_projects(names)
                self.assert_identity_failure(self.s.mutate("source-a", self.s.attestation(index)),
                                             "project_identity")

    def test_two_sources_with_coverage_shadows_remain_independent(self):
        first = self.artifact_with_projects(["private-native::missed"], symbol="first-only")
        second = self.artifact_with_projects(["private-native::missed"], symbol="second-only")
        first["repository_index_id"] = "19"
        second["repository_index_id"] = "20"
        self.s.config["indexes"] = [first, second]; self.s.write_config()
        self.activate("7", first)
        self.activate("8", second)
        for source, yes, no in [("7", "first-only", "second-only"), ("8", "second-only", "first-only")]:
            result = self.call(source)[1]
            self.assertNotIn("error", result, result)
            self.assertIn(yes, json.dumps(result))
            self.assertNotIn(no, json.dumps(result))
        self.assertEqual(self.s.mutate("7", revision=1, method="DELETE")[0], 200)
        self.assertEqual(self.call("7")[1]["error"]["data"]["code"], "SOURCE_NOT_ACTIVE")
        self.assertNotIn("error", self.call("8")[1])

    def test_shadow_cannot_replace_the_requested_primary(self):
        index = self.artifact_with_projects(["private-native::missed"])
        with sqlite3.connect(index["path"]) as db:
            db.execute("DELETE FROM projects WHERE name='private-native'")
        index["artifact_sha256"] = sha(Path(index["path"]).read_bytes())
        self.s.config["indexes"] = [index]; self.s.write_config()
        self.assert_identity_failure(self.s.mutate("source-a", self.s.attestation(index)),
                                     "project_identity")

    def test_shadow_still_requires_native_generation_not_lens_generation(self):
        for generation in ["u0123456789abcdefg2", "f" * 64]:
            with self.subTest(generation=generation):
                index = self.artifact_with_projects(["private-native::missed"], generation)
                self.assert_identity_failure(self.s.mutate("source-a", self.s.attestation(index)),
                                             "native_generation")

    def test_shadow_still_requires_exact_artifact_checksum(self):
        index = self.artifact_with_projects(["private-native::missed"])
        index["artifact_sha256"] = "0" * 64
        self.s.config["indexes"] = [index]; self.s.write_config()
        response = self.s.mutate("source-a", self.s.attestation(index))
        self.assertEqual(response[0], 409, response)
        self.assertIn("ARTIFACT_CHECKSUM_MISMATCH", json.dumps(response[1]))

    def test_shadow_still_requires_native_integrity(self):
        index = self.artifact_with_projects(["private-native::missed"])
        with sqlite3.connect(index["path"]) as db:
            db.execute("UPDATE projects SET root_path='?invalid-root'")
        index["artifact_sha256"] = sha(Path(index["path"]).read_bytes())
        self.s.config["indexes"] = [index]; self.s.write_config()
        self.assert_identity_failure(self.s.mutate("source-a", self.s.attestation(index)), "integrity")

    def test_corrupt_artifact_original_sqlite_cause(self):
        path = self.work / (secrets.token_hex(6) + ".db")
        path.write_bytes(b"not a SQLite database" * 100)
        index = dict(self.one, repository_index_id="corrupt-index", path=str(path), artifact_sha256=sha(path.read_bytes()))
        self.s.config["indexes"].append(index); self.s.write_config()
        result = self.s.mutate("source-a", self.s.attestation(index))
        self.assertEqual(result[0], 409, result)
        self.assertIn("file is not a database", json.dumps(result[1]))
        self.assertIn("sqlite_code", json.dumps(result[1]))

    def test_expiry_and_health(self):
        expires = int(time.time()) + 30
        self.activate(expires_at=expires)
        time.sleep(max(0, expires - time.time() + .2))
        self.assertEqual(self.call()[1]["error"]["data"]["code"], "SOURCE_NOT_ACTIVE")
        health = self.s.request("GET", self.s.base + "/health", token=self.s.control)[1]
        self.assertEqual(health["details"]["sources"][0]["state"], "EXPIRED")

    def test_token_rotation_and_bad_config_fail_closed(self):
        old = self.s.data
        self.s.data = f"asms1.tenant-a.{self.s.project}.data." + secrets.token_hex(32)
        self.s.config["data_token_sha256"] = sha(self.s.data.encode()); self.s.write_config()
        self.assertEqual(self.s.rpc("ping", token=old)[0], 401)
        self.assertEqual(self.s.rpc("ping")[0], 200)
        self.s.path.write_text("invalid", encoding="utf-8")
        self.assertEqual(self.s.rpc("ping")[0], 503)

    def test_protocol_origin_host_accept_and_duplicate_headers(self):
        for headers in [{"Origin": "https://attacker.invalid"}, {"Host": "attacker.invalid"},
                        {"MCP-Protocol-Version": "2099-01-01"}, {"Accept": "application/json"}]:
            self.assertEqual(self.s.rpc("ping", extra=headers)[0], 400)
        with socket.create_connection(("127.0.0.1", self.s.port)) as sock:
            sock.sendall((f"POST {self.s.base}/mcp HTTP/1.1\r\nHost: 127.0.0.1:{self.s.port}\r\n"
                          "Authorization: Bearer first\r\nAuthorization: Bearer second\r\nContent-Length: 0\r\n\r\n").encode())
            self.assertIn(b" 400 ", sock.recv(4096))

    def test_duplicate_json_and_bounds(self):
        self.assertEqual(self.s.request("POST", self.s.base + "/mcp",
                                       b'{"jsonrpc":"2.0","id":1,"method":"ping","method":"tools/call"}')[0], 400)
        self.activate()
        self.assertEqual(self.call(max_rows=0)[0], 400)
        self.assertEqual(self.call(max_rows=101)[0], 400)
        self.assertEqual(self.s.rpc("tools/call", dict(name="trace_path", arguments=dict(source_id="source-a", function_name="x", depth=9)))[0], 400)

    def test_audit_metadata_and_retention(self):
        self.activate()
        self.call()
        dbpath = self.s.root / "control.sqlite"
        with sqlite3.connect(dbpath) as db:
            rows = db.execute("SELECT * FROM audit").fetchall()
            self.assertGreaterEqual(len(rows), 3)
            text = json.dumps(rows)
            for secret in [self.s.data, self.s.control, "MATCH", "alpha-only", self.one["path"], "Authorization"]:
                self.assertNotIn(secret, text)
            db.execute("INSERT INTO audit VALUES('old','','','begin','OK',0,0,1,160)")
        self.call()
        with sqlite3.connect(dbpath) as db:
            self.assertEqual(db.execute("SELECT count(*) FROM audit WHERE correlation='old'").fetchone()[0], 0)

    def test_audit_preflight_and_postflight_failure(self):
        self.activate()
        dbpath = self.s.root / "control.sqlite"
        with sqlite3.connect(dbpath) as db:
            db.execute("CREATE TRIGGER fail_begin BEFORE INSERT ON audit WHEN NEW.stage='begin' BEGIN SELECT RAISE(FAIL,'injected begin'); END")
        result = self.call()[1]
        self.assertEqual(result["error"]["data"]["code"], "AUDIT_PREFLIGHT_FAILED")
        with sqlite3.connect(dbpath) as db:
            db.execute("DROP TRIGGER fail_begin")
            db.execute("CREATE TRIGGER fail_end BEFORE INSERT ON audit WHEN NEW.stage='end' BEGIN SELECT RAISE(FAIL,'injected end'); END")
        result = self.call()[1]
        self.assertEqual(result["error"]["data"]["code"], "AUDIT_POSTFLIGHT_FAILED")

    def test_replay_step_preserves_original_sqlite_cause(self):
        with sqlite3.connect(self.s.root / "control.sqlite") as db:
            db.executescript("""
                ALTER TABLE replays RENAME TO saved_replays;
                CREATE VIEW replays AS SELECT 'step-failure' AS key,
                    json_extract('invalid-json','$') AS fingerprint,
                    '{}' AS response, 1 AS revision;
            """)
        response = self.s.mutate("source-a", self.s.attestation(self.one), key="step-failure")
        self.assertEqual(response[0], 503, response)
        self.assertEqual(response[1]["code"], "CONTROL_STORAGE_FAILED", response)
        causes = response[1]["causes"]
        self.assertTrue(any(c.get("type") == "sqlite" and c.get("code") == 1
                            and "malformed JSON" in c.get("message", "") for c in causes), response)

    def test_audit_size_step_preserves_original_sqlite_cause(self):
        self.activate()
        with sqlite3.connect(self.s.root / "control.sqlite") as db:
            for correlation in ("overflow-one", "overflow-two"):
                db.execute("INSERT INTO audit VALUES(?,'','','begin','OK',0,0,?,?)",
                           (correlation, int(time.time()), 9223372036854775807))
        response = self.call()[1]
        self.assertEqual(response["error"]["data"]["code"], "AUDIT_PREFLIGHT_FAILED", response)
        causes = response["error"]["data"]["causes"]
        self.assertTrue(any(c.get("type") == "sqlite" and c.get("code") == 1
                            and "integer overflow" in c.get("message", "") for c in causes), response)

    def test_double_failure_preserves_native_primary(self):
        self.activate()
        with sqlite3.connect(self.s.root / "control.sqlite") as db:
            db.execute("CREATE TRIGGER fail_end BEFORE INSERT ON audit WHEN NEW.stage='end' BEGIN SELECT RAISE(FAIL,'injected end'); END")
        result = self.s.rpc("tools/call", dict(name="query_graph", arguments=dict(source_id="source-a", query="NOT VALID CYPHER")))[1]
        self.assertTrue(result["result"]["isError"], result)
        self.assertIn("content", result["result"])
        self.assertEqual(result["result"]["_meta"]["causes"][0]["code"], "AUDIT_POSTFLIGHT_FAILED")
        self.assertTrue(result["result"]["_meta"]["correlation_id"])

    def test_audit_quota_fail_closed(self):
        self.activate()
        self.s.config["audit_quota_bytes"] = 1; self.s.write_config()
        self.assertEqual(self.call()[1]["error"]["data"]["code"], "AUDIT_PREFLIGHT_FAILED")

    def test_double_failure_preserves_quota_cause(self):
        self.activate()
        with sqlite3.connect(self.s.root / "control.sqlite") as db:
            used = db.execute("SELECT coalesce(sum(size),0) FROM audit").fetchone()[0]
        self.s.config["audit_quota_bytes"] = used + len("source-aquery_graphbeginACCEPTED") + 160
        self.s.write_config()
        result = self.s.rpc("tools/call", dict(name="query_graph", arguments=dict(source_id="source-a", query="NOT VALID CYPHER")))[1]
        self.assertTrue(result["result"]["isError"], result)
        cause = result["result"]["_meta"]["causes"][0]
        self.assertEqual(cause["code"], "AUDIT_POSTFLIGHT_FAILED", result)
        self.assertEqual(cause["causes"][0]["code"], "AUDIT_QUOTA_EXCEEDED", result)

    def test_artifact_mutation_fails_without_fallback(self):
        path = self.work / (secrets.token_hex(6) + ".db")
        index = fixture(path, "mutable-test")
        self.s.config["indexes"].append(index); self.s.write_config()
        self.activate(index=index)
        with open(path, "ab") as file:
            file.write(b"modified")
        result = self.call()[1]
        self.assertTrue(result["result"]["isError"], result)
        self.assertIn("ARTIFACT_CHECKSUM_MISMATCH", json.dumps(result))

    def test_config_permissions_and_hardlinks_fail_closed(self):
        if os.name == "nt":
            subprocess.run(["icacls", str(self.s.path), "/grant", "*S-1-1-0:R"],
                           check=True, capture_output=True)
        else:
            os.chmod(self.s.path, 0o644)
        self.assertNotEqual(self.s.rpc("tools/list")[0], 200)
        self.s.write_config()
        self.assertEqual(self.s.rpc("tools/list")[0], 200)
        alias = self.s.root / "config-hardlink.json"
        os.link(self.s.path, alias)
        self.assertNotEqual(self.s.rpc("tools/list")[0], 200)
        alias.unlink()
        self.assertEqual(self.s.rpc("tools/list")[0], 200)

    def test_read_only_native_query_does_not_mutate(self):
        self.activate()
        before = sha(Path(self.one["path"]).read_bytes())
        result = self.s.rpc("tools/call", dict(name="query_graph", arguments=dict(source_id="source-a", query="CREATE (n:Function {name:'bad'})")))[1]
        self.assertTrue(result["result"]["isError"], result)
        self.assertEqual(before, sha(Path(self.one["path"]).read_bytes()))


if __name__ == "__main__":
    unittest.main(verbosity=2)
