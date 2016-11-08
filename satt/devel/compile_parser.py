#!/usr/bin/env python
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

import os
import sys

print "***********************************"
print "**** compile update SAT-parser ****"
print "***********************************"
old_dir = os.getcwd()
sat_home = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', '..'))
os.chdir(os.path.join(sat_home, 'src', 'parser', 'post-processor'))
print 'compile...'
clear_flag = ''
if len(sys.argv) > 1:
    if sys.argv[1] == '-c' or sys.argv[1] == '-C':
        os.system('scons -c')
os.system('scons flags="-static -I ' + sat_home +
          '/src/parser/capstone-master/binaries/include -L ' + sat_home +
          '/src/parser/capstone-master/binaries/lib -L ' + sat_home +
          '/src/parser/capstone-master/binaries/lib64"')
# os.system('scons flags="-static -I ' + sat_home +
#          '/src/parser/udis86-master/binaries/include -L ' + sat_home +
#          '/src/parser/udis86-master/binaries/lib"')
os.chdir(os.path.join(sat_home, 'satt', 'process'))
os.system('tar xzf ' + sat_home + '/src/parser/post-processor/sat-post-processor-binaries.tgz')

os.chdir(old_dir)
print 'done.'
