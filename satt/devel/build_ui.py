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

# http_proxy_server = 'http://proxy.example.com:888/'
http_proxy_server = ''

print "***********************************"
print "****       Build SATT UI       ****"
print "***********************************"
old_dir = os.getcwd()

sat_home = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', '..'))

os.chdir(os.path.join(sat_home, 'src', 'ui'))

if http_proxy_server != '':
    if os.environ.get('HTTP_PROXY') is None:
        os.putenv('HTTP_PROXY', http_proxy_server)
    if os.environ.get('HTTPS_PROXY') is None:
        os.putenv('HTTPS_PROXY', http_proxy_server)
    if os.environ.get('http_proxy') is None:
        os.putenv('http_proxy', http_proxy_server)
    if os.environ.get('https_proxy') is None:
        os.putenv('https_proxy', http_proxy_server)

print 'npm install...'
os.system('npm install')

print 'bower install...'
os.system('./node_modules/bower/bin/bower install')

print 'gulp build ui...'
os.system('./node_modules/gulp/bin/gulp.js build')

os.chdir(old_dir)
print 'done.'
