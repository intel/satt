/*
 * Processor trace & sideband logger for Software Analysis Trace Tool
 * Copyright (C) 2011  Corey Henderson
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Adapted from Corey Henderson's 'tpe-lkm' project
 * http://cormander.com/2011/12/how-to-hook-into-hijack-linux-kernel-functions-via-lkm/
 * https://github.com/cormander/tpe-lkm
 */
#include "module.h"

#define OP_JMP_REL32  0xe9
#define OP_CALL_REL32 0xe8

#define HOOK_DATA_STORAGE_SIZE (OP_JMP_SIZE + 32)

#define GPF_DISABLE write_cr0(read_cr0() & (~ 0x10000)) //enables memory writing by changing a register somewhere
#define GPF_ENABLE write_cr0(read_cr0() | 0x10000)      //read somewhere it's terrible practice

#ifdef CONFIG_X86_64
# define CODE_ADDR_FROM_OFFSET(insn_addr, insn_len, offset) \
    (void*)((s64)(insn_addr) + (s64)(insn_len) + (s64)(s32)(offset))
#else
# define CODE_ADDR_FROM_OFFSET(insn_addr, insn_len, offset) \
    (void*)((u32)(insn_addr) + (u32)(insn_len) + (u32)(offset))
#endif

#define CODE_OFFSET_FROM_ADDR(insn_addr, insn_len, dest_addr) \
    (u32)(dest_addr - (insn_addr + (u32)insn_len))

void copy_and_fixup_insn(struct insn *src_insn, void *dest,
                         const struct kernsym *func) {

    u32 *to_fixup;
    unsigned long addr;
    BUG_ON(src_insn->length == 0);

    memcpy((void *)dest, (const void *)src_insn->kaddr,
           src_insn->length);

    if (src_insn->opcode.bytes[0] == OP_CALL_REL32 ||
        src_insn->opcode.bytes[0] == OP_JMP_REL32) {

        addr = (unsigned long)CODE_ADDR_FROM_OFFSET(src_insn->kaddr,
                                                    src_insn->length,
                                                    src_insn->immediate.value);

        if (addr >= (unsigned long)func->addr &&
            addr < (unsigned long)func->addr + func->size)
            return;

        to_fixup = (u32 *)((unsigned long)dest +
                           insn_offset_immediate(src_insn));
        *to_fixup = CODE_OFFSET_FROM_ADDR(dest, src_insn->length,
                                          (void *)addr);
        return;
    }

#ifdef CONFIG_X86_64
    if (!tpe_insn_rip_relative(src_insn))
        return;

    addr = (unsigned long)CODE_ADDR_FROM_OFFSET(src_insn->kaddr,
                                                src_insn->length,
                                                src_insn->displacement.value);

    if (addr >= (unsigned long)func->addr &&
        addr < (unsigned long)func->addr + func->size)
        return;

    to_fixup = (u32 *)((unsigned long)dest +
                       insn_offset_displacement(src_insn));
    *to_fixup = CODE_OFFSET_FROM_ADDR(dest, src_insn->length,
                                      (void *)addr);
#endif
    return;
}

// functions to set/unset write at the page that represents the given address
// this previously was code that disabled the write-protect bit of cr0, but
// this is much cleaner

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26)

#if (defined(CONFIG_XEN) || defined(CONFIG_X86_PAE))
#include <asm/cacheflush.h>
#endif

// copied from centos5 arch/x86_64/mm/pageattr.c

static inline pte_t *tpe_lookup_address(unsigned long address, unsigned int *level)
{
    pgd_t *pgd = pgd_offset_k(address);
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    if (pgd_none(*pgd))
        return NULL;
    pud = pud_offset(pgd, address);
    if (!pud_present(*pud))
        return NULL;
    pmd = pmd_offset(pud, address);
    if (!pmd_present(*pmd))
        return NULL;
    if (pmd_large(*pmd))
        return (pte_t *)pmd;
    pte = pte_offset_kernel(pmd, address);
    if (pte && !pte_present(*pte))
        pte = NULL;
    return pte;
}

#else
#define tpe_lookup_address(address, level) lookup_address(address, level);
#endif

static inline int set_addr_rw(unsigned long addr, bool *flag) {

    int err = 0;
#if (defined(CONFIG_XEN) || defined(CONFIG_X86_PAE)) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26)
    struct page *pg;

    pgprot_t prot;
    pg = virt_to_page(addr);
    prot.pgprot = VM_READ | VM_WRITE;
    change_page_attr(pg, 1, prot);
#else
    unsigned int level;
    pte_t *pte;

    *flag = true;

    pte = tpe_lookup_address(addr, &level);
    if (pte) {
#if !defined(CONFIG_X86_64) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26)
        if (pte_val(*pte) & _PAGE_RW) *flag = false;
        else pte_val(*pte) |= _PAGE_RW;
#else
        if (pte->pte & _PAGE_RW) *flag = false;
        else pte->pte |= _PAGE_RW;
#endif
    }
    else {
        err = -EPERM;
    }
#endif
    return err;
}

static inline int set_addr_ro(unsigned long addr, bool flag) {

    int err = 0;
#if (defined(CONFIG_XEN) || defined(CONFIG_X86_PAE)) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26)
    struct page *pg;

    pgprot_t prot;
    pg = virt_to_page(addr);
    prot.pgprot = VM_READ;
    change_page_attr(pg, 1, prot);
#else
    unsigned int level;
    pte_t *pte;

    // only set back to readonly if it was readonly before
    if (flag) {
        pte = tpe_lookup_address(addr, &level);
        if (pte) {
#if !defined(CONFIG_X86_64) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26)
            pte_val(*pte) = pte_val(*pte) &~_PAGE_RW;
#else
            pte->pte = pte->pte &~_PAGE_RW;
#endif
        }
        else {
            err = -EPERM;
        }
    }
#endif
    return err;
}

int symbol_prepare_for_hijack(struct kernsym *sym,
                              const char *symbol_name,
                              unsigned long *code)
{
    int ret;
    unsigned long orig_addr;
    unsigned long dest_addr;
    unsigned long end_addr;
    u32 *poffset;
    struct insn insn;

    ret = find_symbol_address(sym, symbol_name);

    if (IN_ERR(ret))
        return ret;

    if (sym->size < OP_JMP_SIZE) {
        printk(PKPRE "error: %s is too small to be hijacked\n", symbol_name);
        return -EFAULT;
    }

    if (*(u8 *)sym->addr == OP_JMP_REL32) {
        printk(PKPRE "error: %s already appears to be hijacked\n", symbol_name);
        return -EFAULT;
    }

    sym->new_addr = mod_malloc(sym->size);

    if (sym->new_addr == NULL) {
        printk(PKPRE
               "Failed to allocate buffer of size %lu for %s\n",
               sym->size, sym->name);
        return -ENOMEM;
    }

    memset(sym->new_addr, 0, (size_t)sym->size);

    orig_addr = (unsigned long)sym->addr;
    dest_addr = (unsigned long)sym->new_addr;

    end_addr = orig_addr + sym->size;
    while (end_addr > orig_addr && *(u8 *)(end_addr - 1) == '\0')
        --end_addr;

    if (orig_addr == end_addr) {
        printk(PKPRE
               "A spurious symbol \"%s\" (address: %p) seems to contain only zeros\n",
               sym->name,
               sym->addr);
        ret = -EILSEQ;
        goto out_error;
    }

    while (orig_addr < end_addr) {
        tpe_insn_init(&insn, (void *)orig_addr, MAX_INSN_SIZE);
        tpe_insn_get_length(&insn);
        if (insn.length == 0) {
            printk(PKPRE
                   "Failed to decode instruction at %p (%s+0x%lx)\n",
                   (const void *)orig_addr,
                   sym->name,
                   orig_addr - (unsigned long)sym->addr);
            ret = -EILSEQ;
            goto out_error;
        }

        copy_and_fixup_insn(&insn, (void *)dest_addr, sym);

        orig_addr += insn.length;
        dest_addr += insn.length;
    }

    memcpy(&sym->orig_start_bytes[0], sym->addr, OP_JMP_SIZE);

    sym->new_start_bytes[0] = OP_JMP_REL32;
    poffset = (u32 *)&sym->new_start_bytes[1];
    *poffset = CODE_OFFSET_FROM_ADDR((unsigned long)sym->addr,
                                     OP_JMP_SIZE, (unsigned long)code);

    return 0;

out_error:
    mod_malloc_free(sym->new_addr);
    sym->new_addr = NULL;

    return ret;
}

int symbol_prepare_for_hijack_partial_copy(struct kernsym *sym,
                              const char *symbol_name,
                              unsigned long *code)
{
    int ret;
    unsigned long orig_addr;
    unsigned long src_addr;
    unsigned long dest_addr;
    unsigned long end_addr;
    unsigned long copy_size;
    u32 *poffset;
    struct insn insn;
    unsigned char jmp_to_orig[16];

    ret = find_symbol_address(sym, symbol_name);

    if (IN_ERR(ret))
        return ret;

    if (sym->size < OP_JMP_SIZE) {
        printk(PKPRE "error: %s is too small to be hijacked\n", symbol_name);
        return -EFAULT;
    }

    if (*(u8 *)sym->addr == OP_JMP_REL32) {
        printk(PKPRE "error: %s already appears to be hijacked\n", symbol_name);
        return -EFAULT;
    }

    sym->new_addr = mod_malloc(HOOK_DATA_STORAGE_SIZE);

    if (sym->new_addr == NULL) {
        printk(PKPRE
               "Failed to allocate buffer of size %u for %s\n",
               HOOK_DATA_STORAGE_SIZE, sym->name);
        return -ENOMEM;
    }

    memset(sym->new_addr, 0, (size_t)HOOK_DATA_STORAGE_SIZE);

    orig_addr = (unsigned long)sym->addr;
    dest_addr = (unsigned long)sym->new_addr;
    end_addr = orig_addr + OP_JMP_SIZE;

    src_addr = orig_addr;
    while (src_addr < end_addr) {
        tpe_insn_init(&insn, (void *)src_addr, MAX_INSN_SIZE);
        tpe_insn_get_length(&insn);
        if (insn.length == 0) {
            printk(PKPRE
                   "Failed to decode instruction at %p (%s+0x%lx)\n",
                   (const void *)src_addr,
                   sym->name,
                   src_addr - (unsigned long)sym->addr);
            ret = -EILSEQ;
            goto out_error;
        }

        copy_and_fixup_insn(&insn, (void *)dest_addr, sym);

        src_addr += insn.length;
        dest_addr += insn.length;
    }

    /* add jump to original code */
    copy_size = src_addr - orig_addr;
    jmp_to_orig[0] = OP_JMP_REL32;
    poffset = (u32 *)&jmp_to_orig[1];
    *poffset = CODE_OFFSET_FROM_ADDR((unsigned long)dest_addr,
                  OP_JMP_SIZE, (unsigned long)(sym->addr + copy_size));
    memcpy((void *)dest_addr, &jmp_to_orig[0], OP_JMP_SIZE);
    copy_size += OP_JMP_SIZE;

    memcpy(&sym->orig_start_bytes[0], sym->addr, OP_JMP_SIZE);
    sym->new_start_bytes[0] = OP_JMP_REL32;
    poffset = (u32 *)&sym->new_start_bytes[1];
    *poffset = CODE_OFFSET_FROM_ADDR((unsigned long)sym->addr,
                                     OP_JMP_SIZE, (unsigned long)code);

    sym->size = copy_size;
    return 0;

out_error:
    mod_malloc_free(sym->new_addr);
    sym->new_addr = NULL;

    return ret;

}




void symbol_hijack(struct kernsym *sym)
{
    bool pte_ro;

    if (sym->new_addr) {

        if (!set_addr_rw((unsigned long) sym->addr, &pte_ro))
        {
            sym->run = sym->new_addr;
            GPF_DISABLE;
            memcpy(sym->addr, sym->new_start_bytes, OP_JMP_SIZE);
            GPF_ENABLE;

            if(set_addr_ro((unsigned long) sym->addr, pte_ro)) {
                printk(KERN_WARNING "SAT WARNING: Failed to set function %s back to read-only!", sym->name);
            }
            sym->hijacked = true;
        }
        else {
            printk(KERN_ERR "SAT ERROR: Failed to set function %s readwrite!", sym->name);
        }
    }
}

void symbol_restore(struct kernsym *sym) {

    bool pte_ro;

    // TODO: use memcmp to check hijacking
    if (sym->hijacked) {
        set_addr_rw((unsigned long) sym->addr, &pte_ro);
        GPF_DISABLE;
        memcpy(sym->addr, sym->orig_start_bytes, OP_JMP_SIZE);
        GPF_ENABLE;
        set_addr_ro((unsigned long) sym->addr, pte_ro);

        sym->run = sym->addr;
        sym->hijacked = false;
    }

    return;
}

void symbol_free_copy(struct kernsym *sym) {
    if (sym->new_addr) {
        mod_malloc_free(sym->new_addr);
        sym->new_addr = NULL;
    }
}
