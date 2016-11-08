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
#include <string>
#include "sat-oat-header.h"
#include "sat-log.h"

namespace sat {

using namespace std;

struct PACKED(4) oat_header_043
{
    uint8_t        magic_[4];
    uint8_t        version_[4];
    uint32_t       adler32_checksum_;

    InstructionSet instruction_set_;
    uint32_t       instruction_set_features_bitmap_;
    uint32_t       dex_file_count_;
    uint32_t       executable_offset_;
    uint32_t       interpreter_to_interpreter_bridge_offset_;
    uint32_t       interpreter_to_compiled_code_bridge_offset_;
    uint32_t       jni_dlsym_lookup_offset_;
    uint32_t       portable_imt_conflict_trampoline_offset_;
    uint32_t       portable_resolution_trampoline_offset_;
    uint32_t       portable_to_interpreter_bridge_offset_;
    uint32_t       quick_generic_jni_trampoline_offset_;
    uint32_t       quick_imt_conflict_trampoline_offset_;
    uint32_t       quick_resolution_trampoline_offset_;
    uint32_t       quick_to_interpreter_bridge_offset_;
    int32_t        image_patch_delta_;
    uint32_t       image_file_location_oat_checksum_;
    uint32_t       image_file_location_oat_data_begin_;
    uint32_t       key_value_store_size_;
    uint8_t        key_value_store_[0];  // variable width data
}; // struct oat_header_043

struct PACKED(4) oat_header_064
{
    uint8_t        magic_[4];
    uint8_t        version_[4];
    uint32_t       adler32_checksum_;

    InstructionSet instruction_set_;
    uint32_t       instruction_set_features_bitmap_;
    uint32_t       dex_file_count_;
    uint32_t       executable_offset_;
    uint32_t       interpreter_to_interpreter_bridge_offset_;
    uint32_t       interpreter_to_compiled_code_bridge_offset_;
    uint32_t       jni_dlsym_lookup_offset_;
    uint32_t       quick_generic_jni_trampoline_offset_;
    uint32_t       quick_imt_conflict_trampoline_offset_;
    uint32_t       quick_resolution_trampoline_offset_;
    uint32_t       quick_to_interpreter_bridge_offset_;
    int32_t        image_patch_delta_;
    uint32_t       image_file_location_oat_checksum_;
    uint32_t       image_file_location_oat_data_begin_;
    uint32_t       key_value_store_size_;
    uint8_t        key_value_store_[0];  // variable width data
}; // struct oat_header_064

template <class OAT_HEADER>
class versioned_oat_header : public oat_header
{
public:
    versioned_oat_header(const uint8_t* buf) : header_((const OAT_HEADER*)buf) {}

    size_t size() const { return sizeof(OAT_HEADER); }
    uint32_t dex_file_count() const { return header_->dex_file_count_; }
    uint32_t key_value_store_size() const {
        return header_->key_value_store_size_;
    }

private:
    const OAT_HEADER* header_;
}; // versioned_at_header<>

// static
shared_ptr<oat_header> oat_header::create(const uint8_t* buf)
{
    std::string version((char *)&buf[4]);
    SAT_LOG(1, "VERSION =%s\n", version.c_str());
    if (std::stoi(version) < 64) {
        shared_ptr<oat_header> header(new versioned_oat_header<oat_header_043>(buf));
        return header;
    } else {
        shared_ptr<oat_header> header(new versioned_oat_header<oat_header_064>(buf));
        return header;
    }
}

} // namespace sat
