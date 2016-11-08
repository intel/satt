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
#include "sat-ipt-iterator.h"

namespace sat {

using namespace std;


template <class INPUT, class OUTPUT, class SCANNER, class EVALUATOR>
class call_callback : public ipt_call_output_method<INPUT,
                                                    OUTPUT,
                                                    SCANNER,
                                                    EVALUATOR>
{
    using super = ipt_call_output_method<INPUT, OUTPUT, SCANNER, EVALUATOR>;

public:
    call_callback(const INPUT& input, OUTPUT& output) : super(input, output)
    {}

    void output_token(ipt_parser_token<OUTPUT>& token)
    {
        callback_(token, super::input_.beginning_of_packet());
    }

    void output_lexeme(typename super::lexeme_func lexeme, uint8_t packet[])
    {}

    ipt_iterator::callback_func callback_;
};


class ipt_iterator::imp {
public:
    imp(const string& path) : path_(path) {}

    string path_;
}; // ipt_iterator::imp


ipt_iterator::ipt_iterator(const string& path) :
    imp_{new imp(path)}
{
}

ipt_iterator::~ipt_iterator()
{
}

bool ipt_iterator::iterate(callback_func callback)
{
    bool done = false;

    ipt_parser<input_from_file, ipt_token_output, call_callback> parser;
    if (parser.input().open(imp_->path_)) {
        parser.policy().callback_ = callback;
        while (parser.parse()) {}
    }

    return done;
}

} // sat
