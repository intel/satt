#!/usr/bin/python
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

from satparser import *
from collections import defaultdict
import sys
import os
if sys.platform.startswith('win'):
    import msvcrt




class Enum(set):
    def __getattr__(self, name):
        if name in self:
            return name
        raise AttributeError


class sideband_binary_dumper(sideband_parser_output):

    lines_ = {}

    def __init__(self, sb_lines):
        self.lines_ = sb_lines

    def header_and_binary(self, header, binary):
        if header['tscp'] in self.lines_.keys():
            print "WARNING: duplicate timestamps in sideband streams!!!"
            while True:
                header['tscp'] += 1
                if header['tscp'] not in self.lines_.keys():
                    break;
        self.lines_[header['tscp']] = binary

class sideband_filein(sideband_parser_input):
    # c:\ type mydoc.txt | python.exe -u myscript.py
    eof_ = False
    bad_ = False
    input_file_ = None

    def __init__(self, input_file):
        self.input_file_ = open(input_file, 'rb')

    def read(self, size):
        try:
            buffer = self.input_file_.read(size)
            if not buffer:
                self.eof_ = True
                return False
            return buffer
        except:
            self.bad_ = True
            return False

    def eof(self):
        return self.eof_

    def bad(self):
        # return ferror(stdin)
        return self.bad_


class sideband_combiner:

    input_paths_ = []
    output_path_ = ''
    sb_lines_ = {}

    def __init__(self, input_paths, output_path):
        self.input_paths_ = input_paths
        self.output_path_ = output_path

    def combine(self):
        for path in self.input_paths_:
            sb_input = sideband_filein(path)
            sb_output = sideband_binary_dumper(self.sb_lines_)
            parser = sideband_parser(sb_input, sb_output)
            parsing_ok = parser.parse()

        f = open(self.input_paths_[0], 'rb')
        sat_ver_data = f.read(12)
        f.close()

        outf = open(self.output_path_, 'wb')
        outf.write(sat_ver_data)
        for i in sorted(self.sb_lines_.keys()):
            outf.write(self.sb_lines_[i])
        outf.close()
