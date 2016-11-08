#!/usr/bin/env python
# -*- coding: utf-8 -*-
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

""" AndroidOs Class

"""

import os
import re
import sys
import glob
import time
import pickle
import shutil
import threading
from satt.common import helper
from satt.common import envstore
from satt.common.targetos.targetos import TargetOs


class AndroidOs(TargetOs):
    """ Android specific impl
    """
    _sat_android_module_paths = ['/data/sat.ko', '/lib/modules/sat.ko']
    _local_build_path = ''

    def __init__(self):
        # Base class init call
        TargetOs.__init__(self)

# ####################################
# Public methods
# ####################################
    def get_os_data(self, trace_path):
        TargetOs.get_os_data(self, trace_path)
        self.debug_print("AndroidOS::get_os_data")
        self._trace_path = trace_path
        self._get_build_info()
        self._get_screen_shot()
        self._dump_art_binaries()

    def get_vmlinux_path(self):
        self.debug_print("AndroidOS::get_vmlinux_path")
        return os.path.join(envstore.store.get_variable('sat_path_kernel'), 'vmlinux')

    def get_sat_module_paths(self):
        self.debug_print("AndroidOS::get_sat_module_paths")
        return self._sat_android_module_paths

    def get_system_map_path(self):
        self.debug_print("AndroidOS::get_system_map_path")
        return os.path.join(envstore.store.get_variable('sat_path_kernel'), 'System.map')

    def get_tmp_folder(self):
        self.debug_print("AndroidOS::get_tmp_folder")
        return '/data'

    def get_name(self):
        self.debug_print("AndroidOS::get_name")
        return 'Android'

    # Methods for CONFIG
    # ##################
    def config(self, variables):
        self._set_official_or_local(variables)
        if not variables['sat_build_official']:
            self._set_sat_target_source(variables)
            self._set_sat_target_build(variables)
            self._set_sat_kernel_paths(variables)
        else:
            variables['sat_target_build'] = ''
            variables['sat_target_source'] = ''
            variables['sat_path_kernel'] = ''
            variables['sat_path_kernel_src'] = ''
            variables['sat_path_modules'] = ''

    def _set_official_or_local(self, variables):
        origins = [False, True]
        print helper.color.BOLD + 'Select: SW Build origin' + helper.color.END
        print " * 0) Own Build"
        print "   1) Official Build"
        variables['sat_build_official'] = origins[0]
        value = self._readchar()
        if value.isdigit():
            if int(value) >= 0 and int(value) <= 1:
                variables['sat_build_official'] = origins[int(value)]
        print

    def _set_sat_target_source(self, variables):
        print ('Set ' + helper.color.BOLD + 'Target source path' + helper.color.END + ' ' * 7 +
               '--> Path to point root folder of Android build you are tracing')
        if 'sat_target_source' in variables and variables['sat_target_source'] != '':
            print ' * current ' + ' ' * 17 + ': ' + variables['sat_target_source']
        else:
            print '   example ' + ' ' * 17 + ': /home/joe/builds/android'
        self._helper.prepare_readline()
        selection = raw_input('   enter' + ' ' * 20 + ': ')
        if selection != '':
            variables['sat_target_source'] = selection
        print '   Target source path = ' + helper.color.BOLD + variables['sat_target_source'] + helper.color.END + '\n'

    def _set_sat_target_build(self, variables):
        target_path = ''
        if os.path.exists(os.path.join(variables['sat_target_source'], 'android', 'out', 'target', 'product')):
            target_path = os.path.join(variables['sat_target_source'], 'android', 'out', 'target', 'product')
        elif os.path.exists(os.path.join(variables['sat_target_source'], 'out', 'target', 'product')):
            target_path = os.path.join(variables['sat_target_source'], 'out', 'target', 'product')
        else:
            print helper.color.BOLD + 'Warning:' + helper.color.END + ' Out target path not found, fed manually'
            variables['sat_target_build'] = ''
            variables['sat_target_source'] = ''

        if target_path != '':
            prods = os.walk(target_path).next()[1]
            if len(prods) == 0:
                print helper.color.BOLD + 'Warning:' + helper.color.END + ' No targets found from ' + target_path
            elif len(prods) == 1:
                variables['sat_target_build'] = os.path.join(target_path, prods[0])
                print 'Auto selected ' + helper.color.BOLD + 'Target build path' + helper.color.END
                print '   Target build path = ' + helper.color.BOLD + variables['sat_target_build'] + helper.color.END + '\n'
            else:
                print 'Select: ' + helper.color.BOLD + 'Target build path' + helper.color.END + ' to be used'
                for idx, val in enumerate(prods):
                    if val == os.path.basename(variables['sat_target_build']):
                        print ' *',
                    else:
                        print '  ',
                    print str(idx) + ') ' + val
                while True:
                    value = self._readchar()
                    if value.isdigit():
                        if int(value) >= 0 and int(value) <= len(prods):
                            variables['sat_target_build'] = os.path.join(target_path, prods[int(value)])
                            break
                    elif ord(value) == 13:
                        break
                print '   Target build path = ' + helper.color.BOLD + variables['sat_target_build'] + helper.color.END + '\n'

    def _set_sat_kernel_paths(self, variables):
        # SAT_PATH_KERNEL
        variables['sat_path_kernel'] = ""
        variables['sat_path_kernel_src'] = ""
        variables['sat_path_modules'] = ""

        if os.path.exists(os.path.join(variables['sat_target_build'], 'obj', 'KERNELOBJ')):
            variables['sat_path_kernel'] = os.path.join(variables['sat_target_source'], 'obj', 'KERNELOBJ')
        elif os.path.exists(os.path.join(variables['sat_target_build'], 'linux', 'kernel', 'gmin')):
            variables['sat_path_kernel'] = os.path.join(variables['sat_target_build'], 'linux', 'kernel', 'gmin')
        elif os.path.exists(os.path.join(variables['sat_target_build'], 'linux', 'kernel')):
            variables['sat_path_kernel'] = os.path.join(variables['sat_target_build'], 'linux', 'kernel')
        elif os.path.exists(os.path.join(variables['sat_target_build'], 'obj', 'kernel')):
            variables['sat_path_kernel'] = os.path.join(variables['sat_target_build'], 'obj', 'kernel')
        elif os.path.exists(os.path.join(variables['sat_target_source'], 'kernel')):
            if 'cht' in os.path.basename(variables['sat_target_build']):
                variables['sat_path_kernel'] = os.path.join(variables['sat_target_source'], 'kernel', 'cht')
            elif 's3g' in os.path.basename(variables['sat_target_build']):
                variables['sat_path_kernel'] = os.path.join(variables['sat_target_source'], 'kernel', 'sofia')
            elif 'slt' in os.path.basename(variables['sat_target_build']):
                variables['sat_path_kernel'] = os.path.join(variables['sat_target_source'], 'kernel', 'sofia-lte')
            elif 'coho' in os.path.basename(variables['sat_target_build']):
                variables['sat_path_kernel'] = os.path.join(variables['sat_target_source'], 'kernel', 'coho')
            elif 'bxt' in os.path.basename(variables['sat_target_build']):
                variables['sat_path_kernel'] = os.path.join(variables['sat_target_source'], 'kernel', 'bxt')

        if os.path.exists(os.path.join(variables['sat_target_source'], 'kernel')):
            if 'cht' in os.path.basename(variables['sat_target_build']):
                variables['sat_path_kernel_src'] = os.path.join(variables['sat_target_source'], 'kernel', 'cht')
            elif 's3g' in os.path.basename(variables['sat_target_build']):
                variables['sat_path_kernel_src'] = os.path.join(variables['sat_target_source'], 'kernel', 'sofia')
            elif 'slt' in os.path.basename(variables['sat_target_build']):
                variables['sat_path_kernel_src'] = os.path.join(variables['sat_target_source'], 'kernel', 'sofia-lte')
            elif 'coho' in os.path.basename(variables['sat_target_build']):
                variables['sat_path_kernel_src'] = os.path.join(variables['sat_target_source'], 'kernel', 'coho')
            elif 'bxt' in os.path.basename(variables['sat_target_build']):
                variables['sat_path_kernel_src'] = os.path.join(variables['sat_target_source'], 'kernel', 'bxt')
            else:
                variables['sat_path_kernel_src'] = os.path.join(variables['sat_target_source'], 'kernel', 'gmin')
        else:
            variables['sat_path_kernel_src'] = variables['sat_path_kernel']

        selection = raw_input("   Use kernel path: '" + variables['sat_path_kernel'] + "' ? [Y/n] ")
        if selection == 'n' or selection == 'N':
            variables['sat_path_kernel'] = raw_input('Give another kernel path: ')
            variables['sat_path_kernel'] = variables['sat_path_kernel'].rstrip()
            print "'" + variables['sat_path_kernel'] + "'"
        else:
            print 'Auto select ' + helper.color.BOLD + 'Target kernel path' + helper.color.END
            print '   Target kernel path = ' + helper.color.BOLD + variables['sat_path_kernel'] + helper.color.END


        print
        selection = raw_input("   Use kernel source path: '" + variables['sat_path_kernel_src'] + "' ? [Y/n] ")
        if selection == 'n' or selection == 'N':
            variables['sat_path_kernel_src'] = raw_input('Give another kernel source path: ')
            variables['sat_path_kernel_src'] = variables['sat_path_kernel_src'].rstrip()
            print "'" + variables['sat_path_kernel_src'] + "'"
        else:
            print 'Auto select ' + helper.color.BOLD + 'Target kernel source path' + helper.color.END
            print '   Target kernel source path = ' + helper.color.BOLD + variables['sat_path_kernel_src'] + helper.color.END
        print

        # SAT_PATH_MODULES
        if os.path.exists(os.path.join(variables['sat_target_source'], 'kernel', 'gmin')):
            if os.path.exists(os.path.join(variables['sat_target_source'], 'kernel', 'gmin-quilt-representation',
                                           'x86_64', 'modules')):
                variables['sat_path_modules'] = os.path.join(variables['sat_target_source'],
                                                             'kernel', 'gmin-quilt-representation',
                                                             'x86_64', 'modules')
            elif os.path.exists(os.path.join(variables['sat_target_source'],
                                             'kernel', 'gmin-quilt-representation', 'i386', 'modules')):
                variables['sat_path_modules'] = os.path.join(variables['sat_target_source'], 'kernel',
                                                             'gmin-quilt-representation', 'i386', 'modules')
        elif os.path.exists(os.path.join(variables['sat_target_build'], 'system', 'lib', 'modules')):
            variables['sat_path_modules'] = os.path.join(variables['sat_target_build'],
                                                         'system', 'lib', 'modules')
        elif os.path.exists(os.path.join(variables['sat_target_build'], 'root', 'lib', 'modules')):
            variables['sat_path_modules'] = os.path.join(variables['sat_target_build'],
                                                         'root', 'lib', 'modules')
        else:
            print "WARNING: Path of kernel modules not found!"

        selection = raw_input("   Use kernel modules path: '" + variables['sat_path_modules'] + "' ? [Y/n] ")
        if selection == 'n' or selection == 'N':
            variables['sat_path_modules'] = raw_input('Give another kernel modules path: ')
            variables['sat_path_modules'] = variables['sat_path_modules'].rstrip()
            print "'" + variables['sat_path_modules'] + "'"
        else:
            print 'Auto select ' + helper.color.BOLD + 'Target kernel modules path' + helper.color.END
            print '   Target kernel modules path = ' + helper.color.BOLD + variables['sat_path_modules'] + helper.color.END
        print

# ####################################
# Private methods
# ####################################

    def _get_build_info(self):
        self.debug_print("AndroidOs::_get_build_info")
        # Get build info from the trace
        build_info = {}

        build_info['brand'] = self._control.shell_command("getprop ro.product.brand").strip()
        build_info['name'] = self._control.shell_command("getprop ro.product.name").strip()
        build_info['device'] = self._control.shell_command("getprop ro.product.device").strip()
        build_info['android_v'] = self._control.shell_command("getprop ro.build.version.release").strip()

        build_info['prod_id'] = self._control.shell_command("getprop ro.build.id").strip()
        build_info['version'] = self._control.shell_command("getprop ro.build.version.incremental").strip()
        build_info['type'] = self._control.shell_command("getprop ro.build.type").strip()
        build_info['platform'] = self._control.shell_command("getprop ro.board.platform").strip()
        build_info['user'] = self._control.shell_command("getprop ro.build.user").strip()
        build_info['host'] = self._control.shell_command("getprop ro.build.host").strip()
        build_info['kernel_version'] = self._control.shell_command("uname -a").strip()

        pickle.dump(build_info, open(os.path.join(self._trace_path, "build_info.p"), "wb"), pickle.HIGHEST_PROTOCOL)
        print "Get build info from the device"

        # Create local build path
        self._helper.set_trace_folder(self._trace_path)
        self._local_build_path = os.path.join(self._sat_home_path, 'builds', self._helper.calculateTraceInfoHash())
        if not os.path.exists(self._local_build_path):
            os.makedirs(self._local_build_path)
        with open(os.path.join(self._local_build_path, 'build_info.txt'), 'w') as f:
            f.write(build_info['name'] + '\n')
            f.write(build_info['device'] + '\n')
            f.write(build_info['version'] + '\n')
            f.write(build_info['type'] + '\n')
            f.write(build_info['user'] + '\n')
            f.flush()
            f.close()

    def _dump_art_binaries(self):
        self.debug_print("AndroidOs::_dump_art_binaries")
        art_cache_paths = self._control.shell_command('ls /data/dalvik-cache | grep x86').split()

        md5_cmd = 'md5'
        if 'md5' not in self._control.shell_command('which ' + md5_cmd):
            md5_cmd = 'md5sum'

        for path in art_cache_paths:
            art_path = '/data/dalvik-cache/' + path + '/'
            dev_art_ls = self._control.shell_command('ls /' + art_path)
            if "No such file or directory" not in dev_art_ls:
                # ART binaries found
                print "\rUpdate ART binaries (" + path + "):   0%",
                sys.stdout.flush()

                art_path_components = art_path.split(os.path.sep)
                art_path_components.remove("")
                build_art_path = os.path.join(self._local_build_path, 'art', *art_path_components)
                if not os.path.exists(build_art_path):
                    os.makedirs(build_art_path)

                trace_art_path = os.path.join(self._trace_binary_path, 'art', *art_path_components)
                if not os.path.exists(trace_art_path):
                    os.makedirs(trace_art_path)

                # pickle checksum format:
                # {[checksum] = art_file_name}

                trace_checksum_path = os.path.join(trace_art_path, '.checksum')
                trace_checksums = {}

                index_count = 0
                art_file_amount = len(dev_art_ls.splitlines())
                for line in dev_art_ls.splitlines():
                    index_count += 1
                    if not line:
                        continue

                    # TODO create cross compatible shell_comman_api android, linux, chrome os etc
                    checksum_line = self._control.shell_command(md5_cmd + ' ' + art_path + line).strip()
                    match = re.search("([0-9a-fA-F]{32}) (.*)", checksum_line)
                    if match:
                        checksum = match.group(1)

                        art_target_file_with_checksum = os.path.join(build_art_path, line+'-'+str(checksum))
                        if not os.path.isfile(art_target_file_with_checksum):
                            self._control.get_remote_file(art_path + line, art_target_file_with_checksum)
                            if not os.path.isfile(art_target_file_with_checksum):
                                print "\rERROR: File fetching failed {0}".format(line)
                                continue

                        trace_checksums[checksum] = line
                        if sys.platform.startswith('win'):
                            shutil.copyfile(os.path.join(build_art_path, line+'-'+str(checksum)),
                                            os.path.join(trace_art_path, line))
                        else:
                            if os.path.exists(os.path.join(build_art_path, line+'-'+str(checksum))):
                                os.symlink(os.path.join(build_art_path, line+'-'+str(checksum)),
                                           os.path.join(trace_art_path, line))

                        print '\rUpdate ART binaries (' + path + '): ',
                        print str(index_count * 100 / art_file_amount).rjust(3, ' ') + '%',
                        sys.stdout.flush()

                pickle.dump(trace_checksums, open(trace_checksum_path, 'wb'), 2)
                print "\rUpdate ART binaries (" + path + "): 100%"

    def _get_screen_shot_worker(self):
        self.debug_print("AndroidOs::_get_screen_shot_worker")
        # Get screenshot from the phone
        print 'Get the screenshot from the device'
        self._control.shell_command('screencap -p /sdcard/screen.png')
        time.sleep(0.5)
        self._control.get_remote_file('/sdcard/screen.png', os.path.join(self._trace_path, 'screen.png'))
        self._control.shell_command('rm /sdcard/screen.png')

    def _get_screen_shot(self):
        self.debug_print("AndroidOs::_get_screen_shot")
        # Start get_screen_shot_worker as process
        sub_process = threading.Thread(target=self._get_screen_shot_worker)
        sub_process.start()
        # Wait for 10 seconds or until process finishes
        sub_process.join(5)

        if sub_process.is_alive():
            print "Warning: failed to get screenshot"
            # Terminate
            sub_process._Thread__stop()
            sub_process.join()
