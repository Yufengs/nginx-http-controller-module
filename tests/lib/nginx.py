
import os
import re
import sys
import time
import fcntl
import atexit
import shutil
import signal
import argparse
import tempfile
import unittest
import subprocess

class TestNginx(unittest.TestCase):

    pardir = os.path.abspath(
        os.path.join(os.path.dirname(__file__), os.pardir, os.pardir)
    )

    detailed = False
    save_log = False
    print_log = False

    @classmethod
    def main(cls):
        args, rest = TestNginx._parse_args()

        for i, arg in enumerate(rest):
            if arg[:5] == 'test_':
                rest[i] = cls.__name__ + '.' + arg

        sys.argv = sys.argv[:1] + rest
        TestNginx._set_args(args)

        unittest.main()

    @classmethod
    def setUpClass(cls):
        ngx = TestNginx()
        ngx._run()

        for i in range(50):
            with open(ngx.log_file, 'r') as f:
                log = f.read()
                m = re.search('start worker processes', log)

                if m is None:
                    time.sleep(0.1)
                else:
                    break
        
        if m is None:
            ngx.stop()
            exit("Nginx is writing log too long")

        def destroy():
            ngx.stop()
            shutil.rmtree(ngx.testdir)

        def complete():
            destroy()

        complete()

    def setUp(self, conf):
        self._run(conf)

    def _run(self, conf=None):
        self.nginx_bin = os.getenv(
            'TEST_NGINX_BINARY', '/usr/local/nginx/sbin/nginx'
        )

        if not os.path.isfile(self.nginx_bin):
            exit("Could not find nginx")

        self.testdir = tempfile.mkdtemp(prefix='nginx-test-')
        self.public_dir(self.testdir)

        os.mkdir(self.testdir + '/logs')
        os.mkdir(self.testdir + '/conf')

        self.pid_file = self.testdir + '/logs/nginx.pid'
        self.log_file = self.testdir + '/logs/error.log'
        
        if not conf:
            conf = '''
        error_log  logs/error.log debug;
        events {}
        '''

        self.write_file('conf/nginx.conf', conf)

        with open(self.log_file, 'w') as log:
            self._p = subprocess.Popen(
                [
                    self.nginx_bin,
                    '-p', self.testdir,
                ],
                stderr=log,
            )

        atexit.register(self.stop)

        if not self.waitforfiles(self.pid_file):
            exit("Could not start nginx")

        self.skip_alerts = []

    def tearDown(self):
        self.stop()

        # detect errors and failures for current test

        def list2reason(exc_list):
            if exc_list and exc_list[-1][0] is self:
                return exc_list[-1][1]

        if hasattr(self, '_outcome'):
            result = self.defaultTestResult()
            self._feedErrorsToResult(result, self._outcome.errors)
        else:
            result = getattr(
                self, '_outcomeForDoCleanups', self._resultForDoCleanups
            )

        success = not list2reason(result.errors) and not list2reason(
            result.failures
        )

        # check error.log for alerts

        with open(self.log_file, 'r', encoding='utf-8', errors='ignore') as f:
            self._check_alerts(f.read())

        if not TestNginx.save_log and success:
            shutil.rmtree(self.testdir)

        else:
            self._print_log()

    def stop(self):
        self._stop()
        atexit.unregister(self.stop)

    def _stop(self):
        if self._p.poll() is None:
            with self._p as p:
                p.send_signal(signal.SIGQUIT)

                try:
                    retcode = p.wait(15)
                    if retcode:
                        self.fail(
                            "Child process terminated with code " + str(retcode)
                        )
                except:
                    p.kill()
                    self.fail("Could not terminate unit")

    def waitforfiles(self, *files):
        for i in range(50):
            wait = False
            ret = False

            for f in files:
                if not os.path.exists(f):
                    wait = True
                    break

            if wait:
                time.sleep(0.1)

            else:
                ret = True
                break

        return ret

    def public_dir(self, path):
        os.chmod(path, 0o777)

        for root, dirs, files in os.walk(path):
            for d in dirs:
                os.chmod(os.path.join(root, d), 0o777)
            for f in files:
                os.chmod(os.path.join(root, f), 0o777)

    def write_file(self, name, data):
        path = self.testdir + '/' + name

        file = open(path, 'w')
        file.write(data);
        file.close()

    def _check_alerts(self, log):
        found = False

        alerts = re.findall('.+\[alert\].+', log)

        if alerts:
            print('All alerts errors found in log:')
            [print(alert) for alert in alerts]
            found = True

        if self.skip_alerts:
            for skip in self.skip_alerts:
                alerts = [al for al in alerts if re.search(skip, al) is None]

        if alerts:
            self._print_log(log)
            self.assertFalse(alerts, 'alert(s)')

        if found:
            print('skipped.')
    
    @staticmethod
    def _parse_args():
        parser = argparse.ArgumentParser(add_help=False)

        parser.add_argument(
            '-d',
            '--detailed',
            dest='detailed',
            action='store_true',
            help='Detailed output for tests',
        )
        parser.add_argument(
            '-l',
            '--log',
            dest='save_log',
            action='store_true',
            help='Save unit.log after the test execution',
        )
        parser.add_argument(
            '-r',
            '--reprint_log',
            dest='print_log',
            action='store_true',
            help='Print unit.log to stdout in case of errors',
        )

        return parser.parse_known_args()

    @staticmethod
    def _set_args(args):
        TestNginx.detailed = args.detailed
        TestNginx.save_log = args.save_log

        # set stdout to non-blocking

        if TestNginx.detailed:
            fcntl.fcntl(sys.stdout.fileno(), fcntl.F_SETFL, 0)

    def _print_log(self, data=None):
        print('Path to error.log:\n' + self.log_file)

        if TestNginx.print_log:
            if data is None:
                with open(self.log_file, 'r', encoding='utf-8', errors='ignore') as f:
                    data = f.read()

            print(data)
