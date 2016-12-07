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
#include "sat-mmapped.h"
#include "sat-oat.h"
#include "sat-log.h"
#include "sat-elfio-hack.h"
#include <capstone/capstone.h>
#include <elfio/elfio.hpp>
#include <elfio/elf_types.hpp>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <map>
#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>
#include <cinttypes>

namespace sat {

using namespace ELFIO;
using namespace std;

class section_cache
{
public:
    void add(rva address, rva target_address, size_t size, const string& name, rva flags)
    {
        if (address != 0 && size != 0) {
            if (cache_[address].size < size) {
                cache_[address] = {target_address, size, name, flags};
            }
        }
    }

    void iterate(function<void(rva           address,
                               size_t        size,
                               const string& name)> callback)
    {
        for (auto& section : cache_) {
            callback(section.first, section.second.size, section.second.name);
        }
    }

    void iterate_executables(function<void(
                                     rva           /*offset*/,
                                     rva           /*target_address*/,
                                     size_t        /*size*/,
                                     const string& /*name*/)> callback)
    {
        for (auto& section : cache_) {
            if (section.second.flags & SHF_EXECINSTR) {
                callback(section.first, section.second.target_address, section.second.size, section.second.name);
            }
        }
    }

    bool find(rva address, string& name) const
    {
        bool found = false;
        for (auto& section : cache_) {
            if (section.first <= address &&
                section.first + section.second.size > address)
            {
                name = section.second.name;
                found = true;
                break;
            }
        }
        return found;
    }

    bool find(rva address, rva& start, size_t& size, string& name) const
    {
        bool found = false;
        for (auto& section : cache_) {
            if (section.first <= address &&
                section.first + section.second.size > address)
            {
                start = section.first;
                size  = section.second.size;
                name  = section.second.name;
                found = true;
                break;
            }
        }
        return found;
    }

    bool find(rva func_offset, rva& target_address) const
    {
        bool found = false;
        for (auto& section : cache_) {
            if (section.second.flags & SHF_EXECINSTR) {
                if (section.first <= func_offset &&
                    section.first + section.second.size > func_offset)
                {
                    target_address = (section.second.target_address - section.first) + func_offset;
                    found = true;
                    break;
                }
            }
        }
        return found;
    }

private:
    typedef struct section { rva target_address; size_t size; string name; rva flags; } section;

    map<rva /*address*/, section> cache_;
};

class object_cache
{
public:
    object_cache() {}

    void add(const section_cache& sections,
             rva                  address,
             size_t               size,
             const string&        name)
    {
        rva    section_start;
        size_t section_size;
        string section_name;
        if (sections.find(address, section_start, section_size, section_name)) {
            // Function sizes reported by libraries are not always accurate;
            // hence we need to resort to some trickery here.
            // For now, set the size of the object to cover everything
            // from the beginning of the object to the end of the section.
            // Then, later in fix_sizes(), truncate sizes so that none of
            // the objects overlap.
            cache_[address] = {section_size - (address - section_start), name};
        }
    }

    void fix_sizes()
    {
        // Iterate through the cache, fixing object sizes.
        // add() has set each object's size so that the object covers memory
        // from the beginning of the object to the end of the section.
        // Now truncate them so that each object ends where the next one
        // starts. The last object will still end at the enf of its section.
        // We do this because some libraries do funky stuff, like have
        // nested objects or code between objects. This solution is crude,
        // but at least gives us a name for all code.
        auto next = cache_.begin();
        if (next != cache_.end()) {
            auto prev = next;
            ++next;
            while (next != cache_.end()) {
                if (prev->second.size > (next->first - prev->first)) {
                    prev->second.size = next->first - prev->first;
                }
#if 1
                SAT_LOG(2, "FIX SIZE: %" PRIx64 ", %" PRIx64 " %s\n",
                        prev->first, prev->second.size, prev->second.name.c_str());
#endif
                prev = next;
                ++next;
            }
        }
    }

    bool get_cached(rva       offset,
                    bool&     valid,
                    string&   name,
                    unsigned& offset_in_object) const
    {
        bool found = false;

        auto i = cache_.upper_bound(offset);
        if (i != cache_.begin()) {
            --i;
            if (i->first == offset && !i->second.size) {
                // unknown symbols have been cached with size 0
                valid = false;
                found = true;
            } else if (i->first + i->second.size > offset) {
                name             = i->second.name;
                offset_in_object = offset - i->first;
                valid            = true;
                found            = true;
            }
#ifndef NO_SILLY_HEURISTICS
            // _start is sometimes marked with size 0 in elf headers
            if (!valid && i->second.name == "_start") {
                name             = i->second.name;
                offset_in_object = offset - i->first;
                valid            = true;
                found            = true;
            }
#endif
        }

        return found;
    }

    void iterate(function<void(rva           address,
                               size_t        size,
                               const string& name)> callback)
    {
        for (auto& object : cache_) {
            callback(object.first, object.second.size, object.second.name);
        }
    }

    // Find target_address by function name
    bool find(const section_cache& sections, const string name, rva& address, size_t& size) const
    {
        bool found = false;
        size_t pos = string::npos;
        for (auto& object : cache_) {
            pos = object.second.name.find(name);
            if (pos != string::npos)
            {
                if (sections.find(object.first, address))
                {
                    size = object.second.size;
                    found = true;
                    break;
                }
            }
        }
        return found;
    }

private:
    typedef struct object { size_t size; string name; } object;

    map<rva /*address*/, object> cache_;
};

class robuf : public std::streambuf
{
protected:
    robuf()
    {
        set(0, 0);
    }

public:
    robuf(void* p, size_t size)
    {
        set(p, size);
    }

    pos_type seekoff(off_type           off,
                     ios_base::seekdir  way,
                     ios_base::openmode which)
    {
        off_type o;

        switch (way) {
        case ios_base::beg:
            o = off;
            break;
        case ios_base::end:
            o = size_ + off;
            break;
        case ios_base::cur: // fall through
        default:
            o = -1;
            break;
        }

        if ((which & ios_base::in) && o >= 0 && o < size_) {
            setg(p_, p_ + o, p_ + size_);
        } else {
            o = -1;
        }

        return pos_type(o);
    }

    pos_type seekpos(pos_type sp, ios_base::openmode which)
    {
        return seekoff(off_type(sp), ios_base::beg, which);
    }

    bool get(const void*& p, size_t& size) const
    {
        bool got = false;

        if (p_) {
            p    = p_;
            size = size_;
            got = true;
        }

        return got;
    }

protected:
    void set(void* p, size_t size)
    {
        p_    = (char*)p;
        size_ = size;
        setg(p_, p_, p_ + size_);
    }

    bool get(void*& p, size_t& size)
    {
        bool got = false;

        if (p_) {
            p    = p_;
            size = size_;
        }

        return got;
    }

private:
    char*    p_;
    off_type size_;
};

class rommapbuf : public robuf
{
public:
    explicit rommapbuf(const string& path)
    {
        do_mmap(path);
    }

    ~rommapbuf()
    {
        do_munmap();
    }

private:
    bool do_mmap(const string& path)
    {
        bool done = false;

        int fd = open(path.c_str(), O_RDONLY);
        if (fd == -1) {
            //SAT_LOG(0, "open('%s') failed\n", path.c_str());
        } else {
            struct stat sb;
            if (fstat(fd, &sb) != 0) {
                // TODO
            } else {
                void* p = mmap(0, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
                if (p == (void*)-1) {
                    // TODO
                } else {
                    set(p, sb.st_size);
                    done = true;
                }
            }

            close(fd);
        }

        return done;
    }

    void do_munmap()
    {
        void*  p;
        size_t size;

        if (get(p, size)) {
            (void)munmap(p, size);
            set(0, 0);
        }
    }
};


class mmapped::impl
{
protected:
    impl(const string& path, const string& sym_path) :
        streambuf_(path),
        //istream_(&streambuf_),
        object_cache_filled_(),
        objects_(),
        default_load_address_(),
        text_section_offset_(),
        text_section_size_(),
        bits_(),
        has_64_bit_funcs_(false)
    {
        if (path != sym_path) {
            sym_streambuf_ = new rommapbuf(sym_path);
        } else {
            sym_streambuf_ = &streambuf_;
        }
    }

public:
    virtual void fill_object_cache() = 0;

    virtual void add_x86_64_region(rva begin, rva end)
    {}
    virtual bool is_x86_64_region(rva address)
    {return false;}

    rommapbuf              streambuf_;
    rommapbuf              *sym_streambuf_;
    bool                   object_cache_filled_;
    object_cache           objects_;
    section_cache          sections_;
    rva                    default_load_address_;
    rva                    text_section_offset_;
    size_t                 text_section_size_;
    unsigned               bits_;
    bool                   has_64_bit_funcs_;

    map<string /*name*/,
        rva    /*offset*/> global_function_cache_;
    map<rva    /*offset*/,
        string /*name*/>   relocation_cache_;
    map<rva    /*offset*/,
        string /*name*/>   got_relocation_cache_;

}; // class mmapped::impl


class impl_elf : public mmapped::impl
{
public:
    impl_elf(const string& path, const string& sym_path) :
        mmapped::impl(path, sym_path)
    {}

private:
    void fill_object_cache_relocs() {
        istream mmapped_file(&streambuf_);
        elfio   elf;

        if (!elf.load(mmapped_file)) {
            // TODO
        } else {
            // for the purposes of mmapping the whole file, calculate
            // the address of the beginning of the file
            const section* text_section = elf.sections[".text"];
            if (!text_section) {
                default_load_address_ = 0;
                SAT_LOG(0, "COULD NOT GET DEFAULT LOAD ADDRESS\n");
            } else {
                text_section_size_   = text_section->get_size();
                text_section_offset_ = get_elfio_section_offset(text_section);
                default_load_address_ = text_section->get_address()
                                      - text_section_offset_;
            }

            bits_ = elf.get_class() == ELFCLASS32 ? 32 : 64;

            // read sections
            section_cache sections;
            int i;
            for (i = 0; i < elf.sections.size(); ++i) {
                const section* s = elf.sections[i];
                sections.add(s->get_address() - default_load_address_,
                             s->get_address(),
                             s->get_size(),
                             s->get_name(),
                             s->get_flags());
            }

            // read plts
            const section* plt_section = elf.sections[".plt"];
            if (plt_section) {
                rva plt_address = plt_section->get_address();
                section* relplt_section;
                if (((relplt_section = elf.sections[".rel.plt"]) ||
                    (relplt_section = elf.sections[".rela.plt"])) &&
                    (relplt_section->get_type() == SHT_RELA ||
                     relplt_section->get_type() == SHT_REL))
                {
                    relocation_section_accessor plts(elf, relplt_section);
                    for (unsigned r = 0; r < plts.get_entries_num(); ++r) {
                        Elf64_Addr offset = 0;
                        Elf64_Addr value;
                        string     name;
                        Elf_Word   type = 0;
                        Elf_Sxword addend;
                        Elf_Sxword calc;
                        if (plts.get_entry(r,
                                           offset,
                                           value,
                                           name,
                                           type,
                                           addend,
                                           calc))
                        {
                            if (name != "") {
                                objects_.add(sections,
                                             plt_address + (r + 1) * 16 -
                                               default_load_address_,
                                             16,
                                             name + "@plt");
                            }
                        }
                    }
                }
            }

            // read relocation entries
            section* rel_section;
            if (((rel_section = elf.sections[".rel.dyn"]) ||
                (rel_section = elf.sections[".rela.dyn"])) &&
                (rel_section->get_type() == SHT_RELA ||
                 rel_section->get_type() == SHT_REL))
            {
                relocation_section_accessor rels(elf, rel_section);
                for (unsigned r = 0; r < rels.get_entries_num(); ++r) {
                    Elf64_Addr offset = 0;
                    Elf64_Addr value;
                    string     name;
                    Elf_Word   type = 0;
                    Elf_Sxword addend;
                    Elf_Sxword calc;
                    if (rels.get_entry(r,
                                       offset,
                                       value,
                                       name,
                                       type,
                                       addend,
                                       calc))
                    {
                        if (type == R_386_PC32) {
                            SAT_LOG(1, "relocation: %lx %s \n", offset, name.c_str());
                            relocation_cache_[offset] = name;
                        }
                        else if (type == R_386_GLOB_DAT) {
                            SAT_LOG(1, "got-relocation: %lx %s %lx \n", offset, name.c_str(), value);
                            got_relocation_cache_[offset] = name;
                        }
                    }
                }
            }

            // read plt got
            const section* got_section = elf.sections[".plt.got"];
            if (got_section) {
                rva got_sec_address = got_section->get_address();
                size_t got_sec_size = got_section->get_size();
                Elf64_Off got_sec_offset = sat::get_elfio_section_offset(got_section);

                rva got_map_offset;
                unsigned char *load_address;
                bool host_map;
                size_t      s;
                const void* p;

                host_map = streambuf_.get(p, s);

                if (!host_map)
                    printf("ERROR .plt.got no host_map found?\n");
                else {
                    load_address = (unsigned char*)p;

                    cs_x86      *x86;
                    cs_insn     *insn;
                    cs_mode     mode;
                    csh         handle;

                    switch (bits_) {
                        case 16:
                            mode = CS_MODE_16;
                            break;
                        case 32:
                            mode = CS_MODE_32;
                            break;
                        case 64:
                        default:
                            mode = CS_MODE_64;
                    }
                    if (cs_open(CS_ARCH_X86, mode, &handle) == CS_ERR_OK) {
                        cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
                        cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
                        insn = cs_malloc(handle);

                        rva* address = (rva*)&got_sec_address;
                        const unsigned char* code = load_address + got_sec_offset;   // pointer to the actual code

                        while(cs_disasm_iter(handle, &code, &got_sec_size, address, insn)) {
                            if (insn->id == X86_INS_JMP) {
                                if (insn->detail == NULL)
                                    continue;
                                x86 = &(insn->detail->x86);

                                got_map_offset = x86->disp + insn->address + insn->size;

                                const auto& r = got_relocation_cache_.find(got_map_offset);
                                if (r != got_relocation_cache_.end()) {
                                    objects_.add(sections,
                                                insn->address,
                                                8,
                                                r->second + "@plt");
                                }
                            }
                        }
                        cs_free(insn, 1);
                        cs_close(&handle);
                    }
                }
            }
        }
    }

    void fill_object_cache_symbols() {
        istream sym_mmapped_file(sym_streambuf_);
        elfio   sym_elf;

        if (!sym_elf.load(sym_mmapped_file)) {
            // TODO
        } else {
            bits_ = sym_elf.get_class() == ELFCLASS32 ? 32 : 64;

            // read sections
            section_cache sections;
            int i;
            for (i = 0; i < sym_elf.sections.size(); ++i) {
                const section* s = sym_elf.sections[i];
                /* Use object default_load_address_ and not from the symbol file */
                sections.add(s->get_address() - default_load_address_,
                             s->get_address(),
                             s->get_size(),
                             s->get_name(),
                             s->get_flags());
            }

            // read functions
            for (i = 0; i < sym_elf.sections.size(); ++i) {
                section* sec = sym_elf.sections[i];
                if (sec->get_type() == SHT_SYMTAB ||
                    sec->get_type() == SHT_DYNSYM)
                {
                    const symbol_section_accessor symbols(sym_elf, sec);
                    for (unsigned s = 0; s < symbols.get_symbols_num(); ++s) {
                        string        name;
                        Elf64_Addr    value = 0;
                        Elf_Xword     size  = 0;
                        unsigned char bind  = 0;
                        unsigned char type  = STT_NOTYPE;
                        Elf_Half      section_index;
                        unsigned char other;
                        symbols.get_symbol(s,
                                           name,
                                           value,
                                           size,
                                           bind,
                                           type,
                                           section_index,
                                           other);
                        if ((type == STT_FUNC || type == STT_NOTYPE) &&
                            (name != "") &&
                            (section_index > 0 && section_index < sym_elf.sections.size()))
                        {
                            /* We need to use object default_load_address_ and not the debug version one */
                            auto offset = value - default_load_address_;
                            //printf("func: %8.8lx (%8.8lx) si:%x %s\n", offset, size, section_index, name.c_str());
                            objects_.add(sections, offset, size, name);
                            if (bind == STB_GLOBAL) {
                                global_function_cache_[name] = offset;
                            }
                        }
                    }
                }
            }
        }
    }

public:
    void fill_object_cache() override
    {
        fill_object_cache_relocs();
        fill_object_cache_symbols();

        objects_.fix_sizes();
        object_cache_filled_ = true;
    }

}; // class impl_elf


class impl_oat : public mmapped::impl
{
public:
    impl_oat(const string& path) :
        mmapped::impl(path, path)
    {}

    void fill_object_cache() override
    {
        istream mmapped_file(&streambuf_);

        oat_symbol oatdata;
        oat_symbol oatexec;
        oat_symbol oatlastword;
        string     version;

        if (!get_oat_info(mmapped_file,
                          default_load_address_,
                          text_section_offset_,
                          text_section_size_,
                          bits_,
                          oatdata,
                          oatexec,
                          oatlastword,
                          version))
        {
            // TODO
        } else {
            int ver = std::stoi(version);
            section_cache sections;
            sections.add(text_section_offset_, text_section_offset_, text_section_size_, ".text", SHF_EXECINSTR);

            const void* buf;
            size_t      s;
            if (!streambuf_.get(buf, s)) {
                // TODO
            } else {
                rva text_section_sentinel = text_section_offset_ +
                                            text_section_size_;
                iterate_oat_methods((const uint8_t*)buf + oatdata.file_offset,
                                    ver,
                                    [&](rva           offset,
                                        size_t        size,
                                        const string& name)
                                    {
                                        rva file_offset =
                                            oatdata.file_offset + offset;
                                        if (text_section_offset_ < file_offset &&
                                            file_offset < text_section_sentinel)
                                        {
#if 1
                                            SAT_LOG(2, "ADD OAT SYM: %" PRIx64 \
                                                    " %s\n",
                                                    file_offset,
                                                    name.c_str());
#endif
                                            objects_.add(sections,
                                                         file_offset,
                                                         size,
                                                         name);
                                        }
                                    });
            }
        }

        objects_.fix_sizes();

        object_cache_filled_ = true;
    }
}; // class impl_oat


class impl_vm_elf : public mmapped::impl
{
public:
    impl_vm_elf(const string& path) :
        mmapped::impl(path, path)
    {}

    void fill_object_cache() override
    {
        istream mmapped_file(&streambuf_);
        elfio   elf;
        if (!elf.load(mmapped_file)) {
            // TODO
        } else {
            // for the purposes of mmapping the whole file, calculate
            // the address of the beginning of the file

            bits_ = elf.get_class() == ELFCLASS32 ? 32 : 64;

            // read sections
            //section_cache sections;
            int i;
            for (i = 0; i < elf.sections.size(); ++i) {
                const section* s = elf.sections[i];
                default_load_address_ = 0;
                sections_.add(get_elfio_section_offset(s),
                              s->get_address(),
                              s->get_size(),
                              s->get_name(),
                              s->get_flags());
            }

            // read functions
            for (i = 0; i < elf.sections.size(); ++i) {
                section* sec = elf.sections[i];
                if (sec->get_type() == SHT_SYMTAB ||
                    sec->get_type() == SHT_DYNSYM)
                {
                    const symbol_section_accessor symbols(elf, sec);
                    for (unsigned s = 0; s < symbols.get_symbols_num(); ++s) {
                        string        name;
                        Elf64_Addr    value = 0;
                        Elf_Xword     size  = 0;
                        unsigned char bind  = 0;
                        unsigned char type  = STT_NOTYPE;
                        Elf_Half      section_index;
                        unsigned char other;
                        symbols.get_symbol(s,
                                           name,
                                           value,
                                           size,
                                           bind,
                                           type,
                                           section_index,
                                           other);
                        if ((size > 0)   &&
                            (name != "") &&
                            (section_index > 0 && section_index < elf.sections.size()) &&
                            (elf.sections[section_index]->get_flags() & SHF_EXECINSTR))
                        {
                            /* calculate virtual file load address for the executable section */
                            auto virtual_file_load_address = elf.sections[section_index]->get_address() -
                                                    get_elfio_section_offset(elf.sections[section_index]);

                            /* offset: offset to function start from beginning of elf file */
                            auto offset = value - virtual_file_load_address;
                            objects_.add(sections_, offset, size, name);
                            if (bind == STB_GLOBAL) {
                                global_function_cache_[name] = offset;
                            }
                        }
                    }
                }
            }
        }
        objects_.fix_sizes();

        object_cache_filled_ = true;
    }

    void add_x86_64_region(rva begin, rva end)
    {
        has_64_bit_funcs_ = true;
        x86_64_regions_.insert({begin, end});
    }

    bool is_x86_64_region(rva address)
    {
        if (has_64_bit_funcs_) {
            auto i = x86_64_regions_.upper_bound(address);
            if (i != x86_64_regions_.begin()) {
                --i;
            } else {
                return false;
            }
            if (address <= i->second)
                return true;
            return false;
        } else {
            return false;
        }
    }

private:
    map<rva /*begin*/, rva /*end*/> x86_64_regions_;
}; // class impl_vm_elf


mmapped::mmapped(const string& path, const string& sym_path, bool vm) :
    pimpl_()
{
    auto dot= path.find_last_of('.');
    string ext = dot != string::npos ? path.substr(dot) : "";
    if (ext == ".oat" || ext == ".dex") {
        pimpl_.reset(new impl_oat(path));
    } else if (vm) {
        pimpl_.reset(new impl_vm_elf(path));
    } else {
        pimpl_.reset(new impl_elf(path, sym_path));
    }
}

mmapped::~mmapped()
{
}

// static
shared_ptr<mmapped> mmapped::obtain(const string& path, const string& sym_path, bool vm)
{
    shared_ptr<mmapped> result{nullptr};

    static map<string, shared_ptr<mmapped>> cache;
    auto m = cache.find(path);
    if (m != cache.end()) {
        result = m->second;
    } else {
        result.reset(new mmapped(path, sym_path, vm));
        cache.insert({path, result});
    }

    return result;
}

shared_ptr<mmapped> mmapped::obtain(const string& path, const string& sym_path)
{
    return obtain(path, sym_path, false);
}

bool mmapped::is_ok()
{
    const void* dummy1;
    size_t      dummy2;

    return pimpl_->streambuf_.get(dummy1, dummy2);
}

bool mmapped::get_host_mmap(const unsigned char*& host_load_address,
                            unsigned&             size,
                            unsigned&             bits) const
{
    bool got_it = false;

    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }

    const void* p;
    size_t      s;
    got_it = pimpl_->streambuf_.get(p, s);
    if (got_it) {
        host_load_address = (const unsigned char*)p;
        size              = s;
        bits              = pimpl_->bits_;
    }

    return got_it;
}

bool mmapped::get_function(rva offset, string& name, unsigned& offset_in_func)
{
    bool got_it = false;

    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }

    pimpl_->objects_.get_cached(offset, got_it, name, offset_in_func);

    return got_it;
}

bool mmapped::get_function(string name, rva& address, size_t& size)
{
    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }

    return pimpl_->objects_.find(pimpl_->sections_, name, address, size);
}

bool mmapped::get_global_function(const string& name, rva& offset)
{
    bool got_it = false;

    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }

    const auto& f = pimpl_->global_function_cache_.find(name);
    if (f != pimpl_->global_function_cache_.end()) {
        offset = f->second;
        got_it = true;
    }

    return got_it;
}

bool mmapped::get_relocation(rva offset, string& name)
{
    bool got_it = false;

    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }

    const auto& r = pimpl_->relocation_cache_.find(offset);
    if (r != pimpl_->relocation_cache_.end()) {
        name = r->second;
        got_it = true;
    }

    return got_it;
}

bool mmapped::get_text_section(rva& offset, size_t& size)
{
    bool got_it = false;

    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }

    if (pimpl_->text_section_offset_ && pimpl_->text_section_size_) {
        offset = pimpl_->text_section_offset_;
        size   = pimpl_->text_section_size_;
        got_it = true;
    }

    return got_it;
}

void mmapped::iterate_functions(function<void(
                                    rva           /*offset*/,
                                    size_t        /*size*/,
                                    const string& /*name*/)> callback)
{
    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }
    pimpl_->objects_.iterate(callback);
}

void mmapped::iterate_executable_sections(function<void(
                                     rva           /*offset*/,
                                     rva           /*target_address*/,
                                     size_t        /*size*/,
                                     const string& /*name*/)> callback)
{
    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }
    pimpl_->sections_.iterate_executables(callback);
}

rva mmapped::default_load_address()
{
    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }
    return pimpl_->default_load_address_;
}

void mmapped::add_x86_64_region(rva begin, rva end)
{
    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }
    return pimpl_->add_x86_64_region(begin, end);
}

bool mmapped::is_x86_64_region(rva address)
{
    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }
    return pimpl_->is_x86_64_region(address);
}

} // namespace sat
