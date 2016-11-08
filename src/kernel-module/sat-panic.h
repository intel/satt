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
#ifndef SAT_PANIC_H
#define SAT_PANIC_H

#include <linux/threads.h>

/*
GBuffer SAT-Panic Tracer Output

Header info
<number_of_cpus>|<sideband>|<cpu_id>|<base_address>|<offset>|<cpu_id>|<base_address>|<offset>

Payload data in Gbuffer data
<trace_data_dump>|<trace_data_dump>|<sideband_dump>
*/
#define PANIC_TRACE_VERSION '1'

#define PANIC_TRACE_MARKER "SAT"

typedef struct __attribute__((__packed__)) {
  int cpu_id;
  /* Trace buffer offset
      0 - 4M
      -1 = offset unknown */
  int offset;
  phys_addr_t base_address;
} panic_trace_buffer_info_type;

typedef struct  __attribute__((__packed__)) {
  char marker[4];
  int number_of_cpus;
  int sideband;
  u32 ctl_reg;
  panic_trace_buffer_info_type buffer_infos[NR_CPUS];
} panic_trace_header_type;

int enable_panic_handler(void);
int disable_panic_handler(void);

int init_panic_tracer(void);
void exit_panic_tracer(void);

#endif /* SAT_PANIC_H */
