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
#if 1
#include "sat-demangle.h"
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
#include <memory>
#include <functional>

#define SAT_DEC(width) setw(width) << setfill(' ') << dec << right
#define SAT_HEX(width) setw(width) << setfill('0') << hex << right
#define SAT_STR(width) setw(width) << setfill(' ') << left

#define SAT_RVA  SAT_STR(0) << "0x" << SAT_HEX(8)
#define SAT_SIZE SAT_DEC(10)

using namespace ELFIO;
using namespace std;

typedef uint64_t rva;

class segment_cache
{
public:
    void add(rva offset, size_t size)
    {
        if (size != 0) {
            if (cache_[offset].size < size) {
                cache_[offset] = {size};
            }
        }
    }

    void iterate(function<void(rva offset, size_t size)> callback)
    {
        for (auto& segment : cache_) {
            callback(segment.first, segment.second.size);
        }
    }

private:
    typedef struct segment { size_t size; } segment;

    map<rva /*offset*/, segment> cache_;
};

class section_cache
{
public:
    void add(rva address, size_t size, const string& name)
    {
        if (address != 0 && size != 0) {
            if (cache_[address].size < size) {
                cache_[address] = {size, name};
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

private:
    typedef struct section { size_t size; string name; } section;

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
                prev->second.size = next->first - prev->first;
                prev = next;
                ++next;
            }
        }
    }

    bool get_cached(rva     offset,
                    bool&   valid,
                    string& name,
                    size_t& offset_in_object) const
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

private:
    typedef struct object { size_t size; string name; } object;

    map<rva /*address*/, object> cache_;
};

class robuf : public std::streambuf
{
protected:
    robuf()
    {
        set(0,0);
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


class mmapped
{
private:
    explicit mmapped(const string& path);

public:
    ~mmapped() {}

    static shared_ptr<mmapped> obtain(const string& path);

    bool is_ok();

    bool get_host_mmap(const unsigned char*& host_load_address,
                       unsigned&             size,
                       unsigned&             bits) const;

    bool get_function(rva offset, string& name, size_t& offset_in_func);
    bool get_global_function(const string& name, rva& offset);
    bool get_relocation(rva offset, string& name);

    void iterate_functions(function<void(rva           /*offset*/,
                                         size_t        /*size*/,
                                         const string& /*name*/)> callback);

    rva default_load_address();

private:
    class impl;
    unique_ptr<impl> pimpl_;
};

class mmapped::impl
{
public:
    impl(const string& path) :
        streambuf_(path),
        //istream_(&streambuf_),
        object_cache_filled_(),
        objects_(),
        default_load_address_(),
        bits_()
    {}

    void fill_object_cache()
    {
        cout << "fill_object_cache()" << endl;
        istream mmapped_file(&streambuf_);
        elfio   elf;
        if (!elf.load(mmapped_file)) {
            cerr << "could not load elf file" << endl;
            // TODO
        } else {
            // for the purposes of mmapping the whole file, calculate
            // the address of the beginning of the file;
            const section* text_section = elf.sections[".text"];
            if (!text_section) {
                default_load_address_ = 0;
            } else {
                default_load_address_ = text_section->get_address()
                    - sat::get_elfio_section_offset(text_section);
            }
            cout << "default load address: " << hex << default_load_address_ << endl;

            bits_ = elf.get_class() == ELFCLASS32 ? 32 : 64;

            // read sections
            section_cache sections;
            int i;
            for (i = 0; i < elf.sections.size(); ++i) {
                const section* s = elf.sections[i];
                cout << "section " << s->get_name() << endl;
                sections.add(s->get_address() - default_load_address_,
                             s->get_size(),
                             s->get_name());
            }

            // read functions
            for (i = 0; i < elf.sections.size(); ++i) {
                section* sec = elf.sections[i];
                if (sec->get_type() == SHT_SYMTAB ||
                    sec->get_type() == SHT_DYNSYM)
                {
                    cout << "symbol section " << sec->get_name() << endl;
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
                        if ((type == STT_FUNC || type == STT_NOTYPE) &&
                            (name != "") &&
                            (section_index > 0 && section_index < elf.sections.size()))
/*                            value > 0)
                            && value > default_load_address_)*/
                        {
                            auto offset = value - default_load_address_;
                            cout << offset << " " << name << endl;
                            objects_.add(sections, offset, size, name);
                            if (bind == STB_GLOBAL) {
                                global_function_cache_[name] = offset;
                            }
                        }
                    }
                }
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
                 rel_section->get_type() == SHT_REL)) {
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
                            cout << "relocation: " << hex << type << " " << name << " " << hex << offset << endl;
                            relocation_cache_[offset] = name;
                        }
                        if (type == R_386_GLOB_DAT) {
                            cout << "got-relocation: " << hex << offset << " " << name << " " << hex << value << " " << hex << addend << " " << hex << calc << endl;
                            got_relocation_cache_[offset] = {value, name};
                        }
                    }
                }
            }

            // read plt got
            const section* got_section = elf.sections[".plt.got"];
            if (got_section) {
                rva got_address = got_section->get_address();
                size_t got_section_size = got_section->get_size();
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
                    printf("got.plt.got sections, addr(0x%lx) host_map(%d)\n", got_address, host_map);

                    cs_x86 *x86;
                    cs_insn     *insn;                // capstone instruction storage
                    cs_mode     mode;
                    csh     handle;
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

                        rva* address = (rva*)&got_address;
                        const unsigned char* code = load_address + got_sec_offset;   // pointer to the actual code

                        while(cs_disasm_iter(handle, &code, &got_section_size, address, insn)) {
                            if (insn->id == X86_INS_JMP)
                            {
                                // Detail can be NULL on "data"
                                if (insn->detail == NULL)
                                    continue;
                                x86 = &(insn->detail->x86);

                                // TODO can we get next address easily and not +6 ?
                                got_map_offset = x86->disp + insn->address + 6; // + 6 is the start of the next address (rip)

                                const auto& r = got_relocation_cache_.find(got_map_offset);
                                if (r != got_relocation_cache_.end()) {
                                    objects_.add(sections,
                                                insn->address,
                                                8,
                                                r->second.name + "@got");
                                }
                            }
                        }
                        cs_free(insn, 1);
                        cs_close(&handle);
                    }

                }
            }

        }

        objects_.fix_sizes();

        object_cache_filled_ = true;
    }

    rommapbuf              streambuf_;
    bool                   object_cache_filled_;
    object_cache           objects_;
    rva                    default_load_address_;
    unsigned               bits_;
    map<string /*name*/,
        rva    /*offset*/> global_function_cache_;
    map<rva    /*offset*/,
        string /*name*/>   relocation_cache_;

    typedef struct got_plt { rva address; string name;} got_plt;
    map<rva    /*offset*/,
        got_plt>   got_relocation_cache_;
};


mmapped::mmapped(const string& path) :
    pimpl_{new impl(path)}
{
}

// static
shared_ptr<mmapped> mmapped::obtain(const string& path)
{
    shared_ptr<mmapped> result{nullptr};

    static map<string, shared_ptr<mmapped>> cache;
    auto m = cache.find(path);
    if (m != cache.end()) {
        result = m->second;
    } else {
        result.reset(new mmapped(path));
        cache.insert({path, result});
    }

    return result;
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

bool mmapped::get_function(rva offset, string& name, size_t& offset_in_func)
{
    bool got_it = false;

    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }

    pimpl_->objects_.get_cached(offset, got_it, name, offset_in_func);

    return got_it;
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

rva mmapped::default_load_address()
{
    if (!pimpl_->object_cache_filled_) {
        pimpl_->fill_object_cache();
    }
    return pimpl_->default_load_address_;
}


int main(int argc, char* argv[])
{
    int i;
    for (i = 1; i < argc; ++i) {
        const void*  addr = 0;
        size_t       size = 0;

        shared_ptr<mmapped> m;
        m = mmapped::obtain(argv[i]);

        rva    a = 0x28d0;
        string n;
        rva    o;
        if (m->get_function(a, n, o)) {
            cout << "function at " << hex << a << " is " << n << endl;
        } else {
            cout << "could not find function at " << hex << a << endl;
        }

        cout << "default load address: " << m->default_load_address() << endl;

        const unsigned char* host_load_address;
        unsigned             host_size;
        unsigned             bits;
        if (!m->get_host_mmap(host_load_address, host_size, bits)) {
            cerr << "cannot get host load address " << argv[i] << endl;
            exit(EXIT_FAILURE);
        }
        cout << "host load address: " << hex << (const void*)host_load_address << endl;
        cout << "host size:         " << host_size << endl;
        cout << "bits: " << dec << bits << endl;
        cout << (m->is_ok() ? "OK" : "NOK") << endl;

        m->iterate_functions([](rva offset, size_t size, const string& name)
                             {
                                 cout << hex << offset << " " << name << endl;
                             });
        continue;

        rommapbuf mmapbuf(argv[i]);
        if (!mmapbuf.get(addr, size)) {
            cerr << "cannot mmap " << argv[i] << endl;
            exit(EXIT_FAILURE);
        }

        istream mmapped_file(&mmapbuf);

        elfio reader;

        //if (!reader.load(argv[i])) {
        if (!reader.load(mmapped_file)) {
            cerr << "cannot process ELF file " << argv[i] << endl;
            exit(EXIT_FAILURE);
        }
        cout << argv[i] << endl;

        if (reader.get_class() == ELFCLASS32) {
            cout << "32-bit" << endl;
        } else {
            cout << "64-bit" << endl;
        }

        const section* text = reader.sections[".text"];
        if (text) {
            cout << endl << "has .text section" << endl;
        }

        int si;

        cout << endl << "all segments" << endl;
        segment_cache segments;
        for (si = 0; si < reader.segments.size(); ++si ) {
            const segment* seg = reader.segments[si];
            segments.add(seg->get_virtual_address(), seg->get_file_size());
            cout << SAT_RVA    << seg->get_virtual_address()
                 << SAT_STR(0) << ", ("
                 << SAT_SIZE   << seg->get_file_size() << " bytes)"
                 << endl;
        }
        cout << endl << "segments ordered by offset" << endl;
        segments.iterate([&](rva offset, size_t size)
                         {
                             cout << SAT_RVA    << offset
                                  << SAT_STR(0) << " .. "
                                  << SAT_RVA    << offset + size - 1
                                  << SAT_STR(0) << ", ("
                                  << SAT_SIZE   <<  size << " bytes)"
                                  << endl;
                         });

        cout << endl << "sections" << endl;
        section_cache sections;
        for (si = 0; si < reader.sections.size(); ++si) {
            const section* sec = reader.sections[si];
            sections.add(sec->get_address(), sec->get_size(), sec->get_name());
            cout << SAT_RVA    << sec->get_address()
                 << SAT_STR(0) << " .. "
                 << SAT_RVA    << sec->get_address() + sec->get_size()
                 << SAT_STR(0) << ", ("
                 << SAT_SIZE   << sec->get_size() << " bytes) "
                 << sec->get_name()
                 << endl;
        }
        cout << endl << "sections ordered by address" << endl;
        sections.iterate([&](rva address, size_t size, const string& name)
                         {
                             cout << SAT_RVA    << address
                                  << SAT_STR(0) << " .. "
                                  << SAT_RVA    << address + size - 1
                                  << SAT_STR(0) << ", ("
                                  << SAT_SIZE   << size << " bytes) "
                                  << name
                                  << endl;
                         });

        cout << endl << "functions" << endl;
        object_cache objects;
        for (si = 0; si < reader.sections.size(); ++si) {
            section* sec = reader.sections[si];
            if (sec->get_type() == SHT_SYMTAB ||
                sec->get_type() == SHT_DYNSYM)
            {
                cout << "in section " << sec->get_name() << endl;
                const symbol_section_accessor symbols(reader, sec);
                for (unsigned s = 0; s < symbols.get_symbols_num(); ++s) {
                    string        name;
                    Elf64_Addr    value = 0;
                    Elf_Xword     size  = 0;
                    unsigned char bind;
                    unsigned char type = STT_NOTYPE;
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
                    if (type == STT_FUNC || type == STT_NOTYPE) {
                        cout << SAT_RVA    << value
                             << SAT_STR(0) << ", ("
                             << SAT_SIZE   << size << " bytes) ";
                        string s;
                        sections.find(value, s);
                        cout << SAT_STR(8) << s << " ";
                        cout << SAT_STR(0) << sat::demangle(name);
                        if (type == STT_NOTYPE) {
                            cout << " STT_NOTYPE";
                        }
                       cout << endl;

                       objects.add(sections, value, size, name);
                    }
                }
            }
        }

        const section* plt_section = reader.sections[".plt"];
        if (!plt_section) {
            cout << endl << "no .plt section" << endl;
        } else {
            cout << endl << "has .plt section" << endl;

            rva plt_address = plt_section->get_address();
            cout << "first plt address " << SAT_RVA << plt_address << endl;
            cout << "plt entries "
                 << dec << plt_section->get_size() / 16 << endl;

            section* relplt_section;
            if ((relplt_section = reader.sections[".rel.plt"])) {
                cout << "has .rel.plt section" << endl;
            } else if ((relplt_section = reader.sections[".rela.plt"])) {
                cout << "has .rela.plt section" << endl;
            } else {
                cout << "no .rel[a].plt section" << endl;
            }

            if (relplt_section) {
                relocation_section_accessor plts(reader, relplt_section);
                cout << "relplt entries " << plts.get_entries_num() << endl;
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
                        cout << SAT_DEC(3) << r << ": "
                             << SAT_RVA    << offset << " / " << value << " "
                             <<               addend << " " << calc << " "
                             << SAT_STR(0) << name
                             << endl;
                        if (name != "") {
                            objects.add(sections,
                                        plt_address + (r + 1) * 16,
                                        16,
                                        name + "@plt");
                        }
                    }
                }
            }
        }

        cout << endl << "objects ordered by address" << endl;
        objects.fix_sizes();
        objects.iterate([&](rva address, size_t size, const string& name)
                        {
                             cout << SAT_RVA    << address
                                  << SAT_STR(0) << " .. "
                                  << SAT_RVA    << address + size - 1
                                  << SAT_STR(0) << ", ("
                                  << SAT_SIZE   << size << " bytes) "
                                  << sat::demangle(name)
                                  << endl;
                        });
    }
}
#else
#include "sat-mmapped.h"
#include "sat-log.h"
#include <iostream>

using namespace sat;

int main(int argc, char* argv[])
{
    global_debug_level = 3;
    shared_ptr<mmapped> mmapped;

    if (argc > 1) {
        string path = argv[1];
        mmapped = mmapped::obtain(path);
        if (mmapped) {
            cout << "mmapped " << path << endl;

            rva                  load_address = 0;
            const unsigned char* p;
            unsigned             size;
            unsigned             bits;
            if (mmapped->find_elf_section(load_address, p, size, bits)) {
                cout << "found elf section at " << hex << (void*)p << dec
                     << ", load address " << hex << load_address << dec
                     << ", size " << size
                     << endl;

                mmapped->fill_symbol_cache();

                mmapped->iterate_functions(
                    [&](const string& f, rva p, size_t s)
                {
                    rva address = p;
                    if (address > load_address) {
                        address -= load_address;
                    }
                    if (s > 0) {
                        cout << hex << address << " - "
                             << address + s - 1 << dec
                             << ": " << f << endl;
                    } else {
                        cout << hex << address << " - same: " << f << endl;
                    }
                });
            } else {
                cerr << "could not find elf section" << endl;
            }
        } else {
            cerr << "could not mmap " << path << endl;
        }
    }

}
#endif
