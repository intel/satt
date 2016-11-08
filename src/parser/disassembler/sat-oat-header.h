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
#ifndef SAT_OAT_HEADER_H
#define SAT_OAT_HEADER_H

#include <memory>
#include <cstdint>

#define PACKED(x) __attribute__ ((__aligned__(x), __packed__))

namespace sat{

enum InstructionSet {
    kNone,
    kArm,
    kArm64,
    kThumb2,
    kX86,
    kX86_64,
    kMips,
    kMips64
};

class oat_header
{
public:
    static std::shared_ptr<oat_header> create(const uint8_t* buf);

    virtual size_t   size()                 const = 0;
    virtual uint32_t dex_file_count()       const = 0;
    virtual uint32_t key_value_store_size() const = 0;

protected:
    oat_header() {}

private:
    oat_header(const oat_header&);
};

} // namespace sat

#endif // SAT_OAT_HEADER_H
