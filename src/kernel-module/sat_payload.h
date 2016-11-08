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
#ifndef SAT_PAYLOAD_H
#define SAT_PAYLOAD_H

#if defined(__KERNEL__)
#include <linux/types.h>
#else /* defined(__KERNEL__) */
#include <stdint.h>
#endif /* !defined(__KERNEL__) */

#define SAT_MAX_PATH 160 /* well above typical longest path on android */
#define SAT_DATA_SIZE 256
#define SAT_GEN_NAME_SIZE 5

/* 12 bytes version string */
#define SIDEBAND_VERSION "SATSIDEB0004"

typedef enum {
	SAT_MSG_PROCESS = 1,		/* ABI1 */
	SAT_MSG_MMAP,				/* ABI1 */
	SAT_MSG_MUNMAP,				/* ABI1 */
	SAT_MSG_INIT,
	SAT_MSG_PROCESS_EXIT,
	SAT_MSG_SCHEDULE,
	SAT_MSG_HOOK,				/* ABI1 */
	SAT_MSG_MODULE,				/* ABI1 */
	SAT_MSG_PROCESS_ABI2,
	SAT_MSG_MMAP_ABI2,
	SAT_MSG_MUNMAP_ABI2,
	SAT_MSG_HOOK_ABI2,
	SAT_MSG_MODULE_ABI2,
	SAT_MSG_HOOK_ABI3,
	SAT_MSG_GENERIC,
	SAT_MSG_CODEDUMP,
	SAT_MSG_SCHEDULE_ABI2,
	SAT_MSG_SCHEDULE_ABI3,
	SAT_MSG_SCHEDULE_ID,
    SAT_MSG_INIT_ABI2,
} sat_type;

typedef struct sat_header {
	uint32_t size; /* size of the whole msg */
	uint32_t type; /* sat_type */
	uint64_t tscp; /* rdtscp timestamp info */
	uint32_t cpu;  /* cpu from tscp read */
	uint32_t tsc_offset; /* IA32_TSC_OFFSET */
} __attribute__((packed)) sat_header;

typedef enum {
	SAT_ORIGIN_INIT = 1,
	SAT_ORIGIN_FORK,
	SAT_ORIGIN_MMAP,
	SAT_ORIGIN_MPROTECT,
	SAT_ORIGIN_MUNMAP,
	SAT_ORIGIN_SET_TASK_COMM
} sat_origin;

typedef struct sat_msg_process {
	sat_header header; /* sat_origin: INIT, FORK or SET_TASK_COMM */
	uint32_t   origin;
	int32_t    pid;
	int32_t    ppid;
	int32_t    tgid;
	uint32_t   pgd;
	char       name[SAT_MAX_PATH];
} __attribute__((packed)) sat_msg_process;

typedef struct sat_msg_mmap {
	sat_header header;
	uint32_t   origin; /* sat_origin: INIT, MMAP or MPROTECT */
	int32_t    pid; /* tgid */
	uint32_t   start;
	uint32_t   len;
	uint32_t   pgoff;
	char       path[SAT_MAX_PATH];
} __attribute__((packed)) sat_msg_mmap;

typedef struct sat_msg_munmap {
	sat_header header;
	uint32_t   origin; /* sat_origin: MUNMAP or MPROTECT */
	int32_t    pid; /* tgid */
	uint32_t   start;
	uint32_t   len;
} __attribute__((packed)) sat_msg_munmap;

typedef struct sat_msg_init {
	sat_header header;
	int32_t    pid;
	int32_t    tgid;
	uint32_t   tsc_tick;
	uint32_t   fsb_mhz;
} __attribute__((packed)) sat_msg_init;

typedef struct sat_msg_schedule {
	sat_header header;
	int32_t    pid;
	int32_t    tgid;
	int32_t    prev_pid;
	int32_t    prev_tgid;
	uint32_t   trace_pkt_count;
} __attribute__((packed)) sat_msg_schedule;

typedef struct sat_msg_hook {
	sat_header header;
	uint32_t   org_addr;
	uint32_t   new_addr;
	uint32_t   size;
	char       name[SAT_MAX_PATH];
} __attribute__((packed)) sat_msg_hook;

typedef struct sat_msg_module {
	sat_header header;
	uint32_t   addr;
	char       name[SAT_MAX_PATH];
} __attribute__((packed)) sat_msg_module;

/* ABI2 versions */
typedef struct sat_msg_process_abi2 {
	sat_header header; /* sat_origin: INIT, FORK or SET_TASK_COMM */
	uint32_t   origin;
	int32_t    pid;
	int32_t    ppid;
	int32_t    tgid;
	uint64_t   pgd;
	char       name[SAT_MAX_PATH];
} __attribute__((packed)) sat_msg_process_abi2;

typedef struct sat_msg_mmap_abi2 {
	sat_header header;
	uint32_t   origin; /* sat_origin: INIT, MMAP or MPROTECT */
	int32_t    tgid;
	uint64_t   start;
	uint64_t   len;
	uint64_t   pgoff;
	char       path[SAT_MAX_PATH];
} __attribute__((packed)) sat_msg_mmap_abi2;

typedef struct sat_msg_munmap_abi2 {
	sat_header header;
	uint32_t   origin; /* sat_origin: MUNMAP or MPROTECT */
	int32_t    tgid;
	uint64_t   start;
	uint64_t   len;
} __attribute__((packed)) sat_msg_munmap_abi2;

typedef struct sat_msg_hook_abi2 {
	sat_header header;
	uint64_t   org_addr;
	uint64_t   new_addr;
	uint64_t   size;
	char       name[SAT_MAX_PATH];
} __attribute__((packed)) sat_msg_hook_abi2;

typedef struct sat_msg_module_abi2 {
	sat_header header;
	uint64_t   addr;
	uint64_t   size;
	char       name[SAT_MAX_PATH];
} __attribute__((packed)) sat_msg_module_abi2;

typedef struct sat_msg_hook_abi3 {
	sat_header header;
	uint64_t   org_addr;
	uint64_t   new_addr;
	uint64_t   size;
	uint64_t   wrapper_addr;
	char       name[SAT_MAX_PATH];
} __attribute__((packed)) sat_msg_hook_abi3;

typedef struct sat_msg_generic {
	sat_header header;
	char       name[SAT_GEN_NAME_SIZE];
	char       data[SAT_DATA_SIZE];
} __attribute__((packed)) sat_msg_generic;

typedef struct sat_msg_codedump {
	sat_header header;
	uint64_t   addr;
	uint64_t   size;
	char       name[SAT_MAX_PATH];
} __attribute__((packed)) sat_msg_codedump;

typedef struct sat_msg_schedule_abi2 {
	sat_header header;
	int32_t    pid;
	int32_t    tgid;
	int32_t    prev_pid;
	int32_t    prev_tgid;
	uint32_t   trace_pkt_count;
	uint64_t   buff_offset;
} __attribute__((packed)) sat_msg_schedule_abi2;

typedef struct sat_msg_schedule_abi3 {
	sat_header header;
	int32_t    pid;
	int32_t    tgid;
	int32_t    prev_pid;
	int32_t    prev_tgid;
	uint32_t   trace_pkt_count;
	uint64_t   buff_offset;
	uint8_t    schedule_id;
} __attribute__((packed)) sat_msg_schedule_abi3;

typedef struct sat_msg_schedule_id {
	sat_header header;
	uint64_t   addr;
	uint8_t    id;
} __attribute__((packed)) sat_msg_schedule_id;

typedef struct sat_msg_init_abi2 {
	sat_header header;
	int32_t    pid;
	int32_t    tgid;
	uint32_t   tsc_tick;
	uint32_t   fsb_mhz;
	uint32_t   tma_ratio_tsc;
	uint32_t   tma_ratio_ctc;
	uint8_t    mtc_freq;
} __attribute__((packed)) sat_msg_init_abi2;

typedef union sat_msg {
	sat_header header;
	sat_msg_process process;
	sat_msg_mmap mmap;
	sat_msg_munmap munmap;
	sat_msg_init init;
	sat_msg_schedule schedule;
	sat_msg_hook hook;
	sat_msg_module module;
	sat_msg_process_abi2 process_abi2;
	sat_msg_mmap_abi2 mmap_abi2;
	sat_msg_munmap_abi2 munmap_abi2;
	sat_msg_hook_abi2 hook_abi2;
	sat_msg_module_abi2 module_abi2;
	sat_msg_hook_abi3 hook_abi3;
	sat_msg_generic generic;
	sat_msg_codedump codedump;
	sat_msg_schedule_abi2 schedule_abi2;
	sat_msg_schedule_abi3 schedule_abi3;
	sat_msg_schedule_id schedule_id;
    sat_msg_init_abi2 init_abi2;
} sat_msg;
#endif /* SAT_PAYLOAD_H */
