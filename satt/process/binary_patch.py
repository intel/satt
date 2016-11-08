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

import subprocess
import os
import sys
import shutil
import glob
import platform
from elftools.common.py3compat import bytes2str
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from satt.common import envstore


class BinaryPatch:

    sat_home = ''
    sat_target_build = ''
    sideband_dump_bin = ''
    kernel_modules_path = ''
    kernel_module_target_path = ''
    build_binary_path = ''
    dump_file_folder = ''
    kmod_dump_file_folder = ''

    def __init__(self, target_build, trace_folder):
        # Set Paths
        # GET Environment variables

        self.sat_home = envstore.store.get_sat_home()
        self.sat_target_build = target_build

        self.sideband_dump_bin = os.path.join(self.sat_home, 'satt', 'process', 'bin')
        # self.kernel_modules_path = os.environ.get('SAT_PATH_MODULES')
        self.trace_folder = trace_folder
        self.build_binary_path = os.path.join(self.trace_folder, "binaries")
        self.kernel_module_target_path = os.path.join(self.build_binary_path, "kernel", "modules")
        self.dump_file_folder = os.path.join(self.build_binary_path, "dump")
        self.kmod_dump_file_folder = os.path.join(self.dump_file_folder, "kmod")

    def createPatchingDir(self):
        #
        # Empty and Create Directory for pateched kernel modules
        #
        if not os.path.isdir(self.kernel_module_target_path):
            os.mkdir(self.kernel_module_target_path)

    def getSatModuleFromDevice(self):
        #
        # Get sat.ko module from the device to the KERNEL_MODULE_PATH
        #
        # TODO: check sat.ko from /lib/modules also
        sat_module_paths = ['/data/sat.ko', '/lib/modules/sat.ko']
        curdir = os.getcwd()
        os.chdir(self.trace_folder)
        if not os.path.isfile(os.path.join(self.trace_folder, 'sat.ko')):
            # Get sat.ko from trace folder if exists
            p = os.popen('adb shell "cat /sys/module/sat/parameters/sat_path"')
            line = p.readline().rstrip()
            p.close()
            if line.find('sat.ko') >= 0:
                print 'Fetch sat.ko from ' + line
                p = os.popen('adb pull "' + line + ' ' + self.trace_folder + '"')
                p.close()
            else:
                for mpath in sat_module_paths:
                    p = os.popen('adb shell "ls ' + mpath + '"')
                    line = p.readline().rstrip()
                    p.close()
                    if not line.find('No such file') >= 0:
                        print 'Fetch sat.ko from ' + line + ' (3)'
                        p = os.popen('adb pull ' + line + ' ' + self.trace_folder)
                        break
        os.chdir(curdir)

    def getModulesLists(self, dump_file_folder):
        # Get list of module dumps
        dump_to_km = {}
        curdir = os.getcwd()
        os.chdir(self.kmod_dump_file_folder)
        module_dumps = glob.glob("*.dump")
        os.chdir(self.kernel_module_target_path)
        kernel_modules = glob.glob("*.ko_")
        #print "KErnel modules"
        #print kernel_modules
        #print "++++++++++++++"
        # heuristic search of module names
        search_methods = [{'normalize': False, 'compare': 0},
                          {'normalize': True, 'compare': 0},
                          {'normalize': False, 'compare': 1},
                          {'normalize': True, 'compare': 1},
                          {'normalize': False, 'compare': 2},
                          {'normalize': True, 'compare': 2}]

        for dump_orig in module_dumps:
            found = False
            dump = dump_orig.lower()
            for method in search_methods:
                if found:
                    break
                for km in kernel_modules:
                    #print dump_orig + " <-> " + km
                    if method['normalize']:
                        km_name = km.lower().replace('-', '_')[:-4]
                    else:
                        km_name = km[:-4]

                    if ((method['compare'] == 0 and dump[:-5] == km_name) or
                        (method['compare'] == 1 and km_name.find(dump[:-5], 1) >= 0) or
                       (method['compare'] == 2 and km_name.find(dump[:-5]) >= 0)):
                            dump_to_km[dump_orig] = km
                            found = True
                            break

        os.chdir(curdir)
        #print "dump_to_km:"
        #print dump_to_km
        #print "-----------"
        return dump_to_km

    def getModulesLists_OLDONE(self, dump_file_folder):
        # Get list of modules from self.kernel_modules_path
        dump_to_km = {}
        curdir = os.getcwd()
        os.chdir(self.kernel_modules_path)
        kernel_modules = glob.glob("*.ko")
        for km in kernel_modules:
            dump_filename = km.lower().replace('-', '_')[:-3] + '.dump'
            if os.path.isfile(os.path.join(dump_file_folder, dump_filename)):
                dump_to_km[dump_filename] = km
        os.chdir(curdir)
        # add sat.ko file into dictionary
        dump_to_km["sat.dump"] = "sat.ko"
        return dump_to_km

    def patchedModulesExists(self):
        if os.path.isdir(self.kernel_module_target_path):
            return True
        return False

    def patchFile(self, dumpfile, targetfile):
        func_name = ''
        func_addr = {}
        func_size = {}
        file_offset = {}
        text_section_file_offset = 0
        text_section_code_offset = 0
        err = ''

        # Browse Elf header
        try:
            # print "Target file: " + targetfile
            elffile = ELFFile(targetfile)
            for section in elffile.iter_sections():
                if section.name == '.text':
                    text_section_file_offset = section['sh_offset']
                    text_section_code_offset = section['sh_addr']
                    text_section_code_size = section['sh_size']
                    continue
                if not isinstance(section, SymbolTableSection):
                    # Continue if not section does not contain symbols
                    continue
                if section['sh_entsize'] == 0:
                    err = 'Symbol table "%s" has a sh_entsize of zero!' % (
                            bytes2str(section.name))
                    continue
            # Patch file
            dumpfile.seek(0)
            patch_code = dumpfile.read(text_section_code_size)
            targetfile.seek(text_section_file_offset)
            targetfile.write(patch_code)
        except Exception as e:
            return 'Error: ' + str(e)

        return err

    def patchModules(self, official, no_patch_if_already_patched, ignore_sat_module=False):
        err = ""
        # Check if build is official and linked modules already exist
        # if so, we are good to return
        # DEPRECATED not needed anymore
        # if (official or no_patch_if_already_patched) and self.patchedModulesExists():
        #     return

        # Make a copy of vmlinux for patching.
        # In case patching is not done, the copy is needed anyway because
        #  model script uses the processed copy.

        kernelfilepath = os.path.join(self.build_binary_path, 'kernel', 'vmlinux_')
        if os.path.isfile(kernelfilepath):

            if not os.path.isfile(os.path.join(self.dump_file_folder, 'kernel_dump')):
                print 'kernel_dump not found'
                err = 'no_dump'
                return err

            # Check if SAT module fetch is needed
            # if not official and not ignore_sat_module:
            #    self.getSatModuleFromDevice()

            print 'Patching vmlinux'
            dumpfile = open(os.path.join(self.dump_file_folder, 'kernel_dump'), 'rb')
            targetfile = open(kernelfilepath, 'r+b')
            err = self.patchFile(dumpfile, targetfile).strip()
            dumpfile.close()
            targetfile.close()

            if err != '':
                print 'File vmlinux: ' + err
                sys.exit(1)
            else:
                # rename vmlinux_ file to vmlinux
                os.rename(kernelfilepath, kernelfilepath[:-1])

        elif not os.path.isfile(kernelfilepath[:-1]):
            print 'ERROR: Original vmlinux_ or patched vmlinux not found!'
            sys.exit(1)

        if not os.path.isdir(self.kmod_dump_file_folder):
                print 'kmod dump folder not found'
                err = 'no_dump'
                return err

        index_count = 0
        dump_to_km = self.getModulesLists(self.kmod_dump_file_folder)
        module_count = len(dump_to_km)
        if module_count > 0:
            print 'Patch kernel modules:'
            for dump, module in dump_to_km.items():
                index_count += 1
                print '\rProcessing: ' + str(index_count * 100 / module_count).rjust(3, ' ') + '%',
                sys.stdout.flush()
                targetfilepath = os.path.join(self.kernel_module_target_path, module)

                dumpfile = open(os.path.join(self.kmod_dump_file_folder, dump), 'rb')
                targetfile = open(targetfilepath, 'r+b')
                err = self.patchFile(dumpfile, targetfile)
                dumpfile.close()
                targetfile.close()

                if err != '':
                    print 'File ' + module + ': ' + err
                    sys.exit(1)
                else:
                    os.rename(targetfilepath, targetfilepath[:-1])

            print 'Patching done'
            targetfilepath = os.path.join(self.kernel_module_target_path, "*.ko_")
            for ko_ in glob.glob(targetfilepath):
	            os.remove(ko_)
        return err
