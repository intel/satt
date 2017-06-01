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
import urllib2
import tarfile
import subprocess
from satt.common import envstore

satt_release_server="http://your . release . server . com"
satt_python_packages = ['git+https://github.com/sch3m4/pyadb', 'pyelftools', 'requests']
satt_ui_python_packages = []

DISASSEMBLER = 'capstone-master'
DISASSEMBLER_URL = 'https://github.com/aquynh/capstone/archive/master.tar.gz'

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
        self._disassembler_path = os.path.join(self._sat_home, 'src', 'parser', DISASSEMBLER)

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
            if not ' ' in satt_release_server:
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
            # Install satt to path
            self.install_satt_to_path()

            # Add satt to bash autocompletion
            self.add_satt_autocompletion()

            # Install virtualenv env
            self.enable_install_virtualenv()

            print("\n**************************************************************")
            print("*** Install python packages to <satt>/bin/env")
            print("**************************************************************")
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

            # Install disassembler from the git
            self.check_disassembler()

            # Install parser
            self.build_satt_parser()

            # Install UI python components
            # Pure Linux version, does not work with Windows
            if self._args.ui:
                print("\n**************************************************************")
                print("*** Install required python packages for SATT the UI")
                print("**************************************************************")
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

                print("\n**************************************************************")
                print("*** Creating Postgresql DB configuration")
                print("**************************************************************")
                try:
                    use_sudo = raw_input('Would you like to use sudo rights to install satt user for postgresql db [y/n]?\n')
                    if use_sudo in ['y','Y','yes','Yes']:
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

    def add_satt_autocompletion(self):
        print("\n**************************************************************")
        print("*** Add SATT to bash autocompletion")
        print("**************************************************************")
        # Add satt autocomplete script
        use_sudo = raw_input('Would you like to use sudo rights to add satt support for bash autocompletition [y/n]?\n')
        if use_sudo in ['y','Y','yes','Yes']:
            src_path = os.path.join(self._sat_home, 'conf', 'satt.completion')
            dest_path = '/etc/bash_completion.d/satt'
            if os.path.exists('/etc/bash_completion.d'):
                os.system('sudo cp -f ' + src_path + ' ' + dest_path)

    def enable_install_virtualenv(self):
        print("\n**************************************************************")
        print("*** Install python virtual env to <satt>/bin/env")
        print("**************************************************************")
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

    def check_disassembler(self):
        print("\n**************************************************************")
        print("*** Install disassembler")
        print("**************************************************************")
        if (not os.path.exists(self._disassembler_path)):
            self.download_disassembler()
        self.install_disassembler()

    def download_disassembler(self):
        proxy = {}
        if os.environ.get('https_proxy'):
            proxy['https'] = os.environ.get('https_proxy')
        proxy_handler = urllib2.ProxyHandler(proxy)
        opener = urllib2.build_opener(proxy_handler)
        f = opener.open(DISASSEMBLER_URL)
        print('Downloading disassembler')
        data = f.read()
        with open('capstone.tgz', 'wb') as capstone_f:
           capstone_f.write(data)
        #tar = tarfile.open(mode = 'r:gz', fileobj = StringIO(data))
        tar = tarfile.open('capstone.tgz', mode = 'r:gz')
        tar.extractall(path=os.path.join(self._sat_home, 'src', 'parser'))
        tar.close()

        os.remove('capstone.tgz')

    def install_disassembler(self):
        capstone_binaries_dir = os.path.join(self._sat_home, 'src', 'parser', DISASSEMBLER, 'binaries')
        try:
            os.makedirs(capstone_binaries_dir)
        except:
            pass

        os.chdir(os.path.join(self._sat_home, 'src', 'parser', DISASSEMBLER))
        os.environ['PREFIX'] = capstone_binaries_dir
        subprocess.call('CAPSTONE_ARCHS="x86" CAPSTONE_STATIC=yes ./make.sh', shell=True, env=os.environ)
        subprocess.call('./make.sh install', shell=True, env=os.environ)

    def _check_and_remove_installed_satt_path(self, satt_symlink_path):
        # Check that there are no multiple SATTs in exec paths
        first_satt_path = subprocess.check_output(['which', 'satt']).strip()
        if not first_satt_path == satt_symlink_path:
            if os.access(os.path.dirname(first_satt_path), os.W_OK):
                os.remove(first_satt_path)
                print('Info: removed satt file from {0}'.format(first_satt_path))
                return True
            else:
                print('ERROR: Multiple satt files in path, please remote this instance "{0}"'.format(first_satt_path))
        return False

    def install_satt_to_path(self):
        print("\n**************************************************************")
        print("*** Add SATT to path")
        print("**************************************************************")
        paths = os.environ['PATH']
        bin_accessed_paths = []
        for path in paths.split(":"):
            if (os.access(path, os.W_OK|os.X_OK|os.R_OK) and
                not path == envstore.store.get_sat_venv_bin()):
                bin_accessed_paths.append(path)

        # Sort paths by length and install to shortest path
        bin_accessed_paths.sort(key = len)
        if len(bin_accessed_paths):
            satt_symlink_path = os.path.join(bin_accessed_paths[0], 'satt')
            if os.path.exists(satt_symlink_path):
                os.remove(satt_symlink_path)

            os.symlink(os.environ['SATT_EXEC'], satt_symlink_path)
            print('SATT symbolic link added to PATH "{0}"'.format(satt_symlink_path))
            while (self._check_and_remove_installed_satt_path(satt_symlink_path)):
                pass

        else:
            print('Warninig: Could not install SATT to any local PATH')
            use_sudo = raw_input('Would you like to use sudo access right to create satt link to /usr/bin/satt [y/n]?\n')
            if use_sudo in ['y','Y','yes','Yes']:
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

    def build_satt_parser(self):
        print("\n**************************************************************")
        print("*** Build SATT-parser")
        print("**************************************************************")

        os.chdir(os.path.join(self._sat_home, 'src', 'parser', 'post-processor'))

        # scons flags="-static -I $SAT_HOME/src/parser/udis86-master/binaries/include -L $SAT_HOME/src/parser/udis86-master/binaries/lib"
        subprocess.call("""scons flags="-static -I {0} -L {1} -L {2}" """.format(
            os.path.join(self._disassembler_path, 'binaries', 'include'),
            os.path.join(self._disassembler_path, 'binaries', 'lib'),
            os.path.join(self._disassembler_path, 'binaries', 'lib64')), shell=True, env=os.environ)

        tar = tarfile.open(os.path.join(self._sat_home, 'src', 'parser', 'post-processor', 'sat-post-processor-binaries.tgz'), mode = 'r:gz')
        tar.extractall(path=os.path.join(self._sat_home, 'satt', 'process'))
        tar.close()
