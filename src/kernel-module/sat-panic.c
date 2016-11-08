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
#include "config.h"

#ifdef PANIC_TRACER
#include <linux/stop_machine.h>
#include <linux/notifier.h>
#ifdef CONFIG_EMMC_IPANIC
#include <linux/panic_gbuffer.h>
#endif
#include "sat-tracer.h"
#include "external/module.h"
#include "sat-panic.h"
#include "sideband_tracer.h"

struct kernsym panic_hook_address;

static panic_trace_header_type panic_trace;
static char *panic_trace_buffer = 0;
extern trace_buffer_pool_type buffer_pool;

void exit_panic_tracer(void) {
    vfree(panic_trace_buffer);
}

int init_panic_tracer() {
    const int gbuffer_size = sizeof(panic_trace) + max_trace_buffers * TRACE_BUFFER_SIZE + panic_sideband * 2 * SIDEBAND_BUFFER_SIZE;
    /* Override max_trace_buffers in Panic tracing mode */
    max_trace_buffers = NR_CPUS;

    panic_trace_buffer = vmalloc(gbuffer_size);
    if ( !panic_trace_buffer ) {
        printk(KERN_ERR "SAT: could not allocate panic buffer\n");
        return -ENOMEM;
    }

    memset(panic_trace_buffer, 0, gbuffer_size);

    memset(panic_trace_buffer, 0, gbuffer_size);
    memcpy(panic_trace.marker, PANIC_TRACE_MARKER, sizeof(PANIC_TRACE_MARKER));
    panic_trace.marker[3] = PANIC_TRACE_VERSION;

    if (panic_sideband)
      panic_trace.sideband = 1;
    panic_trace.number_of_cpus = NR_CPUS;

    return 0;
}

static void trace_stop(void) {
    u32 cpu;
    uint32_t msr_trace_ctl;
    if (processor_trace_version == TRACE_IPT)
        msr_trace_ctl = SAT_MSR_IA32_IPT_CTL;
    else
        msr_trace_ctl = SAT_MSR_IA32_RTIT_CTL;

    for_each_present_cpu(cpu) {
        u32 data[2];
        int err = rdmsr_safe_on_cpu(cpu, msr_trace_ctl,
            &data[0], &data[1]);
        generate_STS();
        if (!err && (data[0] & TRACE_CTL_TRACEEN)) {
            /* Read the processor trace config we were running */
            panic_trace.ctl_reg = data[0];
            wrmsr_safe_on_cpu(cpu, msr_trace_ctl,
            data[0] & ~TRACE_CTL_TRACEEN, data[1]);
        }
    }
}

static phys_addr_t get_trace_buffer(char *buffer, int cpu) {
  trace_type *trace;
  char *trace_virt_addr;
  long long offset;

  if (cpu < 0 || cpu >= NR_CPUS || buffer == NULL)
    return -EBADF;

  offset = panic_trace.buffer_infos[cpu].offset;
  trace = per_cpu(processor_tracers, cpu).trace;
  trace_virt_addr = buffer_pool.address[trace->buffer_id[trace->active_idx]].virt;

  /* If trace offset is available, wrap buffer around */
  if (offset != -1) {
    /* Wrap buffer around */
    memcpy(buffer, trace_virt_addr + offset, TRACE_BUFFER_SIZE - offset);
    memcpy(buffer + TRACE_BUFFER_SIZE - offset, trace_virt_addr, offset);
  }
  else {
    memcpy(buffer, trace_virt_addr, TRACE_BUFFER_SIZE);
  }
  return buffer_pool.address[trace->buffer_id[trace->active_idx]].phys;
}

static void read_trace_offsets(void) {
    int cpu;

    for_each_present_cpu(cpu) {
        u32 data[2];
        int err;
        if (processor_trace_version == TRACE_IPT)
            err = rdmsr_safe_on_cpu(cpu, SAT_MSR_IA32_IPT_OFFSET, &data[0], &data[1]);
        else
            err = rdmsr_safe_on_cpu(cpu, SAT_MSR_IA32_RTIT_OFFSET, &data[0], &data[1]);
        if (!err)
            panic_trace.buffer_infos[cpu].offset = (int)data[0];
        else
            panic_trace.buffer_infos[cpu].offset = -1;
    }
}

static void panic_wrapper(struct pt_regs *regs)
{
    typeof(&panic_wrapper)run;
    run = panic_hook_address.run;

    if (regs == NULL) {
        trace_stop();
        read_trace_offsets();
    }

    run(regs);
}

static struct symbol_hook panic_symbol_hook =
    /* Closes to panic function would be crash_kexec_wrapper */
    { "crash_kexec", &panic_hook_address, panic_wrapper, 0 };


static int __hijack_panic_call(void *unused) {
    symbol_hijack(panic_symbol_hook.sym);
    return 0;
}

static int __undo_hijack_panic_call(void *unused) {
    symbol_restore(panic_symbol_hook.sym);
    return 0;
}

static int sat_panic_call_handler(struct notifier_block *this,
                                unsigned long event,
                                void *ptr)
{
#ifdef CONFIG_EMMC_IPANIC
  struct g_buffer_header panic_g_buffer;
#endif /* CONFIG_EMMC_IPANIC */
  int cpu;
  int sb_total_size = 0;

  /* If panic wrapper is not in use, stop tracing and fetch offsets */
  if (panic_tracer!=2) {
    trace_stop();
    read_trace_offsets();
  }

/*  for_each_online_cpu for_each_possible_cpu(cpu) */
  for_each_present_cpu(cpu) {
    panic_trace.buffer_infos[cpu].cpu_id = cpu;
    panic_trace.buffer_infos[cpu].base_address =
      get_trace_buffer(panic_trace_buffer + sizeof(panic_trace) + TRACE_BUFFER_SIZE*cpu,cpu); /*+sizeof(trace_panic_header_type)*/
  }

  if (panic_sideband) {
    panic_trace.sideband = get_sideband_data(panic_trace_buffer + sizeof(panic_trace) + TRACE_BUFFER_SIZE*(cpu), 0, 0,  &sb_total_size);
  }

  memcpy(panic_trace_buffer, &panic_trace, sizeof(panic_trace));

  /* Printing info for ramdump kernel */
  if (1)  {
    printk(KERN_ALERT "SAT: number_of_cpus=%d\n",panic_trace.number_of_cpus);
    printk(KERN_ALERT "SAT: sideband=%d\n",panic_trace.sideband);
    printk(KERN_ALERT "SAT: ctl_reg=%x\n", panic_trace.ctl_reg);
    for_each_present_cpu(cpu) {
      printk(KERN_ALERT "SAT: cpu_id=%d\n",panic_trace.buffer_infos[cpu].cpu_id);
      printk(KERN_ALERT "SAT: offset=%x\n",panic_trace.buffer_infos[cpu].offset);
      printk(KERN_ALERT "SAT: base_address=%llx\n", (uint64_t) panic_trace.buffer_infos[cpu].base_address);
    }
  }
#ifdef CONFIG_EMMC_IPANIC
  if (panic_gbuffer) {
    /* Read buffer size */
    panic_g_buffer.size = (size_t)(sizeof(panic_trace) + TRACE_BUFFER_SIZE*NR_CPUS + panic_sideband*SIDEBAND_BUFFER_SIZE*2);

    /* Provide buffer base address */
    panic_g_buffer.base = (unsigned char *)panic_trace_buffer;

    /* Read buffer write offset */
    panic_g_buffer.woff = panic_g_buffer.size;
    panic_g_buffer.head = 0;

    panic_set_gbuffer(&panic_g_buffer);
  }
#endif /* CONFIG_EMMC_IPANIC */
  printk(KERN_ALERT "SAT sat_panic_call_handler DONE\n");

  return NOTIFY_DONE;
}

static struct notifier_block panic_sat_setup = {
  .notifier_call = sat_panic_call_handler,
  .next = NULL,
  .priority = INT_MAX
};

int enable_panic_handler(void) {
  int err = 0;
  err = atomic_notifier_chain_register(&panic_notifier_list,
                                        &panic_sat_setup);

  if (err)
    return err;

  if (panic_tracer == 2) {
    /* Initialize borrowed kernel functions */
    kernfunc_init();

    err = symbol_prepare_for_hijack(panic_symbol_hook.sym,
      panic_symbol_hook.name,
      panic_symbol_hook.func);

    if (IN_ERR(err)) {
      printk(KERN_ERR "SAT: panic hook prepare failed\n");
      return err;
    }
    stop_machine(__hijack_panic_call, NULL, NULL);
  }

  return err;
}

int disable_panic_handler(void) {
  int err = 0;
  err = atomic_notifier_chain_unregister(&panic_notifier_list,
                                    &panic_sat_setup);

  if (panic_tracer == 2) {
    stop_machine(__undo_hijack_panic_call, NULL, NULL);
  }

  return err;
}

#endif /* PANIC_TRACER */
