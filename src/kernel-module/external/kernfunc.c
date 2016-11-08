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

static struct kernsym sym_module_alloc;
static struct kernsym sym_module_free;

static struct kernsym sym_insn_init;
static struct kernsym sym_insn_get_length;
static struct kernsym sym_insn_rip_relative;

// locate the kernel symbols we need that aren't exported

int kernfunc_init(void) {

	int ret;

	ret = find_symbol_address(&sym_module_alloc, "module_alloc");

	if (IN_ERR(ret))
		return ret;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 1)
	ret = find_symbol_address(&sym_module_free, "module_free");
#else
	ret = find_symbol_address(&sym_module_free, "module_memfree");
#endif

	if (IN_ERR(ret))
		return ret;

	ret = find_symbol_address(&sym_insn_init, "insn_init");

	if (IN_ERR(ret))
		return ret;

	ret = find_symbol_address(&sym_insn_get_length, "insn_get_length");

	if (IN_ERR(ret))
		return ret;

	ret = find_symbol_address(&sym_insn_rip_relative, "insn_rip_relative");

	if (IN_ERR(ret))
		return ret;

	return 0;
}

// call to module_alloc

void *mod_malloc(unsigned long size) {
	void *(*run)(unsigned long) = sym_module_alloc.run;
	return run(size);
}

// call to module_free

void mod_malloc_free(void *buf) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 1)
	void (*run)(struct module *, void *) = sym_module_free.run;
	if (buf != NULL)
		run(NULL, buf);
#else
	void (*run)(void *) = sym_module_free.run;
	if (buf != NULL)
		run(buf);
#endif

}

// call to insn_init

void tpe_insn_init(struct insn *insn, const void *kaddr, int size) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 1)
	void (*run)(struct insn *, const void *, int) = sym_insn_init.run;
#else
	void (*run)(struct insn *, const void *, int, int) = sym_insn_init.run;
#endif

	run(insn, kaddr,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 1)
		size,
#endif
#ifdef CONFIG_X86_64
		1
#else // CONFIG_X86_32
		0
#endif
		);
}

// call to insn_get_length

void tpe_insn_get_length(struct insn *insn) {
	void (*run)(struct insn *) = sym_insn_get_length.run;
	run(insn);
}

// call to insn_rip_relative

int tpe_insn_rip_relative(struct insn *insn) {
	int (*run)(struct insn *) = sym_insn_rip_relative.run;
	return run(insn);
}
