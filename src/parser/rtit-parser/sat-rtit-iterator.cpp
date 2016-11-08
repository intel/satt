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
#include "sat-rtit-iterator.h"
#include "sat-file-input.h"
#include "sat-rtit-pkt-cnt.h"
#include "sat-rtit-workarounds.h"
#include <memory>

namespace sat {


class rtit_iterator::impl
{
public:
    impl(const string& path) :
        path_(path)
    {}

    bool iterate(callback_func callback, callback_func_pkt_cnt callback_pc)
    {
        bool ok = true;

        typedef file_input<rtit_parser_input> rtit_input;
        shared_ptr<rtit_input> input{new rtit_input};
        if (!input->open(path_)) {
            ok = false;
        } else {
            shared_ptr<rtit_parser_output> output{new rtit_parser_output};
            rtit_parser_ = make_shared<parser_type>(input, output);
            rtit_parser_->policy.route_emit_to(this);
            callback_ = callback;
            callback_pc_ = callback_pc;
            rtit_parser_->parse();
        }

        return ok;
    }


    void emit(rtit_parser_output* output)
    {
        if(callback_)
            callback_(output->parsed_);
        else
            callback_pc_(output->parsed_, rtit_parser_->policy.rtit_pkt_cnt());
    }

private:
    string path_;

    typedef rtit_parser<
        postpone_early_mtc<pkt_cnt<route_emit<rtit_iterator::impl>>>
    > parser_type;
    shared_ptr<parser_type> rtit_parser_;
    callback_func           callback_;
    callback_func_pkt_cnt   callback_pc_;
}; // class rtit_iterator::impl


rtit_iterator::rtit_iterator(const string& path) :
    pimpl_{new impl(path)}
{
}

rtit_iterator::~rtit_iterator()
{
}

bool rtit_iterator::iterate(callback_func callback)
{
    return pimpl_->iterate(callback,  NULL);
}

bool rtit_iterator::iterate(callback_func_pkt_cnt callback)
{
    return pimpl_->iterate(NULL, callback);
}

} // namespace sat
