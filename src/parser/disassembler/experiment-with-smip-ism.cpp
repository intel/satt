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
#include "sat-disassembler.h"
#include <ism3/ism_symbolmanager.hpp>
#include <ism3/ism_string_helpers.hpp>
#include <smip3/smip_provider.hpp>
#include <smip3/smip_ismwrapper.hpp>
#include <smip3/smip_address.hpp>

#include <cstdio>

using namespace std;

static void list_funcs(const string& binaryfile)
{
    ism3::ISymbolManagerPtr sm = 0L;
    if (ism3::ISymbolManager::create(sm) == ism3::status_Ok) {

        ism3::IModuleSymbolBankPtr module;
        if (sm->loadSymbolsFromModule(ISM3_A2U(binaryfile.c_str()),
                                      0,
                                      module) == ism3::status_Ok)
        {
            ism3::ISymbolRangeIteratorPtr sri;
            if (module->originalSymbolRanges(sri) != ism3::status_Ok) {
                cerr << "could not get symbol ranges for " << binaryfile << endl;
                return;
            }

            ism3::ISymbolRangePtr sr;
            sri->reset();
            while ((sr = sri->current())) {

                //ism3::ISymbol* s = sr->symbol();
                auto s = sr->symbol();

                printf("%s @ [0x%04llx..0x%04llx)\n",
                       s->functionName(), // vs. s->linkerName()
                       sr->beginAddr()->rva(),
                       sr->endAddr()->rva());

                ism3::ISourceLocationPtr sl = 0;
                if ((s->declarationSrcLocation(sl) == ism3::status_Ok)) {
                    printf("(%s:%d)\n",
                           sl->sourceFile()->sourceFileName(),
                           sl->lineNumber());
                } else {
                    printf("(no source)\n");
                }

                sri->next();
            }
        }
    }
}

#if LIST_PSEUDO_FUNCS
static void list_pseudo_funcs(const string& binaryfile)
{
    ism3::ISymbolManagerPtr sm = 0L;
    if (ism3::ISymbolManager::create(sm) == ism3::status_Ok) {

        ism3::IModuleSymbolBankPtr module;
        if (sm->loadSymbolsFromModule(ISM3_A2U(binaryfile.c_str()),
                                      0,
                                      module) == ism3::status_Ok)
        {
            smip3::IDataSource::Ptr ds =
                smip3::IISMDataSource::create(module,
                                              false,
                                              module->preferredLoadAddress());
            smip3::IAsmProviderPtr provider = smip3::IAsmProvider::create(ds);
            smip3::IFunctionNavigator::Ptr funcs = provider->functions();
            smip3::IFunctionPtr f;

            while (f = funcs->current()) {

                cout << f->name() << endl;

                funcs->next();
            }
        }
    }
}
#endif

static void list_code(const string& binaryfile)
{
    //smip3::IDataSource::Ptr ds = smip3::IISMDataSource::create(binaryfile.c_str());
    auto ds = smip3::IISMDataSource::create(binaryfile.c_str());
    if (!ds) {
        cerr << "could not create data source for " << binaryfile << endl;
        return;
    }

    //smip3::IAsmProviderPtr ap = smip3::IAsmProvider::create(ds);
    auto ap = smip3::IAsmProvider::create(ds);
    if (!ap) {
        cerr << "could not create asm provider for " << binaryfile << endl;
        return;
    }

    //smip3::IAsmInstIteratorPtr ii = ap->instructions();
    auto ii = ap->instructions();
    ii->reset();

    //smip3::IAsmInstPtr i;
    while (auto i = ii->current()) {
        smip3::IAddress::Ptr addr = i->startAddr();
        printf("0x%04llx", addr->pseudoAddr());
        for (unsigned s = 0; s < i->size(); ++s) {
            printf(" %02x", (int)(i->opcode())[s]);
        }
        for (unsigned s = i->size(); s < 8; ++s) {
            printf("   ");
        }
        printf(" %s %s\n", i->label(), i->instruction());

        ii->next();
    }
}

static void print_basic_block_info(smip3::IBasicBlock::Ptr bb)
{
    printf("%s, size %llu (@ %llx -> %llx)%s\n",
           bb->blockId(),
           bb->size(),
           bb->startAddr()->pseudoAddr(),
           bb->nextTarget()?bb->nextTarget()->pseudoAddr():0,
           bb->nextTarget()?"":"*");

    printf("- next address %llx\n",
           bb->nextTarget() ? bb->nextTarget()->pseudoAddr() :
                              bb->startAddr()->pseudoAddr() + bb->size());

    auto bt = bb->branchType();
    printf("- branch type %d\n", bt);
    printf("%s", bb->isCall() ? "- is call\n" : "");
    printf("%s", bb->isConditional() ? "- is conditional\n" : "");
    printf("%s", bb->isJump() && !bb->isCall() && !bb->isConditional() && bb->branchType() != smip3::btRet ? "- is jump\n" : "");
    printf("%s", bb->branchType() == smip3::btRet ? "- is return\n" : "");
}

static void list_basic_blocks(const string& binaryfile)
{
    ism3::ISymbolManagerPtr sm = 0L;
    if (ism3::ISymbolManager::create(sm) == ism3::status_Ok) {

        ism3::IModuleSymbolBankPtr module;
        if (sm->loadSymbolsFromModule(ISM3_A2U(binaryfile.c_str()),
                                      0,
                                      module) == ism3::status_Ok)
        {
            smip3::IDataSource::Ptr ds =
                smip3::IISMDataSource::create(module,
                                              false,
                                              module->preferredLoadAddress());
            smip3::IAsmProviderPtr provider = smip3::IAsmProvider::create(ds);
            smip3::IFunctionNavigator::Ptr funcs = provider->functions();
            smip3::IFunctionPtr f;

            while (f = funcs->current()) {
                printf("%s\n", f->name());

                auto bbn = f->basicBlocks();
                bbn->reset();

                while (auto bb = bbn->current()) {
                    print_basic_block_info(bb);
                    bbn->next();
                }

                funcs->next();
                printf("\n");
            }
        }
    }
}


int main(int argc, char* argv[])
{
    if (argc == 2) {
        cout << "functions:" << endl;
        list_funcs(argv[1]);

        cout << endl << "basic blocks:" << endl;
        list_basic_blocks(argv[1]);

        cout << endl << "code:" << endl;
        list_code(argv[1]);
    } else if (argc ==3) {
        using namespace sat;

        shared_ptr<disassembler> d(disassembler::obtain(argv[1], argv[1], 0));
        if (d) {
            string f;
            unsigned dummy;
            if (d->get_function(atoll(argv[2]), f, dummy)) {
                printf("%s\n", f.c_str());
            } else {
                fprintf(stderr, "could not get the function\n");
            }
        } else {
            fprintf(stderr, "could not obtain a disassembler\n");
        }
    }
}
