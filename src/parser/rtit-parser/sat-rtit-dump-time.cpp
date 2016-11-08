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
#include "sat-rtit-workarounds.h"
#include <string>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>

using namespace sat;
using namespace std;

class rtit_stdin : public rtit_parser_input {
public:
        bool get_next(unsigned char& byte)
        {
            int c = fgetc(stdin);
            if (c != EOF) {
                byte = c;
                return true;
            } else {
                return false;
            }
        }

        bool bad() { return ferror(stdin); }
};

class rtit_dumper : public rtit_parser_output {
public:
        rtit_dumper(bool output_deltas) :
            // TODO: get initial values from command line
            tsc_(), mtc_(), rng_(3),
            deltas_(output_deltas), time_at_prev_mtc_(), offset_at_prev_mtc_(),
            overflows_since_prev_mtc_()
        {}

        void  t() { printf("%s T\n", dump(parsed_.pos)); }
        void nt() { printf("%sNT\n", dump(parsed_.pos)); }

        void fup_pge()
        {
            printf("%sFUP.PGE %" PRIx64 "\n",
                   dump(parsed_.pos), parsed_.fup.address);
        };
        void fup_pgd()
        {
            printf("%sFUP.PGD %" PRIx64 "\n",
                   dump(parsed_.pos), parsed_.fup.address);
            skip_to_psb();
        };
        void fup_buffer_overflow()
        {
            ++overflows_since_prev_mtc_;
            printf("%sBUFFER OVERFLOW %" PRIx64 "\n",
                   dump(parsed_.pos), parsed_.fup.address);
        };
        void fup_pcc()
        {
            printf("%sFUP.PCC %" PRIx64 "\n",
                   dump(parsed_.pos), parsed_.fup.address);
        };
        void tip()
        {
            printf("%sTIP %" PRIx64 "\n", dump(parsed_.pos), parsed_.fup.address);
        };
        void fup_far()
        {
            printf("%sFUP.FAR %" PRIx64 "\n",
                   dump(parsed_.pos), parsed_.fup.address);
        };

        void sts()
        {
            if (parsed_.sts.tsc < tsc_) {
                char s[256];
                snprintf(s, sizeof(s),
                         "STS stepping back in time %" PRIx64 " -> %" PRIx64 " (%" PRIu64 " steps)",
                         tsc_,
                         parsed_.sts.tsc,
                         tsc_ - parsed_.sts.tsc);
                warning(parsed_.pos, OTHER, s);
            }
            tsc_ = parsed_.sts.tsc;

            unsigned old_mtc = mtc_;
            unsigned new_mtc = (parsed_.sts.tsc >> (7 + 2 * rng_)) & 0xff;
            update_mtc(new_mtc);

            printf("%sSTS %u, %u, %" PRIx64 " (mtc: %u -> %u)\n",
                     dump(parsed_.pos),
                     parsed_.sts.acbr,
                     parsed_.sts.ecbr,
                     parsed_.sts.tsc,
                     old_mtc,
                     new_mtc);
        }
        void mtc()
        {
            unsigned rng = parsed_.mtc.rng;
            unsigned tsc = parsed_.mtc.tsc;

            // check for MTC wrapping around
            unsigned last_tsc = (tsc_ >> (7 + 2 * rng)) & 0xff;
            if (last_tsc > tsc) {
                // MTC tsc has wrapped around
                printf("--- MTC tsc wrapped around\n");
                tsc_ += 0x100 << (7 + 2 * rng);
            }

            tsc_ = (tsc_ & (0xffffffffffffffff << (15 + 2 * rng)))
                 | (tsc << (7 + 2 * rng));

            update_mtc(tsc);

            rng_ = rng;

            printf("%sMTC %u, %u\n", dump(parsed_.pos), rng, tsc);
            time_at_prev_mtc_         = tsc_;
            offset_at_prev_mtc_       = parsed_.pos.offset_;
            overflows_since_prev_mtc_ = 0;
        }
        virtual void pip()
        {
            printf("%sPIP %u, %" PRIx64 "\n",
                   dump(parsed_.pos),
                   (unsigned)parsed_.pip.cr0_pg,
                   parsed_.pip.cr3);
        }
        void tracestop()
            { printf("%sTraceSTOP\n", dump(parsed_.pos)); }
        void psb()
            { printf("%sPSB\n", dump(parsed_.pos)); }

        void ccp()
        {
            printf("%sCCP %u\n", dump(parsed_.pos), parsed_.ccp.cntp);
        }

        void warning(rtit_pos pos, REASON type, const char* text)
        {
            if (type == BROKEN_OVERFLOW) {
                ++overflows_since_prev_mtc_;
            }
            printf("%sWARNING: %s\n", dump(parsed_.pos), text);
        }

        void skip(rtit_pos pos, rtit_offset count)
            { printf("%08" PRIx64 ": SKIPPED %" PRId64 " bytes\n", parsed_.pos.offset_, count); }

private:
        void update_mtc(unsigned new_mtc)
        {
            // first see if we have dropped any MTCs
            unsigned diff;
            if (mtc_ > new_mtc) {
                diff = 256 - mtc_ + new_mtc;
            } else {
                diff = new_mtc - mtc_;
            }
            if (diff > 1) {
                printf("--- %u MTCs dropped\n", diff - 1);
            }

            mtc_ = new_mtc;
        }

        const char* dump(rtit_pos pos)
        {
            // space for offset + dump + nil
            static char d[256];

            if (deltas_) {
                sprintf(d, "%" PRId64 ",%" PRId64 ",%u,",
                        pos.offset_ - offset_at_prev_mtc_,
                        tsc_   - time_at_prev_mtc_,
                        overflows_since_prev_mtc_);
            } else {
                sprintf(d, "%" PRId64 "/%" PRIx64 ",%" PRId64 "/%" PRIx64 ",", pos.offset_, pos.offset_, tsc_, tsc_);
            }

            return d;
        }

        uint64_t    tsc_;
        unsigned    mtc_;
        unsigned    rng_; // MTC rng
        bool        deltas_;
        uint64_t    time_at_prev_mtc_;
        rtit_offset offset_at_prev_mtc_;
        unsigned    overflows_since_prev_mtc_;
};

int main(int argc, char* argv[])
{
    bool cycle_accurate    = false;
    bool output_deltas     = false;
    bool skip_to_first_psb = false;

    for (int i = 1; i < argc; ++i) {
        string arg(argv[i]);

        if (arg == "-c") {
            cycle_accurate = true;
        } else if (arg == "-d") {
            output_deltas = true;
        } else if (arg == "-p") {
            skip_to_first_psb = true;
        } else {
            fprintf(stderr, "Usage: %s [-c] [-d] [-p]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    shared_ptr<rtit_stdin>  input{new rtit_stdin};
    shared_ptr<rtit_dumper> output{new rtit_dumper(output_deltas)};
    rtit_parser< postpone_early_mtc<skip_after_overflow_with_compressed_lip>>
        parser(input, output);

    if (parser.parse(cycle_accurate, skip_to_first_psb)) {
        printf("OK\n");
        exit(EXIT_SUCCESS);
    } else {
        printf("ERROR\n");
        exit(EXIT_FAILURE);
    }
}
