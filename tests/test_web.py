"""HTTP authentication, terminal execution, resize and cleanup tests."""
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('shellforge_web',ROOT/'web/server.py')
web = importlib.util.module_from_spec(spec)
spec.loader.exec_module(web)

class WebTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        cls.session=web.TerminalSession(ROOT/'bin/shellforge',cls.temp.name)
        cls.server=web.TerminalServer(('127.0.0.1',0),'test-only-token',cls.session,['https://trusted.example'])
        cls.url='http://127.0.0.1:'+str(cls.server.server_port)
        cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.session.close();cls.server.shutdown();cls.server.server_close()
        cls.thread.join();cls.temp.cleanup()

    def request(self,path,data=None,token='test-only-token',headers=None,method=None):
        h={'Authorization':'Bearer '+token}
        h.update(headers or {})
        req=Request(self.url+path,data=None if data is None else json.dumps(data).encode(),headers=h,method=method)
        with urlopen(req,timeout=5) as response:
            return response.status,response.headers,response.read()

    def test_auth_required(self):
        for path in ['/api/status','/api/output']:
            with self.assertRaises(HTTPError) as context:self.request(path,token='wrong')
            self.assertEqual(context.exception.code,401)

    def test_cross_origin_rejected(self):
        with self.assertRaises(HTTPError) as context:
            self.request('/api/start',{},headers={'Origin':'https://untrusted.example'})
        self.assertEqual(context.exception.code,403)

    def test_host_rejected(self):
        with self.assertRaises(HTTPError) as context:self.request('/api/status',headers={'Host':'attacker.example'})
        self.assertEqual(context.exception.code,403)

    def test_trusted_preflight(self):
        code,headers,_=self.request('/api/start',headers={'Origin':'https://trusted.example'},method='OPTIONS')
        self.assertEqual(code,204)
        self.assertEqual(headers['Access-Control-Allow-Origin'],'https://trusted.example')

    def test_static_and_traversal(self):
        code,headers,body=self.request('/')
        self.assertEqual(code,200)
        self.assertIn(b'ShellForge',body)
        self.assertEqual(headers['X-Content-Type-Options'],'nosniff')
        with self.assertRaises(HTTPError) as context:self.request('/%2e%2e/server.py')
        self.assertEqual(context.exception.code,404)

    def test_invalid_input_and_size(self):
        for path,data in [('/api/input',{'data':42}),('/api/resize',{'rows':-1,'cols':80}),('/api/input',{'data':'x'*9000})]:
            with self.assertRaises(HTTPError) as context:self.request(path,data)
            self.assertEqual(context.exception.code,400)

    def test_real_terminal_roundtrip(self):
        _,_,body=self.request('/api/start',{})
        session=json.loads(body)['session']
        self.request('/api/resize',{'rows':30,'cols':100})
        self.request('/api/input',{'data':'echo browser-test > test.txt\ncat < test.txt\nthreads\n'})
        output='';cursor=0;deadline=time.monotonic()+5
        while time.monotonic()<deadline and '400000 (expected 400000)' not in output:
            _,_,body=self.request(f'/api/output?cursor={cursor}&session={session}')
            result=json.loads(body);output+=result['output'];cursor=result['cursor']
        self.assertIn('browser-test',output)
        self.assertIn('400000 (expected 400000)',output)
        self.assertEqual((Path(self.temp.name)/'test.txt').read_text(),'browser-test\n')
        self.request('/api/stop',{})
        self.assertFalse(self.session.info()['running'])
        _,_,body=self.request('/api/start',{})
        self.assertGreater(json.loads(body)['session'],session)
        self.request('/api/stop',{})

if __name__=='__main__':unittest.main()
