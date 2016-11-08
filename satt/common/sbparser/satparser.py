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

import struct
import sys

# SAT Sideband version number
SAT_SB_VERSION = "SATSIDEB0004"

SAT_MSG_PROCESS = 1
SAT_MSG_MMAP = 2
SAT_MSG_MUNMAP = 3
SAT_MSG_INIT = 4
SAT_MSG_PROCESS_EXIT = 5
SAT_MSG_SCHEDULE = 6
SAT_MSG_HOOK = 7
SAT_MSG_MODULE = 8
SAT_MSG_PROCESS_ABI2 = 9
SAT_MSG_MMAP_ABI2 = 10
SAT_MSG_MUNMAP_ABI2 = 11
SAT_MSG_HOOK_ABI2 = 12
SAT_MSG_MODULE_ABI2 = 13
SAT_MSG_HOOK_ABI3 = 14
SAT_MSG_GENERIC = 15
SAT_MSG_CODEDUMP = 16
SAT_MSG_SCHEDULE_ABI2 = 17
SAT_MSG_SCHEDULE_ABI3 = 18
SAT_MSG_SCHEDULE_ID = 19
SAT_MSG_INIT_ABI2 = 20

class sideband_parser_output:
    def init(self, header, pid, tid, tsc_tick, fsb_mhz, tma_ratio_tsc, tma_ratio_ctc, mtc_freq):
        return

    def process(self, header, origin, pid, ppid, tgid, pgd, name):
        return

    def mmap(self, header, origin, pid, start, leng, pgoff, path):
        return

    def munmap(self, header, origin, pid, start, leng):
        return

    def schedule(self, header, prev_pid, prev_tid, pid, tid, ipt_pkt_count, ipt_pkt_mask, buff_offset, schedule_id):
        return

    def schedule_id(self, header, addr, id):
        return

    def hook(self, header, original_address, new_address, size, wrapper_address, name):
        return

    def module(self, header, address, size, name):
        return

    def generic(self, header, name, data):
        return

    def codedump(self, header, addr, size, name):
        return

    def header_and_binary(self, header, binary):
        return


class sideband_parser_input:
  def read(self, size, buffer):
    print "Hello"
  def eof():
    print "Hello"
  def bad():
    print "Hello"

class sideband_parser:
  input_        = None
  output_       = None
  first_packet_ = True

  def __init__(self, input, output):
    self.input_ = input
    self.output_ = output


  def parse(self):
    ok = False

    ok = self.parse_message()
    while ok and not self.input_.eof():
      ok = self.parse_message()

    return ok and not self.input_.bad()

  def pack_header(self, msg_size, header_type, tscp, cpu, tsc_offset):
    header = {}
    header['size'] = msg_size
    header['header_type'] = header_type
    header['tscp'] = tscp
    header['cpu'] = cpu
    header['tsc_offset'] = tsc_offset
    return header

  def parse_message(self):
    ok = True
    VERSION_STRING_SIZE = 12
    HEADER_SIZE = 4
    HEADER_MESSAGE_SIZE = 24
    HEADER_TYPE_SIZE = 4
    SAT_MAX_PATH = 160
    MESSAGE_SIZE = 288

    msg = self.input_.read(HEADER_SIZE)
    binary_msg = msg
    # SB Version Check
    if self.first_packet_:
      self.first_packet_ = False
      if msg == "SATS":
        version = self.input_.read(VERSION_STRING_SIZE - HEADER_SIZE)
        #version = struct.unpack('<8s', msg)[0]
        sb_version = msg + version
        if int(sb_version[-4:]) > int(SAT_SB_VERSION[-4:]):
          print "ERROR: SB version is bigger {0} > {1} SB parser version".format(int(sb_version[-4:]), int(SAT_SB_VERSION[-4:]))
          sys.exit(-10)
        return ok

    if msg:
      msg_size = struct.unpack('<I', msg)[0]
      if msg_size == 0:
        ok = False
        print "broken header: zero message size"
      elif msg_size < HEADER_MESSAGE_SIZE:
        ok = False
        print "broken header: message size too short"
      elif msg_size > MESSAGE_SIZE:
        ok = False
        print "broken header: message size too long"
      else:
        header_type = self.input_.read(HEADER_TYPE_SIZE)
        binary_msg += header_type
        size = msg_size - HEADER_SIZE - HEADER_TYPE_SIZE
        msg = self.input_.read(size)
        binary_msg += msg
        if header_type and msg:
            header_type = struct.unpack('<I', header_type)[0]
            if header_type == SAT_MSG_PROCESS:
              fmt = '<QIIIiiiI'
              name_size = size - struct.calcsize(fmt)
              tscp, cpu, tsc_offset, origin, pid, ppid, tgid, pgd, name = struct.unpack(fmt + str(name_size) + 's', msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.process(header, origin, pid, ppid, tgid, pgd, name[:-1])

            elif header_type == SAT_MSG_MMAP:
              fmt = '<QIIIiIII'
              path_size = size - struct.calcsize(fmt)
              tscp, cpu, tsc_offset, origin, pid, start, leng, pgoff, path = struct.unpack(fmt + str(path_size) + 's', msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.mmap(header, origin, pid, start, leng, pgoff, path[:-1])

            elif header_type == SAT_MSG_MUNMAP:
              fmt = '<QIIIiII'
              tscp, cpu, tsc_offset, origin, pid, start, leng = struct.unpack(fmt, msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.munmap(header, origin, pid, start, leng)

            elif header_type == SAT_MSG_INIT:
              fmt = '<QIIiiII'
              tscp, cpu, tsc_offset, pid, tgid, tsc_tick, fsb_mhz = struct.unpack(fmt, msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.init(header, tgid, pid, tsc_tick, fsb_mhz, 0, 0, 0)

            elif header_type == SAT_MSG_INIT_ABI2:
              fmt = '<QIIiiIIIIB'
              tscp, cpu, tsc_offset, pid, tgid, tsc_tick, fsb_mhz, tma_ratio_tsc, tma_ratio_ctc, mtc_freq = struct.unpack(fmt, msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.init(header, tgid, pid, tsc_tick, fsb_mhz, tma_ratio_tsc, tma_ratio_ctc, mtc_freq)

            elif header_type == SAT_MSG_SCHEDULE:
              fmt = '<QIIiiiiI'
              tscp, cpu, tsc_offset, pid, tgid, prev_pid, prev_tgid, ipt_pkt_count = struct.unpack(fmt, msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              ipt_count = (ipt_pkt_count >> 16) & 0x3
              self.output_.header_and_binary(header, binary_msg)
              self.output_.schedule(header, prev_tgid, prev_pid, tgid, pid, ipt_pkt_count & 0x3fff, ipt_count, 0, 0)

            elif header_type == SAT_MSG_HOOK:
              fmt = '<QIIIII'
              name_size = size - struct.calcsize(fmt)
              tscp, cpu, tsc_offset, org_addr, new_addr, size_, name = struct.unpack(fmt + str(name_size) + 's', msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.hook(header, org_addr, new_addr, size_, 0, name[:-1])

            elif header_type == SAT_MSG_MODULE:
              fmt = '<QIII'
              name_size = size - struct.calcsize(fmt)
              tscp, cpu, tsc_offset, addr, name = struct.unpack(fmt + str(name_size) + 's', msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.module(header, addr, name[:-1])

            elif header_type == SAT_MSG_PROCESS_ABI2:
              fmt = '<QIIIiiiQ'
              name_size = size - struct.calcsize(fmt)
              tscp, cpu, tsc_offset, origin, pid, ppid, tgid, pgd, name = struct.unpack(fmt + str(name_size) + 's', msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.process(header, origin, pid, ppid, tgid, pgd, name[:-1])

            elif header_type == SAT_MSG_MMAP_ABI2:
              fmt = '<QIIIiQQQ'
              path_size = size - struct.calcsize(fmt)
              tscp, cpu, tsc_offset, origin, tgid, start, leng, pgoff, path = struct.unpack(fmt + str(path_size) + 's', msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.mmap(header, origin, tgid, start, leng, pgoff, path[:-1])

            elif header_type == SAT_MSG_MUNMAP_ABI2:
              fmt = '<QIIIiQQ'
              tscp, cpu, tsc_offset, origin, tgid, start, leng = struct.unpack(fmt, msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.munmap(header, origin, tgid, start, leng)

            elif header_type == SAT_MSG_HOOK_ABI2:
              fmt = '<QIIQQQ'
              name_size = size - struct.calcsize(fmt)
              tscp, cpu, tsc_offset, org_addr, new_addr, size_, name = struct.unpack(fmt + str(name_size) + 's', msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.hook(header, org_addr, new_addr, size_, 0, name[:-1])

            elif header_type == SAT_MSG_MODULE_ABI2:
              fmt = '<QIIQQ'
              name_size = size - struct.calcsize(fmt)
              tscp, cpu, tsc_offset, addr, size, name = struct.unpack(fmt + str(name_size) + 's', msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.module(header, addr, size, name[:-1])

            elif header_type == SAT_MSG_HOOK_ABI3:
              fmt = '<QIIQQQQ'
              name_size = size - struct.calcsize(fmt)
              tscp, cpu, tsc_offset, org_addr, new_addr, size_, wrapper_addr, name = struct.unpack(fmt + str(name_size) + 's', msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.hook(header, org_addr, new_addr, size_, wrapper_addr, name[:-1])

            elif header_type == SAT_MSG_GENERIC:
              fmt = '<QII5s256s'
              tscp, cpu, tsc_offset, gen_name, data = struct.unpack(fmt, msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.generic(header, gen_name, data)

            elif header_type == SAT_MSG_CODEDUMP:
              fmt = '<QIIQQ'
              name_size = size - struct.calcsize(fmt)
              tscp, cpu, tsc_offset, addr, size_, name = struct.unpack(fmt + str(name_size) + 's', msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.codedump(header, addr, size_, name[:-1])

            elif header_type == SAT_MSG_SCHEDULE_ABI2:
              fmt = '<QIIiiiiIQ'
              tscp, cpu, tsc_offset, pid, tgid, prev_pid, prev_tgid, ipt_pkt_count, buff_offset = struct.unpack(fmt, msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              ipt_count = (ipt_pkt_count >> 16) & 0x3
              self.output_.header_and_binary(header, binary_msg)
              self.output_.schedule(header, prev_tgid, prev_pid, tgid, pid, ipt_pkt_count & 0x3fff, ipt_count, buff_offset, 0)

            elif header_type == SAT_MSG_SCHEDULE_ABI3:
              fmt = '<QIIiiiiIQB'
              tscp, cpu, tsc_offset, pid, tgid, prev_pid, prev_tgid, ipt_pkt_count, buff_offset, shedule_id = struct.unpack(fmt, msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              ipt_count = (ipt_pkt_count >> 16) & 0x3
              self.output_.header_and_binary(header, binary_msg)
              self.output_.schedule(header, prev_tgid, prev_pid, tgid, pid, ipt_pkt_count & 0x3fff, ipt_count, buff_offset, shedule_id)

            elif header_type == SAT_MSG_SCHEDULE_ID:
              fmt = '<QIIQB'
              tscp, cpu, tsc_offset, addr, id = struct.unpack(fmt, msg)
              header = self.pack_header(msg_size, header_type, tscp, cpu, tsc_offset)
              self.output_.header_and_binary(header, binary_msg)
              self.output_.schedule_id(header,addr, id)

            else:
              print "broken header: unknown message type %u\n" % header_type
              ok = False

    else:
        ok = self.input_.eof()

    return ok
