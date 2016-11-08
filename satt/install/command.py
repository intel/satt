'''
// Copyright (c) 2015 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
'''

##################################
# description: Install satt tool
##################################

import os
import sys
import argparse
from satt.common import envstore

satt_release_server="http://satt.example.com"
satt_python_packages = ['git+https://github.com/sch3m4/pyadb', 'pyelftools', 'requests']
satt_ui_python_packages = []

##################################
# Command file for satt install
##################################
class_name = "SattInstall"


class SattInstall:
    _sat_home = ''
    _variables = {}
    _server_version = ''
    _local_version = ''
    _install = True

    def __init__(self):
        self._sat_home = envstore.store.get_sat_home()

    def action(self):
        # Parameter handling
        parser = argparse.ArgumentParser(description='satt install')
        parser.add_argument('-u', '--ui', action='store_true',
                            help='install ui', required=False)
        self._args = parser.parse_args()

        # Check local satt version
        self._local_version = envstore.store.get_sat_version()
        if self._local_version == '':
            self._local_version == '0.0.0'

        print ("SATT version: " + self._local_version + "\n")

        # Check latest satt version in server for update
        try:
            import urllib2
            proxy_handler = urllib2.ProxyHandler({})
            opener = urllib2.build_opener(proxy_handler)
            resp = opener.open(satt_release_server + '/releases/version.txt')
            self._server_version = resp.read().rstrip()
        except:
            print ("Can't read version info from SATT server")

        if self._server_version != '':
            t_serv = tuple(map(int, (self._server_version.split("-")[0].split("."))))
            t_loc = tuple(map(int, (self._local_version.split("-")[0].split("."))))
            if t_serv > t_loc:
                print ("New version available: " + self._server_version)
                resp = raw_input("Update new version (y/N)? ")
                if len(resp) > 0 and resp[0].lower() == 'y':
                    self._install = False
                    import urllib2
                    proxy_handler = urllib2.ProxyHandler({})
                    opener = urllib2.build_opener(proxy_handler)
                    link = satt_release_server + '/releases/sat-latest.tgz'
                    try:
                        rf = opener.open(link)
                        with open(os.path.basename(link), "wb") as lf:
                            lf.write(rf.read())
                    except urllib2.HTTPError as e:
                        print ("HTTP error: ", e.code, link)
                    except urllib2.URLError as e:
                        print ("URL error: ", e.reason, link)

                    print ("Extract file")
                    os.system('tar -xvzf ' + os.path.basename(link))
                    envstore.store.set_sat_version(self._server_version + '\n')
                    print ("\nPlease re-run 'bin/satt install' for new satt version")
            else:
                print ("SATT environment is up-to-date")

        if self._install:
            print ("Install..")
            # Create link to /usr/bin
            src_path = os.path.join(self._sat_home, 'bin', 'satt')
            dest_path = '/usr/bin/satt'
            desc_text = 'Request sudo access to install satt into ' + dest_path
            if os.path.lexists(dest_path):
                if os.readlink(dest_path) != src_path:
                    print (desc_text)
                    os.system('sudo rm -f ' + dest_path)
                    os.system('sudo ln -s ' + src_path + ' ' + dest_path)
            else:
                print (desc_text)
                os.system('sudo ln -s ' + src_path + ' ' + dest_path)

            # Add satt autocomplete script
            src_path = os.path.join(self._sat_home, 'conf', 'satt.completion')
            dest_path = '/etc/bash_completion.d/satt'
            if os.path.exists('/etc/bash_completion.d'):
                os.system('sudo cp -f ' + src_path + ' ' + dest_path)

            self.enable_install_virtualenv()

            # Install packages
            virtual_env_pip_path = os.path.join(self._sat_home, 'bin', 'env','bin','pip')
            if not os.path.exists(virtual_env_pip_path):
                print ("ERROR: Virtualenv pip did not found!")
                return -1

            for package in satt_python_packages:
                try:
                    os.system(virtual_env_pip_path + " install '" + package + "'")
                except Exception as e:
                    print ("Error installing package " + package)
                    print (e)

            # Install UI python components
            # Pure Linux version, does not work with Windows
            if self._args.ui:
                print ("Installing required UI packages")
                error = False
                sat_backend_requirements = os.path.join(self._sat_home, 'satt', 'visualize', 'backend', 'requirement.txt')
                with open(sat_backend_requirements) as f:
                    for package in f:
                        try:
                            os.system(virtual_env_pip_path + " install '" + package + "'")
                        except Except as e:
                            print (e)
                            error = True
                if error:
                    print ("ERROR: All UI packages was not installed correctly")
                    print ("try to install required packages and run again > satt install --ui ")

                print ("Postgresql DB configuration")
                try:
                    user = os.popen('''sudo -u postgres psql -q --command "SELECT * FROM pg_user WHERE usename = 'sat';" ''').read()
                    if "sat" not in user:
                        os.system('''sudo -u postgres psql -q --command "CREATE USER sat WITH PASSWORD 'uranus';" ''')
                    else:
                        print ("User Ok")

                    db = os.popen('''sudo -u postgres psql -q --command "SELECT datname FROM pg_catalog.pg_database WHERE lower(datname) = 'sat';"''').read()
                    if "sat" not in db:
                        os.system('''sudo -u postgres psql -q --command "CREATE DATABASE sat OWNER sat;" ''')
                    else:
                        print ("DB Ok")

                    # Find pg_hba.conf filepath
                    hba_file = os.popen('''sudo -u postgres psql -q -P format=unaligned --command "SHOW hba_file;"''').read()
                    hba_file = hba_file.split('\n')[1]

                    # Permissions check
                    sat_user = os.system('sudo grep -q "sat" ' + hba_file)
                    if sat_user:
                        os.system('''sudo sed -i 's/local.*all.*postgres.*peer/local   all             postgres                                peer\\nlocal   all             sat                                     trust/' ''' + hba_file)
                        # Restart postgres
                        os.system('''sudo -u postgres psql -q --command "SELECT pg_reload_conf();"''')
                    else:
                        print ("Permissions Ok")

                except Exception as e:
                    print ("ERROR: DB configuration failed, please check the erros and try again.")
                    print (e)

            print ("Install complete.")

    def enable_install_virtualenv(self):
        # Check if Virtual env has been installed
        VENV = hasattr(sys, "real_prefix")

        if not VENV:
            virtual_env_path = os.path.join(self._sat_home, 'bin', 'env')
            os.system('virtualenv ' + virtual_env_path + ' > /dev/null')

            activate_this_file = os.path.join(virtual_env_path, 'bin', 'activate_this.py')
            if os.path.isfile(activate_this_file):
                if sys.version_info < (3, 0):
                    execfile(activate_this_file, dict(__file__=activate_this_file))
                else:
                    exec(compile(open(activate_this_file, "rb").read(), activate_this_file, 'exec'), dict(__file__=activate_this_file))
            else:
                print ("ERROR: Virtualenv installation error!")
        else:
            print ("Virtualenv installed")
