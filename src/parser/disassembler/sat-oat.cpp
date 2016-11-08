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
#include "sat-oat.h"
#include "sat-types.h"
#include "sat-oat-header.h"
#include "sat-log.h"
#include <string>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <cstdlib>

namespace sat {

using namespace std;
using namespace ELFIO;

bool get_symbol(const symbol_section_accessor& symtab,
                const string&                  name,
                oat_symbol&                    symbol)
{
    unsigned char bind  = 0;
    unsigned char type  = STT_NOTYPE;
    unsigned char other;

    return symtab.get_symbol(name,
                             symbol.offset,
                             symbol.size,
                             bind,
                             type,
                             symbol.section_index,
                             other);
}

bool get_oat_info(istream&    mmapped_file,
                  rva&        default_load_address,
                  rva&        text_section_offset,
                  size_t&     text_section_size,
                  unsigned&   bits,
                  oat_symbol& oatdata,
                  oat_symbol& oatexec,
                  oat_symbol& oatlastword,
                  string&     version)
{
    bool got_info = false;

    elfio elf;
    if (elf.load(mmapped_file)) {
        section* dynsym = elf.sections[".dynsym"];
        if (dynsym) {
            const symbol_section_accessor symtab(elf, dynsym);
            if (get_symbol(symtab, "oatdata", oatdata) &&
                get_symbol(symtab, "oatexec", oatexec) &&
                get_symbol(symtab, "oatlastword", oatlastword))
            {
                section* rodata = elf.sections[oatdata.section_index];
                if (rodata && rodata->get_size() >= 8) { // 8 == magic & version
                    // TODO: access mmapped file directly instead of get_data()
                    const char* data = rodata->get_data();
                    if (data &&
                        data[0] == 'o' && data[1] == 'a' && data[2] == 't')
                    {
                        // got oat magic; now get oat version
                        char v[4] = { data[4], data[5], data[6], '\0' };
                        version = v;

                        oatdata.file_offset = rodata->get_address() -
                                              oatdata.offset +
                                              get_elfio_section_offset(rodata);
                        section* text = elf.sections[oatexec.section_index];
                        if (text) {
                            text_section_offset = get_elfio_section_offset(text);
                            default_load_address = text->get_address() -
                                                   text_section_offset;
                            text_section_size   = text->get_size();
                            bits = elf.get_class() == ELFCLASS32 ? 32 : 64;
                            oatexec.file_offset =
                                text->get_address() -
                                oatexec.offset +
                                text_section_offset;
                            oatlastword.file_offset =
                                oatexec.file_offset +
                                (oatlastword.offset - oatexec.offset);
                            got_info = true;
                        }
                    }
                }
            }
        }
    }

    return got_info;
}

struct dex_header {
    uint8_t  magic_[8];
    uint32_t checksum_;
    uint8_t  signature_[20];
    uint32_t file_size_;       // size of entire file
    uint32_t header_size_;     // offset to start of next section
    uint32_t endian_tag_;
    uint32_t link_size_;
    uint32_t link_off_;
    uint32_t map_off_;
    uint32_t string_ids_size_; // number of StringIds
    uint32_t string_ids_off_;  // file offset of StringIds array
    uint32_t type_ids_size_;   // number of TypeIds
    uint32_t type_ids_off_;    // file offset of TypeIds array
    uint32_t proto_ids_size_;  // number of ProtoIds
    uint32_t proto_ids_off_;   // file offset of ProtoIds array
    uint32_t field_ids_size_;  // number of FieldIds
    uint32_t field_ids_off_;   // file offset of FieldIds array
    uint32_t method_ids_size_; // number of MethodIds
    uint32_t method_ids_off_;  // file offset of MethodIds array
    uint32_t class_defs_size_; // number of ClassDefs
    uint32_t class_defs_off_;  // file offset of ClassDef array
    uint32_t data_size_;
    uint32_t data_off_;
};

enum oat_class_type {
    all_compiled  = 0,
    some_compiled = 1,
    none_compiled = 2
};

struct string_id {
    uint32_t string_data_off_;  // offset in bytes from the base address
};

struct type_id {
    uint32_t descriptor_idx_;
};

struct proto_id {
    uint32_t shorty_idx_;  // index into string_ids array for shorty descriptor
    uint16_t return_type_idx_; // index into type_ids array for return type
    uint16_t pad_;             // padding = 0
    uint32_t parameters_off_;  // file offset to type_list for parameter types
};

struct method_id {
    uint16_t class_idx_; // index into type_ids_ array for defining class
    uint16_t proto_idx_; // index into proto_ids_ array for method prototype
    uint32_t name_idx_;  // index into string_ids_ array for method name
};

struct class_def {
    uint16_t class_idx_;         // index into type_ids_ array for this class
    uint16_t pad1_;              // padding = 0
    uint32_t access_flags_;
    uint16_t superclass_idx_;    // index into type_ids_ array for superclass
    uint16_t pad2_;              // padding = 0
    uint32_t interfaces_off_;    // file offset to TypeList
    uint32_t source_file_idx_;   // index into string_ids_ for source file name
    uint32_t annotations_off_;   // file offset to annotations_directory_item
    uint32_t class_data_off_;    // file offset to class_data_item
    uint32_t static_values_off_; // file offset to EncodedArray
};

struct oat_method_offsets {
    uint32_t code_offset_;
    uint32_t gc_map_offset_;
};

struct class_data_header {
    uint32_t static_fields_size_;   // the number of static fields
    uint32_t instance_fields_size_; // the number of instance fields
    uint32_t direct_methods_size_;  // the number of direct methods
    uint32_t virtual_methods_size_; // the number of virtual methods
};

struct class_data_method {
    uint32_t method_idx_delta_; // delta of index into the method_ids array
                                // for MethodId
    uint32_t access_flags_;
    uint32_t code_off_;
};

static inline uint32_t decode_unsigned_leb128(const uint8_t** data) {
    const uint8_t* ptr = *data;
    int result = *(ptr++);
    if (result > 0x7f) {
        int cur = *(ptr++);
        result = (result & 0x7f) | ((cur & 0x7f) << 7);
        if (cur > 0x7f) {
            cur = *(ptr++);
            result |= (cur & 0x7f) << 14;
            if (cur > 0x7f) {
                cur = *(ptr++);
                result |= (cur & 0x7f) << 21;
                if (cur > 0x7f) {
                    cur = *(ptr++);
                    result |= cur << 28;
                }
            }
        }
    }
    *data = ptr;
    return static_cast<uint32_t>(result);
}

typedef function<void(rva /*offset*/, size_t /*size*/, const string& /*name*/)>
        oat_method_callback;

void iterate_class_methods(const uint8_t*             oat_data,
                           uint32_t                   class_offset,
                           uint32_t                   count,
                           uint32_t&                  current,
                           function<string(uint32_t)> name,
                           const uint8_t**            class_data,
                           oat_method_callback        callback)
{
    SAT_LOG(2, "class offset: %x\n", class_offset);
    const uint8_t* p = oat_data + class_offset + sizeof(uint16_t);
    uint16_t type = *(uint16_t*)p;
    SAT_LOG(2, "class type: %x\n", (uint32_t)type);
    p += sizeof(type);

    const oat_method_offsets* methods;
    if (type == all_compiled) {
        methods     = (const oat_method_offsets*)p;
    } else if (type == some_compiled) {
        uint32_t bitmap_size = *(uint32_t*)p;
        p += sizeof(bitmap_size);
        p += bitmap_size;
        methods = (const oat_method_offsets*)p;
    } else {
        methods     = 0;
        SAT_LOG(2, "No methods\n");
    }

    if (methods) {
        uint32_t idx = 0;
        for (unsigned i = 0; i < count; ++i) {
            class_data_method cdm;
            cdm.method_idx_delta_ = decode_unsigned_leb128(class_data);
            cdm.access_flags_ = decode_unsigned_leb128(class_data);
            cdm.code_off_ = decode_unsigned_leb128(class_data);

            idx += cdm.method_idx_delta_;
            SAT_LOG(2, "i: %u idx: %u\n", i, idx);

            if (type == all_compiled) {
                SAT_LOG(2, "method %u offset: %x %s\n",
                        i, methods[current].code_offset_, name(idx).c_str());
                callback(methods[current].code_offset_,
                         0,
                         name(idx).c_str());
            } else if (type == some_compiled) {
                // TODO: add support if needed
            } else {
                // do nothing
            }
            ++current;
        }
    }
}


const uint8_t* iterate_dex_methods(const uint8_t*      oat_data,
                                   const uint8_t*      dex_data,
                                   int                 version,
                                   oat_method_callback callback)
{
    const uint8_t* p = dex_data;

    uint32_t dex_file_location_size = *(const uint32_t*)p;
    p += sizeof(dex_file_location_size);
    SAT_LOG(1, "dex_file_location_size: 0x%04x\n", dex_file_location_size);
    if (dex_file_location_size > 256)
        SAT_LOG(1, "WARNING: suspicious big size value!!\n");

    SAT_LOG(1, "dex file location: %*.*s\n",
            dex_file_location_size, dex_file_location_size, p);
    p += dex_file_location_size;

    uint32_t dex_file_checksum = *(const uint32_t*)p;
    SAT_LOG(1, "dex file checksum: %x\n", dex_file_checksum);
    p += sizeof(dex_file_checksum);

    uint32_t dex_file_offset = *(const uint32_t*)p;
    SAT_LOG(1, "dex file offset: %x\n", dex_file_offset);
    p += sizeof(dex_file_offset);

    const uint8_t* base = oat_data + dex_file_offset;
    const dex_header* header = (const dex_header*)base;
    SAT_LOG(1, "dex header magic: %s\n", header->magic_);

    const uint32_t* methods_offsets_pointer;
    if (version >= 79) {
        SAT_LOG(2, "dex methods_offsets_pointer: %x\n", *((uint32_t*) p));
        methods_offsets_pointer = (const uint32_t*)(oat_data + *((uint32_t*) p));
    } else {
        methods_offsets_pointer = (const uint32_t*)p;
    }

    const string_id* strings =
        (const string_id*)(base + header->string_ids_off_);
    const type_id* types =
        (const type_id*)(base + header->type_ids_off_);
#if 0 // we don't need protos
    const proto_id* protos =
        (const proto_id*)(base + header->proto_ids_off_);
#endif
    const method_id* methods =
        (const method_id*)(base + header->method_ids_off_);
    const class_def* classes =
        (const class_def*)(base + header->class_defs_off_);

    SAT_LOG(1, "class defs: %u\n", header->class_defs_size_);
    for (unsigned i = 0; i < header->class_defs_size_; ++i) {
        SAT_LOG(2, "class idx: %x\n", (uint32_t)classes[i].class_idx_);
        uint32_t descriptor = types[classes[i].class_idx_].descriptor_idx_;
        const string_id& str = strings[descriptor];
        // type descriptor format is "'L' FullClassName ';'
        const uint8_t* class_mutf = base + str.string_data_off_;
        uint32_t class_mutf_len = decode_unsigned_leb128(&class_mutf);
        if (class_mutf_len > 0) {
            ++class_mutf; // skip the 'L'
        }
        SAT_LOG(2, "class name: %s\n", (const char*)(class_mutf));
        SAT_LOG(2, "class descriptor: %x\n", descriptor);
        if (classes[i].class_data_off_) {
            string class_name = (const char*)class_mutf;
            if (class_name.length() > 0 && class_name.back() == ';') {
                // remove the ';'
                class_name = class_name.substr(0, class_name.length() - 1);
            }

            const uint8_t* class_data = base + classes[i].class_data_off_;
            class_data_header cdh;
            cdh.static_fields_size_ = decode_unsigned_leb128(&class_data);
            cdh.instance_fields_size_ = decode_unsigned_leb128(&class_data);
            cdh.direct_methods_size_ = decode_unsigned_leb128(&class_data);
            cdh.virtual_methods_size_ = decode_unsigned_leb128(&class_data);

            // skip field data to get to the beginning of method data
            for (uint32_t f = 0;
                 f < cdh.static_fields_size_ + cdh.instance_fields_size_;
                 ++f)
            {
                (void)decode_unsigned_leb128(&class_data);
                (void)decode_unsigned_leb128(&class_data);
            }

            auto make_name = [&](uint32_t idx) {
                const method_id& method = methods[idx];
                const string_id& str = strings[method.name_idx_];
                const uint8_t* method_mutf = base + str.string_data_off_;
                (void)decode_unsigned_leb128(&method_mutf);
                return class_name + "." + (const char*)method_mutf;
            };

            SAT_LOG(2, "direct methods %u, virtual methods %u\n",
                    cdh.direct_methods_size_, cdh.virtual_methods_size_);
            uint32_t m = 0;
            iterate_class_methods(
                oat_data,
                methods_offsets_pointer[i],
                cdh.direct_methods_size_,
                m,
                make_name,
                &class_data,
                callback);
            iterate_class_methods(
                oat_data,
                methods_offsets_pointer[i],
                cdh.virtual_methods_size_,
                m,
                make_name,
                &class_data,
                callback);
        }
    }

    SAT_LOG(1, "method ids: %u\n", header->method_ids_size_);
    for (unsigned i = 0; i < header->method_ids_size_; ++i) {
        const method_id& method = methods[i];
        //const proto_id& proto = protos[method.proto_idx_];
        const string_id& str = strings[method.name_idx_];
        const char* method_name = (const char*)(base + str.string_data_off_ + 1);
        SAT_LOG(3, "%u: %s\n", i, method_name);
    }

    if (version >= 79) {
        p += sizeof(methods_offsets_pointer);
    } else {
        p += sizeof(*methods_offsets_pointer) * header->class_defs_size_;
    }

    return p;
}


void iterate_oat_methods(const uint8_t* oat_data, int version, oat_method_callback callback)
{
    const uint8_t* p = oat_data;

    shared_ptr<oat_header> header = oat_header::create(p);

    SAT_LOG(1, "dex files: %u\n", header->dex_file_count());
    SAT_LOG(1, "key value storage size: %x\n",
           header->key_value_store_size());
    p += header->size();

    SAT_LOG(1, "first key: %s\n", p);
    p += header->key_value_store_size();

    for (unsigned i = 0; i < header->dex_file_count(); ++i) {
        SAT_LOG(1, "dex file %d:\n", i);
        fflush(stdout);

        p = iterate_dex_methods(oat_data, p, version, callback);
    }
}

} // namespace sat
