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
#ifndef CONFIG_H
#define CONFIG_H

//#undef DEBUG
#define DEBUG

/* Masterchannel to use when logging sideband data to PTI */
#define STP_MC 0x0

/* USE_NATIVE_READ_TSCP */
/* If defined use native_read_tscp to get tsc count, if not use native_read_tsc */
#define CONFIG_BUFFER_OFFSET_IN_SCHEDULE

#define CONFIG_SCHEDULE_ID_IN_SCHEDULE

#define INDIRECT_MODULE_DUMP

#define CONFIG_TSC_BEFORE_SLEEP
/* Power Monitor Enable / Disable */
//#define POWER_MONITORING_FROM_FUEL_GAUGE

/* Panic Tracer */
/* Flag to control if Panic Tracer is enabled */
//#define PANIC_TRACER
/*
  Panic tracer allows processor trace post processing after kernel panic,
  using eather SAT-panic tracing or "RAMDUMP" and Lautherbach T32
  Usage:
  insmod sat.ko panic_tracer=2 trace_method=1 exclude_userspace=1
  insmod sat.ko panic_tracer=2 trace_method=1 exclude_userspace=1 panic_sideband=1
*/
#ifdef PANIC_TRACER
extern int panic_tracer;
extern int panic_sideband;
#ifdef CONFIG_EMMC_IPANIC
extern int panic_gbuffer;
#endif /* CONFIG_EMMC_IPANIC */
#endif /* PANIC_TRACER */

/* processor trace buffer config */
#define TRACE_BUFFER_ORDER 10 /* 4kB page * 2^10 = 4MB */
#define TRACE_BUFFER_SIZE (PAGE_SIZE << TRACE_BUFFER_ORDER) /* 4MB */
#define MAX_TRACE_BUFFERS 32 /* max trace buffer pool size 32 * 4MB = 128MB */

/* Processor trace MSR configs */
#define PSB_FREQ_VALUE	 2	/* PSB is sent when ~8192 processor trace packets are passed */

/* Processor trace version Intel PT or RTIT */
enum {
    NO_TRACE = 0,
    TRACE_RTIT,
    TRACE_IPT
};
extern int processor_trace_version;

/* Tracing method for data: 0=PTI (default), 1=RAM */
enum {
    LOG_TO_PTI = 0,
    LOG_TO_RAM = 1,
};
extern int trace_method;

/* Module param to enable / disable ring buffer tracing */
extern int ring_buffer_tracing;

/* SB ringbuffer tracing */
extern struct work_struct    sb_init_work;

/*
 * trigger point: (buffer_size / level) * (level-1)
 *  value 10 means buffer is switched when the free
 *  space is less than 1/10 of the total buffer size
 */
#define SIDEBAND_SWITCH_TRIGGER_LEVEL 10

extern int sideband_buffer_count;
#define SIDEBAND_BUFFER_SIZE (PAGE_SIZE << 10)

/* Method for sideband data: 0=PTI, 1=RAM (default) */
extern int sideband_log_method;

/* Amount of trace buffers for mem trace (default 32) */
extern int max_trace_buffers;

/* Enable / Disable power monitor: 0=Disabled (Default), 1-1000=ms sampling period */
extern int power_monitor;

/* Flag to enable starting / stopping through memory address */
#undef EXTERNAL_TRACE_START_AND_STOP

#endif /* CONFIG_H */
