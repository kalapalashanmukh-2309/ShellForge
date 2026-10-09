"""Behavioral integration tests against the actual compiled shell."""
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('SHELLFORGE_BINARY', ROOT / 'bin/shellforge'))

class ShellTests(unittest.TestCase):
    def setUp(self):
        self.workspace = tempfile.TemporaryDirectory()
        self.addCleanup(self.workspace.cleanup)
        self.cwd = Path(self.workspace.name)

    def run_shell(self, script=None, command=None, timeout=8):
        args = [str(BINARY)] + (['-c', command] if command is not None else [])
        return subprocess.run(args, input=script or '', text=True, capture_output=True,
                              cwd=self.cwd, timeout=timeout)

    def test_eof(self):
        p = self.run_shell()
        self.assertEqual((p.returncode, p.stdout, p.stderr), (0, '', ''))

    def test_execute_and_quotes(self):
        p = self.run_shell(command='printf "%s|%s|%s\\n" "hello world" \'a|b\' ""')
        self.assertEqual(p.stdout, 'hello world|a|b|\n')
        self.assertEqual(p.returncode, 0)

    def test_escaped_space(self):
        self.assertEqual(self.run_shell(command=r'printf "%s" hello\ world').stdout, 'hello world')

    def test_comment_and_blank(self):
        self.assertEqual(self.run_shell('\n# comment\necho ok # ignored\n').stdout, 'ok\n')

    def test_overwrite_append_input(self):
        p = self.run_shell('echo first > out.txt\necho second >> out.txt\ncat < out.txt\n')
        self.assertEqual(p.stdout, 'first\nsecond\n')
        self.assertEqual((self.cwd/'out.txt').read_text(), p.stdout)

    def test_truncate(self):
        self.run_shell('echo old > out\necho new > out\n')
        self.assertEqual((self.cwd/'out').read_text(), 'new\n')

    def test_error_redirection(self):
        p = self.run_shell('ls definitely-missing 2> errors.txt\n')
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(p.stderr, '')
        self.assertIn('definitely-missing', (self.cwd/'errors.txt').read_text())

    def test_combined_redirection(self):
        (self.cwd/'in').write_text('hello\n')
        p = self.run_shell('cat <in >out 2>err\n')
        self.assertEqual(p.returncode, 0)
        self.assertEqual((self.cwd/'out').read_text(), 'hello\n')
        self.assertEqual((self.cwd/'err').read_text(), '')

    def test_redirect_argument_after_filename(self):
        self.run_shell('echo >out hello world\n')
        self.assertEqual((self.cwd/'out').read_text(), 'hello world\n')

    def test_quoted_redirect_filename(self):
        self.run_shell('echo hello > "two words.txt"\n')
        self.assertEqual((self.cwd/'two words.txt').read_text(), 'hello\n')

    def test_redirect_order(self):
        self.run_shell('echo hi >first >second\n')
        self.assertEqual((self.cwd/'first').read_text(), '')
        self.assertEqual((self.cwd/'second').read_text(), 'hi\n')

    def test_builtin_redirection_restores_stdout(self):
        p = self.run_shell('pwd >cwd.txt\necho visible\n')
        self.assertEqual(p.stdout, 'visible\n')
        self.assertEqual(Path((self.cwd/'cwd.txt').read_text().strip()).resolve(), self.cwd.resolve())

    def test_failed_builtin_redirection_restores_stdout(self):
        p = self.run_shell('pwd >/missing-dir/file\necho visible\n')
        self.assertEqual(p.stdout, 'visible\n')
        self.assertTrue(p.stderr)

    def test_missing_input(self):
        self.assertNotEqual(self.run_shell(command='cat < missing').returncode, 0)

    def test_multistage_pipeline(self):
        p = self.run_shell(command='printf "pear\\napple\\npear\\n" | sort | uniq | wc -l')
        self.assertEqual(int(p.stdout.strip()), 2)
        self.assertEqual(p.returncode, 0)

    def test_large_pipeline_does_not_deadlock(self):
        p = self.run_shell(command='seq 1 100000 | wc -l')
        self.assertEqual(int(p.stdout.strip()), 100000)

    def test_pipeline_redirection(self):
        (self.cwd/'in').write_text('b\na\n')
        p = self.run_shell('cat <in | sort >out\n')
        self.assertEqual(p.returncode, 0)
        self.assertEqual((self.cwd/'out').read_text(), 'a\nb\n')

    def test_builtin_in_pipeline(self):
        p = self.run_shell(command='pwd | wc -l')
        self.assertEqual(int(p.stdout.strip()), 1)

    def test_cd_persists(self):
        (self.cwd/'child').mkdir()
        p = self.run_shell('cd child\npwd\n')
        self.assertEqual(Path(p.stdout.strip()).resolve(), (self.cwd/'child').resolve())

    def test_exit_status(self):
        self.assertEqual(self.run_shell(command='exit 42').returncode, 42)
        self.assertEqual(self.run_shell('false\nexit\n').returncode, 1)
        self.assertEqual(self.run_shell('false\nstatus\n').stdout, '1\n')
        self.assertEqual(self.run_shell(command='false | true').returncode, 0)

    def test_unknown_command(self):
        p = self.run_shell(command='shellforge-command-does-not-exist')
        self.assertEqual(p.returncode, 127)
        self.assertIn('shellforge-command-does-not-exist', p.stderr)

    def test_unknown_command_error_redirection(self):
        p = self.run_shell(command='shellforge-missing-command 2>err')
        self.assertEqual(p.returncode, 127)
        self.assertEqual(p.stderr, '')
        self.assertIn('shellforge-missing-command', (self.cwd/'err').read_text())

    def test_invalid_syntax(self):
        for command in ['echo >', '| cat', 'echo hi |', 'echo hi || cat', 'echo hi && pwd', 'echo hi ; pwd', 'echo "unterminated', '&', 'echo hi & pwd']:
            with self.subTest(command=command):
                self.assertEqual(self.run_shell(command=command).returncode, 2)

    def test_long_input(self):
        text = 'x' * 10000
        self.assertEqual(self.run_shell(command='echo '+text).stdout, text+'\n')
        self.assertEqual(self.run_shell('x'*65537+'\n').returncode, 2)

    def test_parallel_background(self):
        start = time.monotonic()
        p = self.run_shell('sleep 1 &\nsleep 1 &\njobs\nwait\necho finished\n')
        elapsed = time.monotonic()-start
        self.assertEqual(p.returncode, 0)
        self.assertIn('Running', p.stdout)
        self.assertIn('finished', p.stdout)
        self.assertLess(elapsed, 1.9)

    def test_background_pipeline(self):
        p = self.run_shell('printf hello | cat >out &\nwait\ncat out\n')
        self.assertTrue(p.stdout.endswith('hello'))
        self.assertEqual(p.returncode, 0)

    def test_background_exit_cleanup(self):
        p = self.run_shell('sleep 60 &\nexit\n', timeout=3)
        pid = int(p.stdout.split()[1])
        with self.assertRaises(ProcessLookupError): os.kill(pid, 0)

    def test_threads_and_join(self):
        p = self.run_shell(command='threads')
        self.assertEqual(p.returncode, 0)
        self.assertIn('400000 (expected 400000)', p.stdout)

    def test_race_demonstration(self):
        p = self.run_shell(command='race-demo')
        self.assertEqual(p.returncode, 0)
        self.assertIn('Expected: 2000 | Actual: 1000', p.stdout)

    def test_monitor_shutdown_promptly(self):
        start = time.monotonic()
        p = self.run_shell('monitor on\nmonitor status\nexit\n')
        self.assertIn('Monitor: on', p.stdout)
        self.assertLess(time.monotonic()-start, 2)

    def test_multiple_commands_after_pipeline(self):
        p = self.run_shell('echo first | cat\necho second\necho third\n')
        self.assertEqual(p.stdout, 'first\nsecond\nthird\n')

    def test_legacy_pipe_child_exits(self):
        p = self.run_shell('pipe\necho alive\n')
        self.assertEqual(p.returncode, 0)
        self.assertEqual(p.stdout.count('alive'), 1)
        self.assertIn('270000 bytes', p.stdout)

class TerminalTests(unittest.TestCase):
    def test_ctrl_c_foreground_and_eof(self):
        pid, master = pty.fork()
        if pid == 0:
            os.execv(str(BINARY), [str(BINARY)])
        def until(needle, timeout=5):
            data=b''
            deadline=time.monotonic()+timeout
            while needle not in data and time.monotonic()<deadline:
                if select.select([master], [], [], 0.1)[0]:
                    try: data+=os.read(master,65536)
                    except OSError: break
            self.assertIn(needle, data, data.decode(errors='replace'))
            return data
        try:
            until(b'shellforge>')
            os.write(master,b'sleep 30\n')
            time.sleep(0.15)
            os.write(master,b'\x03')
            until(b'shellforge>')
            os.write(master,b'status\n')
            self.assertIn(b'130', until(b'shellforge>'))
            os.write(master,b'\x03')
            until(b'shellforge>')
            os.write(master,b'\x04')
            until(b'Goodbye!')
            os.waitpid(pid,0)
            pid=0
        finally:
            if pid:
                try: os.kill(pid,signal.SIGKILL)
                except ProcessLookupError: pass
                os.waitpid(pid,0)
            os.close(master)

if __name__ == '__main__': unittest.main()
