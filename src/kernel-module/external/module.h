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
#ifndef TPE_H_INCLUDED
#define TPE_H_INCLUDED

#include <linux/module.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <linux/version.h>
#include <linux/utsname.h>
#include <linux/kallsyms.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/sysctl.h>
#include <linux/err.h>
#include <asm/uaccess.h>
#include <asm/insn.h>

#define MODULE_NAME "tpe"
#define PKPRE "[" MODULE_NAME "] "
#define MAX_FILE_LEN 256
#define TPE_HARDCODED_PATH_LEN 1024

#define LOG_FLOODTIME 5
#define LOG_FLOODBURST 5

#define OP_JMP_SIZE 5

#define IN_ERR(x) (x < 0)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
#define get_task_uid(task) task->uid
#define get_task_parent(task) task->parent
#else
#define get_task_uid(task) task->cred->uid
#define get_task_parent(task) task->real_parent
#endif

// d_path changed argument types. lame

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25)
#define tpe_d_path(file, buf, len) d_path(file->f_dentry, file->f_vfsmnt, buf, len);
#else
#define tpe_d_path(file, buf, len) d_path(&file->f_path, buf, len);
#endif

struct kernsym {
    void *addr; // orig addr
    unsigned long size;
    const char *name;
    u8 orig_start_bytes[OP_JMP_SIZE];
    u8 new_start_bytes[OP_JMP_SIZE];
    void *new_addr;
    bool found;
    bool hijacked;
    void *run;
};

int symbol_prepare_for_hijack(struct kernsym *, const char *, unsigned long *);
int symbol_prepare_for_hijack_partial_copy(struct kernsym *, const char *, unsigned long *);
void symbol_hijack(struct kernsym *);
void symbol_restore(struct kernsym *);
void symbol_free_copy(struct kernsym *);

int find_symbol_address(struct kernsym *, const char *);

int kernfunc_init(void);

void tpe_insn_init(struct insn *, const void *, int);
void tpe_insn_get_length(struct insn *insn);
int tpe_insn_rip_relative(struct insn *insn);

void *mod_malloc(unsigned long size);
void mod_malloc_free(void *buf);

#endif
