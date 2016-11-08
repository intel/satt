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


class LinkModules:

    sat_home = ''
    trace_folder = ''
    sat_target_build = ''
    sideband_dump_bin = ''
    linux_kernel_path = ''
    kernel_modules_path = ''
    kernel_module_target_path = ''

    def __init__(self, target_build, trace_folder):
        # Set Paths
        # GET Environment variables
        self.sat_home = os.environ.get('SAT_HOME')
        self.sat_target_build = target_build
        self.trace_folder = trace_folder
        self.sideband_dump_bin = os.path.join(self.sat_home,"bin","bin","sat-sideband-dump")
        print "self.sideband_dump_bin = " + self.sideband_dump_bin
        self.linux_kernel_path = os.environ.get('SAT_PATH_KERNEL')
        self.kernel_modules_path = os.environ.get('SAT_PATH_MODULES')
        print "self.sat_target_build = " + self.sat_target_build
        print "os.environ.get('SAT_PATH_MODULES') = " + os.environ.get('SAT_PATH_MODULES')
        print "self.linux_kernel_path = " + self.linux_kernel_path
        print "self.kernel_modules_path = " + self.kernel_modules_path
        self.kernel_module_target_path = os.path.join(self.trace_folder,"binaries","ld-modules")

    def getModulesFromSb(self, trace_folder):
        # Get module info from Sideband
        p = subprocess.Popen(self.sideband_dump_bin + ' < ' + trace_folder + '/sideband.bin',
                             shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        modules = {}
        for line in iter(p.stdout.readline, ''):
            if "module" in line:
                row = line.split()
                modules[row[5]] = row[4][:-1]
        p.wait()

        return modules

    def createLinkingDir(self):
      #
      # Empty and Create Directory for linked kernel modules
      #
        if os.path.isdir(self.kernel_module_target_path):
            shutil.rmtree(self.kernel_module_target_path)
        os.mkdir(self.kernel_module_target_path)

    def getSatModuleFromDevice(self):
        #
        # Get sat.ko module from the device to the KERNEL_MODULE_PATH
        #
        curdir = os.getcwd()
        os.chdir(self.kernel_modules_path)
        subprocess.call(["adb", "pull", "/data/sat.ko"])

        if os.path.isfile(self.kernel_modules_path + "sat.ko"):
            if os.path.isfile(self.sat_home + "/kernel-module/sat.ko"):
                shutil.copy(self.sat_home + "/kernel-module/sat.ko", self.kernel_modules_path)
        os.chdir(curdir)

    def getModulesLists(self):
        # Get list of modules from self.kernel_modules_path
        modules_in_fs = {}
        curdir = os.getcwd()
        os.chdir(self.kernel_modules_path)
        kernel_modules = glob.glob("*.ko")
        for km in kernel_modules:
            modules_in_fs[km.lower().replace('-', '_')[:-3]] = km
        os.chdir(curdir)

        return kernel_modules, modules_in_fs

    def createSystemMapLd(self):
        #
        # Create system.map.ld for the linking
        #
        system_map_ld_file = open(self.linux_kernel_path + "system.map.ld", "w")
        curdir = os.getcwd()
        os.chdir(self.linux_kernel_path)
        systen_map_file = open(self.linux_kernel_path + "System.map")
        for line in systen_map_file:
            items = line.split()
            system_map_ld_file.write("--defsym=" + items[2] + "=0x" + items[0] + "\n")
        systen_map_file.close()
        system_map_ld_file.close()
        os.chdir(curdir)

    def linkedModulesExists(self):
        if os.path.isdir(self.kernel_module_target_path):
            return True
        return False

    def linkModules(self, official, ignore_sat_module=False):

        # Check if build is official and linked modules already exist
        # if so, we are good to return
        if official and self.linkedModulesExists():
            return

        self.createLinkingDir()

        # Check if SAT module fetch is needed
        if not official and not ignore_sat_module:
            self.getSatModuleFromDevice()

        modules = self.getModulesFromSb(self.trace_folder)

        #
        # Get Architecture from the vmlinux or first found kernel modules
        #
        kernel_modules, modules_in_fs = self.getModulesLists()

        arch = '64bit'
        if os.path.isfile(self.linux_kernel_path + "vmlinux"):
            arch = platform.architecture(self.linux_kernel_path + 'vmlinux')[0]
        elif os.path.isfile(self.kernel_modules_path + kernel_modules[0]):
            arch = platform.architecture(self.kernel_modules_path + kernel_modules[0])[0]

        self.createSystemMapLd()

        #
        # Link all the modules
        #
        curdir = os.getcwd()
        os.chdir(self.linux_kernel_path)
        ko_link_output = curdir + "/" + self.trace_folder + "/ko-link-output.log"
        print "Link kernel modules:"
        print "(Linker output written into '" + ko_link_output + "')"
        if os.path.isfile(ko_link_output):
            os.system("rm " + ko_link_output + " > /dev/null")
        index_count = 0
        module_count = len(modules)
        for module, addr in modules.items():
            index_count += 1
            print "\rProcessing: " + str(index_count * 100 / module_count).rjust(3," ") + "%",
            sys.stdout.flush()
            if ignore_sat_module and module == "sat":
                continue
            if module in modules_in_fs.keys():
                os.system("echo '\n============================================================================' >> " + ko_link_output + " 2>&1")
                os.system("echo '" + modules_in_fs[module] + " : " + module + " = " + addr + "' >> " + ko_link_output + " 2>&1")
                os.system("echo '============================================================================' >> " + ko_link_output + " 2>&1")
                if arch == '64bit':
                    os.system("ld -static -z muldefs -m elf_x86_64 @system.map.ld --oformat=elf64-x86-64 --section-start=.text=" +
                        addr + " " + self.kernel_modules_path + modules_in_fs[module] + " -o " + self.kernel_module_target_path +
                        modules_in_fs[module] + " >> " + ko_link_output + " 2>&1")
                else:
                    os.system("ld -static -z muldefs -m elf_i386 @system.map.ld --oformat=elf32-i386 --section-start=.text=" +
                        addr + " " + self.kernel_modules_path + modules_in_fs[module] + " -o " + self.kernel_module_target_path +
                        modules_in_fs[module] + " >> " + ko_link_output + " 2>&1")
        os.chdir(curdir)
        print ""
