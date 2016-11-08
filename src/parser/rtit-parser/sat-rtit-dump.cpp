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
#include "sat-rtit-parser.h"
#include "sat-rtit-pkt-cnt.h"
#include "sat-rtit-tsc-heuristics.h"
#include "sat-rtit-workarounds.h"
#include "sat-file-input.h"
#include <string>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <functional>

using namespace sat;
using namespace std;

function<rtit_offset(void)> rtit_pkt_cnt;
function<bool(rtit_pos, uint64_t& /*tsc*/, uint64_t& /*next*/)> get_tscs;

class dumping_input_base {
public:
        const char* dump(rtit_pos pos)
        {
            // space for  offset  + dump         + nil + extra for span
            static char d[31      + 3 * max_dump + 1   + 6];

            uint64_t tsc;
            uint64_t next_tsc;
            if (!get_tscs || !get_tscs(pos, tsc, next_tsc)) {
                tsc = next_tsc = 0;
            }
            uint64_t span = next_tsc - tsc;
            int o = sprintf(d, "%08" PRIx64 ":%04" PRIx64 "|%010" PRIx64 ":%04" PRIx64 "|",
                            pos.offset_,
                            rtit_pkt_cnt ? rtit_pkt_cnt() : 0,
                            tsc,
                            span);
            unsigned i;
            for (i = 0; i < dump_size % max_dump; ++i) {
                sprintf(&d[o + i * 3], " %02x", dump_buffer[i]);
            }
            for (; i < max_dump; ++i) {
                sprintf(&d[o + i * 3], "   ");
            }
            dump_size = 0;

            return d;
        }

protected:
        static const unsigned max_dump = 10;
        unsigned char dump_buffer[max_dump];
        unsigned dump_size;
};

template <class INPUT>
class dumping_input : public INPUT, public dumping_input_base {
public:
        bool get_next(unsigned char& byte)
        {
            if (INPUT::get_next(byte)) {
                dump_buffer[dump_size++ % max_dump] = byte;
                return true;
            } else {
                return false;
            }
        }
};

class rtit_dumper : public rtit_parser_output {
public:
        rtit_dumper(shared_ptr<dumping_input_base> input) : input_(input) {}

        void  t() { printf("%s  T\n", input_->dump(parsed_.pos)); }
        void nt() { printf("%s NT\n", input_->dump(parsed_.pos)); }

        void fup_pge()
        {
            printf("%s FUP.PGE %" PRIx64 "\n",
                   input_->dump(parsed_.pos), parsed_.fup.address);
        };
        void fup_pgd()
        {
            printf("%s FUP.PGD %" PRIx64 "\n",
                   input_->dump(parsed_.pos), parsed_.fup.address);
            // Following comments copied from sat-rtit-model.cpp:
            // 2013-06-07: it seems that we are sometimes getting garbage
            // between a FUP.PGD and a PSB, so skip until the PSB.
            // (Alas, it seems that sometimes there is quite a lot of
            // data before the PSB, so we might end up skipping a lot.)
            // 2015-01-29: garbage no longer seen; comment out skipping

            //skip_to_psb();
        };
        void fup_buffer_overflow() {
            printf("%s BUFFER OVERFLOW %" PRIx64 "\n",
                   input_->dump(parsed_.pos), parsed_.fup.address);
        };
        void fup_pcc() {
            printf("%s FUP.PCC %" PRIx64 "\n",
                   input_->dump(parsed_.pos), parsed_.fup.address);
        };
        void tip() {
            printf("%s TIP %" PRIx64 "\n",
                   input_->dump(parsed_.pos), parsed_.fup.address);
        };
        void fup_far() {
            printf("%s FUP.FAR %" PRIx64 "\n",
                   input_->dump(parsed_.pos), parsed_.fup.address);
        };

        void sts() {
            printf("%s STS %u, %u, %" PRIx64 "\n",
                   input_->dump(parsed_.pos),
                   parsed_.sts.acbr,
                   parsed_.sts.ecbr,
                   parsed_.sts.tsc); }
        void mtc() {
            printf("%s MTC %u, %u\n",
                   input_->dump(parsed_.pos),
                   parsed_.mtc.rng,
                   parsed_.mtc.tsc);
        }
        virtual void pip()
        {
            printf("%s PIP %u, %" PRIx64 "\n",
                   input_->dump(parsed_.pos),
                   (unsigned)parsed_.pip.cr0_pg,
                   parsed_.pip.cr3);
        }
        void tracestop()
            { printf("%s TraceSTOP\n", input_->dump(parsed_.pos)); }
        void psb()
            { printf("%s PSB\n", input_->dump(parsed_.pos)); }

        void ccp() {
            printf("%s CCP %u\n",
                   input_->dump(parsed_.pos), parsed_.ccp.cntp);
        }

        void warning(rtit_pos pos, REASON type, const char* text)
            { printf("%s WARNING: %s\n", input_->dump(pos), text); }

        void skip(rtit_pos pos, rtit_offset count)
            { printf("%08" PRIx64 ": SKIPPED %" PRId64 " bytes\n", pos.offset_, count); }

private:
        shared_ptr<dumping_input_base> input_;
};

class rtit_discarder : public rtit_parser_output {
public:
        rtit_discarder(shared_ptr<rtit_dumper> dump) : dump_(dump) {}

        void  t()                  { prefix(); dump_-> t(); }
        void nt()                  { prefix(); dump_->nt(); }

        void fup_pge()             { prefix(); dump_->fup_pge(); }
        void fup_pgd()             { prefix(); dump_->fup_pgd(); }
        void fup_buffer_overflow() { prefix(); dump_->fup_buffer_overflow(); }
        void fup_pcc()             { prefix(); dump_->fup_pcc(); }
        void tip()                 { prefix(); dump_->tip(); }
        void fup_far()             { prefix(); dump_->fup_far(); }

        void sts()                 { prefix(); dump_->sts(); }
        void mtc()                 { prefix(); dump_->mtc(); }
        void pip()                 { prefix(); dump_->pip(); }
        void tracestop()           { prefix(); dump_->tracestop(); }
        void psb()                 { prefix(); dump_->psb(); }

        void ccp()                 { prefix(); dump_->ccp(); }

        void warning(rtit_pos pos, REASON type, const char* text)
            { prefix(); dump_->warning(pos, type, text); }

        void skip(rtit_pos pos, rtit_offset count)
            { prefix(); dump_->skip(pos, count); }

private:
        void prefix() { printf("SKIP "); }

        shared_ptr<rtit_dumper> dump_;
};

void usage(char* name)
{
    fprintf(stdout, "Usage: %s [-c] [-p] [-v] <rtit-file>\n", name);
}


int main(int argc, char* argv[])
{
    bool     cycle_accurate     = false;
    bool     skip_to_first_psb  = false;
    bool     raw                = false;
    unsigned verbosity          = 0;
    string   input_path;

    for (int i = 1; i < argc; ++i) {
        string arg(argv[i]);

        if (arg == "-c") {
            cycle_accurate = true;
        } else if (arg == "-p") {
            skip_to_first_psb = true;
        } else if (arg == "-r") {
            raw = true;
        } else if (arg == "-v") {
            ++verbosity;
        } else if (input_path == "") {
            input_path = arg;
        } else {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (input_path == "") {
        fprintf(stderr, "must specify input file\n");
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    typedef rtit_parser<
                postpone_early_mtc<
                synthesize_dropped_mtcs<
                pkt_cnt<
                skip_after_overflow_with_compressed_lip
            >>>> parser_type;

    rtit_parser_base* parser;

    // set up the input
    typedef dumping_input<file_input<rtit_parser_input>> input_type;
    shared_ptr<input_type>     input{new input_type};
    if (!input->open(input_path)) {
        fprintf(stderr, "cannot open '%s' for reading\n",
                input_path.c_str());
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    // set up outputs
    shared_ptr<rtit_dumper>    output{new rtit_dumper(input)};
    shared_ptr<rtit_discarder> discard;
    if (verbosity != 0) {
        discard = make_shared<rtit_discarder>(output);
    }

    if (raw) {
        parser = new rtit_parser<>(input, output, discard);
    } else {
        parser_type* parser_with_heuristics;

        // take a pass over the rtit data to analyze timestamps
        shared_ptr<tsc_heuristics> tscs{new tsc_heuristics};
        tscs->parse_rtit(input_path);
        tscs->apply();

        // set up the parser
        parser_with_heuristics = new parser_type(input, output, discard);

        rtit_pkt_cnt =
            [parser_with_heuristics]() -> rtit_offset {
                return parser_with_heuristics->policy.rtit_pkt_cnt();
            };
        get_tscs =
            [tscs](rtit_pos pos, uint64_t& tsc, uint64_t& next_tsc) {
                return tscs->get_tsc(pos, tsc, next_tsc);
            };

        parser = parser_with_heuristics;
    }

    // do the dirty deed
    printf("OFFSET:PKTCNT|TSC:SPAN\n" \
           "----------------------\n");
    if (parser->parse(cycle_accurate, skip_to_first_psb)) {
        printf("OK\n");
        exit(EXIT_SUCCESS);
    } else {
        printf("ERROR\n");
        exit(EXIT_FAILURE);
    }
}
