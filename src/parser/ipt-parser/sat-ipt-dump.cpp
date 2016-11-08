/*
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
*/
#include "sat-input.h"
#include "sat-ipt-parser.h"
#include <cstdio>
#include <cinttypes>
#include <algorithm>

#define DUMP_COLUMNS 8
using namespace std;
using namespace sat;

template <class INPUT>
class dump_ipt
{
public:
    using token = ipt_parser_token<dump_ipt>;

    dump_ipt(const INPUT& input) : input_(input) {}

    void output_packet(uint8_t packet[])
    {
        ipt_pos pos = input_.beginning_of_packet();
        printf("%08lx|", pos);
        int bytes = input_.index() - pos;
        int max_column = min(bytes, DUMP_COLUMNS);
        int column     = 0;

        for (; column < max_column; ++column) {
            printf(" %02x", packet[column]);
        }
        if (bytes > max_column) {
            printf("\n                           ");
            column = 0;
            for (; column < max_column; ++column) {
                printf(" %02x", packet[column + max_column]);
            }
        }
        for (; column < DUMP_COLUMNS; ++column) {
            printf("   ");
        }
        printf("  ");
    }

    void short_tnt(token& t)
    {
        printf("short tnt");
        output_tnt(t);
        printf("\n");
    }

    void long_tnt(token& t)
    {
        printf("long tnt");
        output_tnt(t);
        printf("\n");
    }

    void tip(token& t)
    {
        printf("tip %" PRIx64 "\n", t.tip);
    }

    void tip_pge(token& t)
    {
        printf("tip.pge %" PRIx64 "\n", t.tip);
    }

    void tip_pgd(token& t)
    {
        printf("tip.pgd %" PRIx64 "\n", t.tip);
    }

    void fup(token& t)
    {
        printf("fup %" PRIx64 "\n", t.tip);
    }

    void pip(token& t)
    {
        printf("pip %08" PRIx64 " %sroot\n", t.pip.cr3, t.pip.nr ? "non" : "");
    }

    void tsc(token& t)
    {
        printf("tsc %08" PRIx64 "\n", t.tsc);
    }

    void mtc(token& t)
    {
        static uint8_t prev = 0;
        if (((prev + 1) & 0xff) != t.ctc) {
            printf("SKIP ");
        }
        prev = t.ctc;
        printf("mtc %02x\n", (unsigned)t.ctc);
    }

    void mode_exec(token& t)
    {
        const char* modes[] = { "16", "64", "32" };
        printf("mode.exec %s-bit\n", modes[t.tsx]);
    }

    void mode_tsx(token& t)
    {
        const char* modes[] = { "out", "in", "abort" };
        printf("mode.tsx %s\n", modes[t.tsx]);
    }

    void tracestop(token& t)
    {
        printf("tracestop\n");
    }

    void cbr(token& t)
    {
        printf("cbr %02x\n", (unsigned)t.cbr);
    }

    void tma(token& t)
    {
        printf("tma %04x, %03x\n", (unsigned)t.tma.ctc, (unsigned)t.tma.fast);
    }

    void cyc(token& t)
    {
        printf("cyc\n");
    }

    void vmcs(token& t)
    {
        printf("VMCS %" PRIx64 "\n", t.vmcs_base_address);
    }

    void ovf(token& t)
    {
        printf("OVERFLOW\n");
    }

    void psb(token& t)
    {
        printf("psb\n");
    }

    void psbend(token& t)
    {
        printf("psbend\n");
    }

    void mnt(token& t)
    {
        printf("mnt\n");
    }

    void pad(token& t)
    {
        printf("pad\n");
    }

    void eof(token& t)
    {
        printf("EOF\n");
    }

    void report_warning(const string& message)
    {
        printf("%lxh: WARNING: %s in packet starting at %lxh\n",
               input_.index() - 1,
               message.c_str(),
               input_.beginning_of_packet());
    }

    void report_error(const string& message)
    {
        printf("%lxh: ERROR: %s in packet starting at %lxh\n",
               input_.index() - 1,
               message.c_str(),
               input_.beginning_of_packet());
    }

    void set_ff_state(bool state)
    {
        state = false;
    }

private:
    void output_tnt(const token& t)
    {
        auto mask = t.tnt.mask;
        while (mask) {
            printf(" %s", (t.tnt.bits & mask) ? "T" : "N");
            mask >>= 1;
        }
    }

    const INPUT& input_;
};

#if 0
template <class INPUT, class OUTPUT, class SCANNER, class EVALUATOR>
class prefix_with_packet_offset :
    public ipt_call_output_method<INPUT, OUTPUT, SCANNER, EVALUATOR>
{
    using super = ipt_call_output_method<INPUT, OUTPUT, SCANNER, EVALUATOR>;

public:
    prefix_with_packet_offset(const INPUT& input) : super(input) {}

    void output_lexeme(typename super::lexeme_func lexeme, uint8_t packet[])
    {
        printf("%08lx: ", super::input_.beginning_of_packet());
    }
};
#endif

int main(int argc, char* argv[])
{
    if (argc != 2) {
        printf("Usage: %s [ipt-file]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    ipt_parser<> dummy_parser;
    dummy_parser.parse();

    ipt_parser<input_from_file, dump_ipt/*, prefix_with_packet_offset*/> parser;

    if (argc == 2 && !parser.input().open(argv[1])) {
        fprintf(stderr, "cannot open '%s' for reading\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    while (parser.parse()) {}
}
