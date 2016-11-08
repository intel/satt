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
#include "sat-disassembler.h"

#include <elf.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sys/mman.h>
#include <cinttypes>

using namespace sat;

#if 0
struct elf_internal_phdr {
    unsigned long p_type;   /* Identifies program segment type */
    unsigned long p_flags;  /* Segment flags */
    bfd_vma       p_offset; /* Segment file offset */
    bfd_vma       p_vaddr;  /* Segment virtual address */
    bfd_vma       p_paddr;  /* Segment physical address */
    bfd_vma       p_filesz; /* Segment size in file */
    bfd_vma       p_memsz;  /* Segment size in memory */
    bfd_vma       p_align;  /* Segment alignment, file & memory */
};
typedef struct elf_internal_phdr Elf_Internal_Phdr;


void handle_section(bfd* abfd, asection* sect, void* obj)
{
    printf("%16" PRIx64 "/%16" PRIx64 " %6" PRIx64 " %8" PRIx64 " %c %c %s\n",
           sect->vma, sect->lma, sect->filepos, sect->size,
           (sect->flags & SEC_CODE) ? 'c' : (sect->flags & SEC_LOAD) ? ' ' : 'X',
           (sect->flags & SEC_ROM) ? 'r' : ' ',
           sect->name);
}

void iterate_sections(bfd* abfd)
{
    bfd_map_over_sections(abfd, handle_section, 0);
}
#endif

void disassemble(shared_ptr<disassembler> d, rva address, size_t size)
{
    rva                      a = address;
    disassembled_instruction i;
    while (a < address + size && d->disassemble(a, i)) {
        printf("   %6" PRIx64 ": %s%s",
               a,
               i.text().c_str(),
               i.has_jump_target() ? "  =>  " : "\n");
        if (i.has_jump_target()) {
            if (i.jump_target() > a && i.jump_target() < i.next_address()) {
                string target;
                if (d->get_relocation(i.jump_target(), target)) {
                    printf("%s  ", target.c_str());
                }
                printf("*** relocation ***\n");
            } else {
                string   target;
                unsigned offset;
                if (d->get_function(i.jump_target(), target, offset)) {
                    printf("%s", target.c_str());
                    if (offset) {
                        printf("+%x\n", offset);
                    } else {
                        printf("\n");
                    }
                } else {
                    printf("*unknown*\n");
                }
            }
        }
        a = i.next_address();
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        goto fail;
    }

#if 0
    bfd_init();

    bfd* abfd;

    abfd = bfd_openr(argv[1], 0);

    if (abfd == 0) {
        fprintf(stderr, "Could not open '%s'\n", argv[0]);
        goto fail;
    }

    if (!bfd_check_format_matches(abfd, bfd_object, 0)) {
        fprintf(stderr, "Not an object file\n");
        goto close_and_fail;
    }

    if (bfd_octets_per_byte(abfd) != 1) {
        fprintf(stderr, "Weird byte size\n");
        goto close_and_fail;
    }

    printf("start address %" PRIx64 "\n", bfd_get_start_address(abfd));

    {
        long phdrs_size = bfd_get_elf_phdr_upper_bound(abfd);
        if (phdrs_size > 0) {
            Elf_Internal_Phdr* phdr =
                (Elf_Internal_Phdr*)new unsigned char[phdrs_size];
            int phdrs = bfd_get_elf_phdrs(abfd, phdr);
            if (phdrs > 0) {
                printf("program headers\n");
                int i;
                for (i = 0; i < phdrs; ++i) {
                    printf("%u: %" PRIx64 " %" PRIx64 "\n",
                           i, phdr[i].p_vaddr, phdr[i].p_filesz);
                }
            }
        }
    }
#endif

    int i;
    for (i = 0; i < 1; ++i)
    {
        shared_ptr<mmapped> m;
        m = mmapped::obtain(argv[1], argv[1]);
        const unsigned char* p;
        unsigned             size;
        unsigned             bits;
        if (m->get_host_mmap(p, size, bits)) {
            printf("get_host_mmap(): %u bits (%p, %x)\n", bits, p, size);
        } else {
            printf("get_host_mmap() failed\n");
        }
        rva default_load_address = 0;
        shared_ptr<disassembler> d =
            disassembler::obtain(argv[1], argv[1], default_load_address);
        printf("default load address %" PRIx64 "\n", default_load_address);
        m->iterate_functions([&](rva offset, size_t size, const string& name)
                             {
                                 printf("\n%p %4" PRIx64 " %s\n",
                                        (void*)offset,
                                        (uint64_t)size,
                                        name.c_str());\
                                 disassemble(d,
                                             default_load_address + offset,
                                             size);
                             });
    }

#if 0
    printf("sections:\n");
    iterate_sections(abfd);

    if (!(bfd_get_file_flags(abfd) & HAS_SYMS)) {
        printf("No symbols\n");
    }
    if (!(bfd_get_file_flags(abfd) & DYNAMIC)) {
        printf("Not dynamic\n");
    }

    bfd_size_type symtabsize;
    symtabsize = bfd_get_dynamic_symtab_upper_bound(abfd);
    if (symtabsize < 0) {
        fprintf(stderr, "could not get symbols\n");
        goto close_and_fail;
    }

    if (symtabsize == 0) {
        fprintf(stderr, "no symbols\n");
        goto close_and_fail;
    }

    asymbol** symtab;
    symtab = (asymbol**)bfd_alloc(abfd, symtabsize);

    long symcount;
    symcount = bfd_canonicalize_dynamic_symtab(abfd, symtab);

    if (symcount < 0) {
        fprintf(stderr, "could not canonicalize symbols\n");
        goto close_and_fail;
    }

    printf("%ld symbols\n", symcount);

    bfd_size_type relsize;
    relsize = bfd_get_dynamic_reloc_upper_bound(abfd);
    if (relsize < 0) {
        fprintf(stderr, "could not get relocations\n");
        goto close_and_fail;
    } else if (relsize == 0) {
        goto close_and_fail;
    }

    arelent** rels;
    rels = (arelent**)bfd_alloc(abfd, relsize);

    long relcount;
    relcount = bfd_canonicalize_dynamic_reloc(abfd, rels, symtab);
    if (relcount == -1) {
        fprintf(stderr, "could not canonicalize relocations\n");
        goto close_and_fail;
    }
    if (relcount) {
        printf("%ld relocations\n", relcount);
        arelent** r;
        for (r = rels; *r; ++r) {
            arelent* e = *r;
            if (e->sym_ptr_ptr && *e->sym_ptr_ptr) {
                const char* sym;
                const char* section;
                sym     = (*e->sym_ptr_ptr)->name;
                section = (*e->sym_ptr_ptr)->section->name;
                printf("%" PRIx64 " %s %s %u %s %s\n",
                       e->address,
                       sym ? sym : "?",
                       section ? section : "?",
                       e->howto->type,
                       e->howto->name,
                       e->howto->pc_relative ? "rel" : "abs");
            }
        }
    } else {
        printf("no relocs\n");
    }
#endif

    return EXIT_SUCCESS;

#if 0
close_and_fail:
    bfd_close(abfd);
#endif
fail:
    return EXIT_FAILURE;
}
