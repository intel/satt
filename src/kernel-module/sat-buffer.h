/*
 * Processor trace & sideband logger for Software Analysis Trace Tool
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
 */
#ifndef SAT_BUFFER_H
#define SAT_BUFFER_H

#include <linux/types.h>

int init_trace_ram_alloc(int cpu, gfp_t gfp);
void exit_trace_ram_alloc(int cpu);
int request_trace_buffer(trace_type *trace);
int reset_trace_buffers(trace_type *trace);

int create_trace_buffer_pool(void);
void release_trace_buffer_pool(void);
void reinit_trace_buffer_pool(void);

#endif /* SAT_BUFFER_H */
