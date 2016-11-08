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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mount.h>
#include <linux/spinlock.h>
#include <linux/stop_machine.h>
#include <linux/debugfs.h>
#include <asm/tsc.h>
#include <cpuid.h>
#include "external/module.h"
#ifdef CONFIG_INTEL_PTI_STM
#include <asm/intel-mid.h>
#include <linux/pti.h>
#include <linux/lnw_gpio.h>
#endif /* CONFIG_INTEL_PTI_STM */
#include "config.h"
#include "sat-tracer.h"
#include "sideband_tracer.h"
#include "main.h"
#ifdef PANIC_TRACER
#include "sat-panic.h"
#endif /* PANIC_TRACER */
#include "power-mon.h"
#include <bitops.h>
#ifdef INDIRECT_MODULE_DUMP
#include <linux/slab.h>
#include <linux/vmalloc.h>
#endif
#ifdef CONFIG_TSC_BEFORE_SLEEP
#include <linux/cpuidle.h>
#endif


extern struct task_struct *switch_to_wrapper_ipt(struct task_struct *prev,
						struct task_struct *next);

extern struct task_struct *switch_to_wrapper_rtit(struct task_struct *prev,
						struct task_struct *next);

#ifdef EXTERNAL_TRACE_START_AND_STOP
#include "ext-start-stop.h"
#endif /* EXTERNAL_TRACE_START_AND_STOP */

#ifdef CONFIG_INTEL_PTI_STM
extern int intel_scu_ipc_command(u32 cmd, u32 sub, u8 *in,
				 u32 inlen, u32 *out, u32 outlen) __attribute__((weak));
#define IPC_CMD_PTI_CLOCK_CTRL 0xED
#endif /* CONFIG_INTEL_PTI_STM */

DEFINE_PER_CPU(uint8_t, schedule_id) = 0;
#define SCHEDULE_ID_COUNT_MAX 7

static int hooking_done = false;
static int trace_active = false;
static int intel_pt_running = false;

/* *** Module parameters *** */
#ifdef PANIC_TRACER
int panic_tracer = 0;
module_param(panic_tracer, int, 0444);
MODULE_PARM_DESC(panic_tracer,
	"Enable panic tracer 0=Disable (Default), 1=Enable, 2=Enable (Hooked mode)");
#ifdef CONFIG_EMMC_IPANIC
int panic_gbuffer = 1;
module_param(panic_gbuffer, int, 0444);
MODULE_PARM_DESC(panic_gbuffer,
	"Panic output to Gbuffer 0=Disable, 1=Enabled (Default)");
#endif /* CONFIG_EMMC_IPANIC */
int panic_sideband = 0;
module_param(panic_sideband, int, 0444);
MODULE_PARM_DESC(panic_sideband,
	"Enable Sideband to panic tracer 0=Disable (Default), 1=Enabled");
#endif /* PANIC_TRACER */
int ring_buffer_tracing = 0;
module_param(ring_buffer_tracing, int, 0444);
MODULE_PARM_DESC(ring_buffer_tracing,
	"Enable ring buffer tracing for processor trace and sideband 0=Disable (Default), 1=Enabled");
int trace_method = LOG_TO_RAM;
module_param(trace_method, int, 0444);
MODULE_PARM_DESC(trace_method,
		 "Method to use for trace data: 0=PTI, 1=RAM (Default)");
int sideband_log_method = LOG_TO_RAM;
module_param(sideband_log_method, int, 0444);
MODULE_PARM_DESC(sideband_log_method,
		 "Method to use for sideband data: 0=PTI, 1=RAM (Default)");
bool exclude_kernel = false;
module_param(exclude_kernel, bool, 0444);
MODULE_PARM_DESC(exclude_kernel,
		 "Exclude kernelspace from the trace");
bool exclude_userspace = false;
module_param(exclude_userspace, bool, 0444);
MODULE_PARM_DESC(exclude_userspace,
		 "Exclude userspace from the trace");
int max_trace_buffers = MAX_TRACE_BUFFERS;
module_param(max_trace_buffers, int, 0444);
MODULE_PARM_DESC(max_trace_buffers,
                 "Amount of trace buffers in use for Mem trace");
/* default path */
char *sat_path = "/data/sat.ko";
module_param(sat_path, charp, 0444);
MODULE_PARM_DESC(sat_path,
                 "SAT kernel module load path");

#ifdef POWER_MONITORING_FROM_FUEL_GAUGE
int power_monitor = 0;
module_param(power_monitor, int, 0644);
MODULE_PARM_DESC(power_monitor,
		 "Enable / Disable power monitor: 0=Disabled (Default), 1-1000=ms sampling period ");
#endif /* POWER_MONITORING_FROM_FUEL_GAUGE */

uint32_t tma_ratio_tsc = 1;
uint32_t tma_ratio_ctc = 1;

int processor_trace_version = NO_TRACE;

static struct dentry *sat_dir_dent;
static struct dentry *control_file;
static char control_buf[128];
static unsigned capture_on;

static struct dentry *ko_fetch_addr;
static uint64_t *ko_fetch_address;
static struct dentry *ko_fetch_size;
static uint32_t ko_fetch_length;
static struct dentry *ko_fetch_data;

#ifdef CONFIG_INTEL_PTI_STM
static enum intel_mid_cpu_type cpu_type;
static struct pti_masterchannel *mc = NULL;
#endif /* CONFIG_INTEL_PTI_STM */
#if defined(CONFIG_INTEL_PTI_STM) || !defined(USE_NATIVE_READ_TSCP)
static DEFINE_SPINLOCK(sat_lock);
#endif

atomic_t hijack_callers = ATOMIC_INIT(0);

asmlinkage __visible void *switch_to_ptr = NULL;
struct kernsym switch_to_address;
struct kernsym mmap_address;
struct kernsym task_fork_fair_address;
struct kernsym do_munmap_address;
struct kernsym lnw_gpio_set_alt_address;
struct kernsym gpio_request_address;
struct kernsym set_task_comm_address;
struct kernsym intel_idle_address;
void sb_buffer_init(struct work_struct *work);

/* Workqueue for sideband buffer swap */
struct work_struct		sb_init_work;

u64 satt_rdtsc(void)
{
     unsigned int low, high;
     asm volatile("rdtsc" : "=a" (low), "=d" (high));
     return low | ((u64)high) << 32;
}

/*
 * Get Current CPU's tsc offset
 */
static int get_tsc_offset(void) {
	int tsc_offset;
	rdmsrl(SAT_MSR_IA32_TSC_ADJUST, tsc_offset);
	return tsc_offset;
}

/*
 * Send version msg to sideband stream
 */

static void send_pti_sb_identification_msg(void)
{
#ifdef CONFIG_INTEL_PTI_STM
	unsigned long flags;
	char msg[12] = SIDEBAND_VERSION; // "SATSIDEBxxxx"

	if (sideband_log_method == LOG_TO_PTI) {
		spin_lock_irqsave(&sat_lock, flags);
		pti_writedata(mc, msg, sizeof(msg), false);
		spin_unlock_irqrestore(&sat_lock, flags);
	}
#endif
}

/**
 * send_sat_msg() - Send a message
 *
 * @msg: The message to send
 */
void send_sat_msg(sat_msg *msg)
{
#if defined(CONFIG_INTEL_PTI_STM) || !defined(USE_NATIVE_READ_TSCP)
	unsigned long flags;
#endif

	if (!trace_active)
		return;

	msg->header.tsc_offset = get_tsc_offset();

	if (sideband_log_method == LOG_TO_RAM) {
#ifdef USE_NATIVE_READ_TSCP
		msg->header.tscp = native_read_tscp(&msg->header.cpu);
#else /* USE_NATIVE_READ_TSC */
		spin_lock_irqsave(&sat_lock, flags);
		msg->header.tscp = satt_rdtsc();
		msg->header.cpu = smp_processor_id();
		spin_unlock_irqrestore(&sat_lock, flags);
#endif
		sb_trace((char *)msg, msg->header.size);
	}
#ifdef CONFIG_INTEL_PTI_STM
	else if (sideband_log_method == LOG_TO_PTI) {
		spin_lock_irqsave(&sat_lock, flags);
		pti_writedata(mc, (char *)msg, msg->header.size, false);
		spin_unlock_irqrestore(&sat_lock, flags);
	}
#endif /* CONFIG_INTEL_PTI_STM */
}

/**
 * send_process() - Send information about a process
 *
 * @origin: The origin of the message
 * @pid: The process ID
 * @ppid: The process parent ID
 * @tgid: The thread-group ID
 * @pgd: The Page Global Directory-entry
 * @name: The process name
 */
static void send_process(sat_origin origin,
			 pid_t pid,
			 pid_t ppid,
			 pid_t tgid,
			 unsigned long pgd,
			 const char *name)
{
	sat_msg umsg;
#ifdef __i386__
	sat_msg_process *msg = &umsg.process;
	msg->header.type = SAT_MSG_PROCESS;
#else
	sat_msg_process_abi2 *msg = &umsg.process_abi2;
	msg->header.type = SAT_MSG_PROCESS_ABI2;
#endif
	msg->origin      = origin;
	msg->pid         = pid;
	msg->ppid        = ppid;
	msg->tgid        = tgid;
	msg->pgd         = pgd;
	msg->header.size = sizeof(*msg);

	memset(msg->name, '\0', sizeof(msg->name));

	if (name)
		strncpy(msg->name, name, sizeof(msg->name) - 1);

	/* do not send trailing nulls */
	msg->header.size -= sizeof(msg->name) - (strlen(msg->name) + 1);

	send_sat_msg(&umsg);
}

static void send_mmap(sat_origin origin,
		      pid_t tgid,
		      unsigned long start,
		      unsigned long len,
		      unsigned long pgoff,
		      const struct path *path,
		      const char* name)
{
	sat_msg umsg;
#ifdef __i386__
	sat_msg_mmap *msg = &umsg.mmap;
	msg->header.type = SAT_MSG_MMAP;
	msg->pid         = tgid;
#else
	sat_msg_mmap_abi2 *msg = &umsg.mmap_abi2;
	msg->header.type = SAT_MSG_MMAP_ABI2;
	msg->tgid        = tgid;
#endif
	msg->origin      = origin;
	msg->start       = start;
	msg->len         = len;
	msg->pgoff       = pgoff;

	msg->header.size = sizeof(*msg);
	memset(msg->path, '\0', sizeof(msg->path));

	if (path) {
		char *p = d_path(path, msg->path, sizeof(msg->path));

		if (IS_ERR(p))
			strcpy(msg->path, "?");
		else
			memmove(msg->path, p, sizeof(msg->path) - (p - msg->path));
	} else if (name) {
		strncpy(msg->path, name, sizeof(msg->path));
	} else {
		strcpy(msg->path, "?");
	}

#if 0
	trace_printk(KERN_ALERT "SAT: PID %i: %s(%p, %x, %x) on %s\n",
		     msg.pid,
		     msg.origin == SAT_ORIGIN_INIT ? "sat init" :
						     "mmap/mprotect",
		     (void *)msg.start, msg.len, msg.pgoff, msg.path);
#endif

	/* do not send trailing nulls */
	msg->header.size -= sizeof(msg->path) - (strlen(msg->path) + 1);

	send_sat_msg(&umsg);
}

static void send_munmap(sat_origin origin,
			pid_t tgid,
			unsigned long start,
			unsigned long len)
{
	sat_msg umsg;
#ifdef __i386__
	sat_msg_munmap *msg = &umsg.munmap;
	msg->header.type = SAT_MSG_MUNMAP;
	msg->pid         = tgid;
#else
	sat_msg_munmap_abi2 *msg = &umsg.munmap_abi2;
	msg->header.type = SAT_MSG_MUNMAP_ABI2;
	msg->tgid        = tgid;
#endif
	msg->origin      = origin;
	msg->start       = start;
	msg->len         = len;

	msg->header.size = sizeof(*msg);
	send_sat_msg(&umsg);
}

/**
 * get_fsb_speed() - Retrieve the FSB speed of an Intel CPU
 *
 * Returns the speed in MHz, rounded up, of the Front Side Bus of
 * an Intel CPU that supports the MSR_FSB_FREQ call
 *
 * Return: FSB speed in MHz; 0 on failure
 */
static uint32_t get_fsb_speed(void)
{
	uint32_t fsb = 0;
	uint32_t msr_lo, msr_tmp;
	//TODO Skylake support missing
	rdmsr_safe(MSR_FSB_FREQ, &msr_lo, &msr_tmp);

	switch (msr_lo & 0x7) {
	case 5:
		fsb = 100000;	/* 100MHz */
		break;

	case 1:
		fsb = 133333;	/* 133MHz */
		break;

	case 3:
		fsb = 166667;	/* 167MHz */
		break;

	case 2:
		fsb = 200000;	/* 200MHz */
		break;

	case 0:
		fsb = 266667;	/* 267MHz */
		break;

	case 4:
		fsb = 333333;	/* 333MHz */
		break;

	default:
		printk(KERN_ERR "PCORE - MSR_FSB_FREQ undefined value");
		break;
	}

	return DIV_ROUND_CLOSEST(fsb, 1000);
}

static void send_init(pid_t pid, pid_t tgid)
{
	sat_msg umsg;
    uint64_t reg;

    sat_msg_init_abi2 *msg = &umsg.init_abi2;
    msg->header.type = SAT_MSG_INIT_ABI2;
	msg->header.size = sizeof(*msg);

	msg->pid           = pid;
	msg->tgid          = tgid;
	msg->tsc_tick      = tsc_khz;
	msg->fsb_mhz       = get_fsb_speed();
    msg->tma_ratio_tsc = tma_ratio_tsc;
    msg->tma_ratio_ctc = tma_ratio_ctc;
    rdmsrl(SAT_MSR_IA32_IPT_CTL, reg);
    msg->mtc_freq      = TRACE_CTL_IPT_MTC_FREQ_READ(reg);

	send_sat_msg(&umsg);
}

void satt_schedule_id_0(void) {;}
void satt_schedule_id_1(void) {;}
void satt_schedule_id_2(void) {;}
void satt_schedule_id_3(void) {;}
void satt_schedule_id_4(void) {;}
void satt_schedule_id_5(void) {;}
void satt_schedule_id_6(void) {;}
void satt_schedule_id_7(void) {;}

typedef void (*schedule_inject_type)(void);
const schedule_inject_type schedule_inject[] = {
	satt_schedule_id_0,
	satt_schedule_id_1,
	satt_schedule_id_2,
	satt_schedule_id_3,
	satt_schedule_id_4,
	satt_schedule_id_5,
	satt_schedule_id_6,
	satt_schedule_id_7 };

void satt_inject_schedule_id_to_trace(uint8_t schedule_id)
{
	schedule_inject_type schedule_func = schedule_inject[schedule_id];
	schedule_func();
}

asmlinkage __visible void send_schedule(struct task_struct *prev,
			struct task_struct *next)
//static void send_schedule(struct task_struct *prev,
//			struct task_struct *next)
{
	uint64_t pktc;
	uint64_t offset = 0;
	uint8_t  schedule_id_counter;
	sat_msg umsg;
    uint64_t ctl;
	trace_type *trace;

#ifdef CONFIG_SCHEDULE_ID_IN_SCHEDULE
	sat_msg_schedule_abi3 *msg = &umsg.schedule_abi3;
#elif defined (CONFIG_BUFFER_OFFSET_IN_SCHEDULE)
	sat_msg_schedule_abi2 *msg = &umsg.schedule_abi2;
#else
	sat_msg_schedule *msg = &umsg.schedule;
#endif
#ifdef CONFIG_INTEL_PTI_STM
	unsigned long flags;
#endif

	// Get CPU specific and update schedule id counter
	schedule_id_counter = __this_cpu_read(schedule_id)  + 1;
	if (schedule_id_counter > SCHEDULE_ID_COUNT_MAX) {
		schedule_id_counter = 0;
	}
 	__this_cpu_write(schedule_id, schedule_id_counter);

	if (!trace_active)
		return;
	msg->header.size = sizeof(*msg);
#ifdef CONFIG_SCHEDULE_ID_IN_SCHEDULE
	msg->header.type = SAT_MSG_SCHEDULE_ABI3;
#elif defined (CONFIG_BUFFER_OFFSET_IN_SCHEDULE)
	msg->header.type = SAT_MSG_SCHEDULE_ABI2;
#else
	msg->header.type = SAT_MSG_SCHEDULE;
#endif
	msg->pid         = next->pid;
	msg->tgid        = next->tgid;
	msg->prev_pid    = prev->pid;
	msg->prev_tgid   = prev->tgid;

#ifndef USE_NATIVE_READ_TSCP
	msg->header.cpu = smp_processor_id();
#endif
#ifdef CONFIG_SCHEDULE_ID_IN_SCHEDULE
	msg->schedule_id = schedule_id_counter;
#endif

	if (sideband_log_method == LOG_TO_RAM) {
		/* Populate msg.trace_pkg_count */
		if (processor_trace_version == TRACE_RTIT) {
			rdmsrl(SAT_MSR_IA32_RTIT_PKT_CNT, pktc);
		} else {
			rdmsrl(SAT_MSR_IA32_IPT_STATUS, pktc);
			pktc = (pktc >> 32) & 0x1ffff;
		}
		msg->header.tsc_offset = get_tsc_offset();
		msg->trace_pkt_count = (uint32_t) pktc;
#ifdef USE_NATIVE_READ_TSCP
		msg->header.tscp = native_read_tscp(&msg->header.cpu);
#else /* USE_NATIVE_READ_TSC */
		msg->header.tscp = satt_rdtsc();
#endif
#ifdef CONFIG_BUFFER_OFFSET_IN_SCHEDULE

        if (processor_trace_version == TRACE_RTIT) {
            rdmsrl(SAT_MSR_IA32_RTIT_OFFSET, offset);
		} else {
            rdmsrl(SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS, offset);
            offset = (offset >> 32) & 0xffffffff;
        }
		trace = this_cpu_ptr(&processor_tracers)->trace;
		msg->buff_offset = trace->size + offset;
#endif
		sb_trace((char *)msg, msg->header.size);
	} else if (sideband_log_method == LOG_TO_PTI) {
		/* Populate msg.trace_pkg_count */
		if (processor_trace_version == TRACE_RTIT) {
			rdmsrl(SAT_MSR_IA32_RTIT_PKT_CNT, pktc);
		} else {
			rdmsrl(SAT_MSR_IA32_IPT_STATUS, pktc);
			pktc = (pktc >> 32) & 0x1ffff;
		}
		msg->header.tsc_offset = get_tsc_offset();
		msg->trace_pkt_count = (uint32_t) pktc;
#ifdef USE_NATIVE_READ_TSCP
		msg->header.tscp = native_read_tscp(&msg->header.cpu);
#else /* USE_NATIVE_READ_TSC */
		msg->header.tscp = satt_rdtsc();
#endif
#ifdef CONFIG_BUFFER_OFFSET_IN_SCHEDULE
		msg->buff_offset = 0;
#endif
#ifdef CONFIG_INTEL_PTI_STM
		spin_lock_irqsave(&sat_lock, flags);
		pti_writedata(mc, (char *)msg, msg->header.size, false);
		spin_unlock_irqrestore(&sat_lock, flags);
#endif /* CONFIG_INTEL_PTI_STM */
	}
	/* Inject Schdule id to trace */
	satt_inject_schedule_id_to_trace(schedule_id_counter);

	if (processor_trace_version == TRACE_IPT) {
		// && intel_pt_running == true) {
		/* TODO: Do we need to check intel_pt_running?
		     If not checked, we can start toggling trace pgd/pge
		     immediately when trace is enabled by HW, no need to
		     wait for intel_pt_running flag to raise. We are anyway
		     in middle of scheduling when this code is executed, so
		     there can't be race condition between trace stop &
		     this pgd/pge toggling (e.i. trace stop code can't be
		     executed in middle of this toggling.)
		*/
		// Stop&start IPT trace
		rdmsrl(SAT_MSR_IA32_IPT_CTL, ctl);

		if (ctl & TRACE_CTL_TRACEEN) {
			ctl &= ~TRACE_CTL_TRACEEN;
			wrmsrl(SAT_MSR_IA32_IPT_CTL, ctl);
			ctl |= TRACE_CTL_TRACEEN;
			wrmsrl(SAT_MSR_IA32_IPT_CTL, ctl);
		}
	}
}

/**
 * send_hook() - send information about a hooked symbol
 * @sh: The symbol hook information to send
 */
static void send_hook(
		unsigned long orig_addr,
		unsigned long new_addr,
		unsigned long wrapper_addr,
		unsigned long size, char *name)
{
	sat_msg umsg;
	sat_msg_hook_abi3 *msg = &umsg.hook_abi3;
	msg->header.type = SAT_MSG_HOOK_ABI3;

	msg->org_addr        = orig_addr;
	msg->new_addr        = new_addr;
	msg->wrapper_addr    = wrapper_addr;
	msg->size            = size;

	msg->header.size = sizeof(*msg);
	memset(msg->name, '\0', sizeof(msg->name));

	if (name)
		strncpy(msg->name, name, sizeof(msg->name) - 1);

	msg->header.size -= sizeof(msg->name) - (strlen(msg->name) + 1);

	send_sat_msg(&umsg);
}

/**
 * send_codedump() - Send information about a specific code block
 * @addr: The address of the code block
 * @name: The name of the code block
 */
static void send_codedump(uint64_t addr, uint64_t size, const char* name)
{
	sat_msg umsg;
	sat_msg_codedump *msg = &umsg.codedump;
	msg->header.type = SAT_MSG_CODEDUMP;
	msg->addr        = addr;
	msg->size        = size;

	msg->header.size = sizeof(*msg);
	memset(msg->name, '\0', sizeof(msg->name));
	strncpy(msg->name, name, sizeof(msg->name) - 1);
	msg->header.size -= sizeof(msg->name) - (strlen(msg->name) + 1);

	printk("SAT: sending SAT_MSG_CODEDUMP");
	send_sat_msg(&umsg);
}

/**
 * send_module() - Send information about a loaded module
 * @addr: The address of the module
 * @name: The name of the module
 */
static void send_module(unsigned long addr, unsigned long size, const char *name)
{
	sat_msg umsg;
#ifdef __i386__
	sat_msg_module *msg = &umsg.module;
	msg->header.type = SAT_MSG_MODULE;
#else
	sat_msg_module_abi2 *msg = &umsg.module_abi2;
	msg->header.type = SAT_MSG_MODULE_ABI2;
#endif
	msg->addr        = addr;
	msg->size        = size;

	msg->header.size = sizeof(*msg);
	memset(msg->name, '\0', sizeof(msg->name));
	strncpy(msg->name, name, sizeof(msg->name) - 1);
	msg->header.size -= sizeof(msg->name) - (strlen(msg->name) + 1);

	send_sat_msg(&umsg);
}

/**
 * send_schdule_id() - send information about a schedule_id functions
 * @sh: The symbol hook information to send
 */
static void send_schedule_id(
		uint64_t addr,
		uint8_t  id)
{
	sat_msg umsg;
	sat_msg_schedule_id *msg = &umsg.schedule_id;
	msg->header.type = SAT_MSG_SCHEDULE_ID;

	msg->addr        	= addr;
	msg->id        		= id;

	msg->header.size = sizeof(*msg);

	send_sat_msg(&umsg);
}

/*
 * Print info from task
 */
static void print_all_task_info(struct task_struct *tsks)
{
	struct mm_struct *mm;
	struct vm_area_struct *vm_structs;
	struct file *f;
	char *name;
	const char *map_name = NULL;

	task_lock(tsks);

	name = tsks->comm;
	mm = tsks->mm;

	if (mm && tsks->pid == tsks->tgid) {
		for (vm_structs = mm->mmap; vm_structs;
			vm_structs = vm_structs->vm_next) {
			f = vm_structs->vm_file;
			if (!(!f || !f->f_path.dentry || !f->f_path.mnt) &&
				vm_structs->vm_flags & VM_EXEC) {
				send_mmap(SAT_ORIGIN_INIT,
					tsks->tgid,
					vm_structs->vm_start,
					vm_structs->vm_end -
					vm_structs->vm_start,
					vm_structs->vm_pgoff,
					&f->f_path,
					NULL);
			} else if(!f){
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
				if (vm_structs->vm_flags & VM_EXEC) {
					// Find mmap name.
					// TODO: If vm_ops->name returns null, it could still
					//       be vdso..
					if (vm_structs->vm_ops && vm_structs->vm_ops->name) {
						map_name = vm_structs->vm_ops->name(vm_structs);
						if (map_name && strcmp(map_name, "[vdso]") == 0) {
							if (vm_structs->vm_start > U32_MAX) {
								map_name = "/vdso64.so";
							} else {
								map_name = "/vdso32-sysenter.so";
							}
						}
					}
					//printk("mmap: [0x%lx .. 0x%lx]  '%s'\n", vm_structs->vm_start, vm_structs->vm_end, map_name);
					send_mmap(SAT_ORIGIN_INIT,
						tsks->tgid,
						vm_structs->vm_start,
						vm_structs->vm_end -
						vm_structs->vm_start,
						0,
						NULL,
						map_name
						);
				}
#endif
			}
		}
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		// Hardcode vdso mmap
		send_mmap(SAT_ORIGIN_INIT,
			tsks->tgid,
			0xffffe000,
			0x1000,
			0,
			NULL,
			"/vdso32-sysenter.so"
			);
#endif
		// send_mmap(SAT_ORIGIN_INIT,
		// 	tsks->tgid,
		// 	vdso,
		// 	vdso_size,
		// 	0,
		// 	&vdso_name);
	}

	task_unlock(tsks);

#if 0
	trace_printk(KERN_ALERT "SAT: task: %p, pid: %d, mm: %p, name: %s\n",
		     tsks, (int)tsks->pid, mm, name);

	trace_printk(KERN_ALERT "cpu:%d t:%p p:%d pgd:0x%llx mm:%p name: %s\n",
		     task_cpu(tsks), tsks, (int)tsks->pid,
		     (long long)__pa(mm && mm->pgd ? mm->pgd : 0),
		     mm, name);
#endif

	send_process(SAT_ORIGIN_INIT, tsks->pid, 0, tsks->tgid,
		     virt_to_phys(mm && mm->pgd ? mm->pgd : 0), name);
}

/*
 * Print out all task info
 */
static void print_all_tasks(void)
{
	struct task_struct *g, *p;

	rcu_read_lock();

	do_each_thread(g, p) {
		print_all_task_info(p);
	} while_each_thread(g, p);

	rcu_read_unlock();
}

/* ASM version is in use */
#ifdef SATT_ASM_VERSION_IS_IN_USE
#pragma GCC push_options
#pragma GCC optimize ("omit-frame-pointer")

/**
 * Hook Schedule info
 * - send new pid
 * - send new tgid
 */
 static struct task_struct *switch_to_wrapper_rtit(struct task_struct *prev,
 					     struct task_struct *next)
 {
 #ifdef __i386__
 	/* impl for __i386__ */
 	asm ("push %%eax;"
 		"push %%esi;"
 		"push %%edi;"
 		"push %%ecx;"
 		"push %%edx;"
 		:
 		:
 		);

 	send_schedule(prev, next);

 	asm (	"pop %%edx;"
 		"pop %%ecx;"
 		"pop %%edi;"
 		"pop %%esi;"
 		"mov %%cr3, %%eax;"
 		"mov %%eax, %%cr3;"
 		"pop %%eax;"
 		"pop %%ebp;"
 		"jmp  *%0;"
 		:
 		: "m" (switch_to_address.run)
 		);
 	/* We never reach this far.. */
 	return 0;
 #else
 	asm ("push %%rax;"
 		"push %%rsi;"
 		"push %%rdi;"
 		:
 		:
 		);

 	send_schedule(prev, next);

 	asm ("pop %%rdi;"
 		 "pop %%rsi;"
 		 "mov %%cr3, %%rax;"
 		 "mov %%rax, %%cr3;"
 		 "pop %%rax;"
 		 "jmp  *%0;"
 		:
 		: "m" (switch_to_address.run)
 		);

 	return 0;
#endif
}

static struct task_struct *switch_to_wrapper_ipt(struct task_struct *prev,
					     struct task_struct *next)
{
#ifdef __i386__
	/* impl for __i386__ */
	asm ("push %%eax;"
		"push %%esi;"
		"push %%edi;"
		"push %%ecx;"
		"push %%edx;"
		:
		:
		);

	send_schedule(prev, next);

	asm ("pop %%edx;"
		"pop %%ecx;"
		"pop %%edi;"
		"pop %%esi;"
		"pop %%eax;"
		"jmp  *%0;"
		:
		: "m" (switch_to_address.run)
		);
	/* We never reach this far.. */
	return 0;
#else
	asm ("push %%rax;"
		"push %%rsi;"
		"push %%rdi;"
		:
		:
		);

	send_schedule(prev, next);

	asm ("pop %%rdi;"
		 "pop %%rsi;"
		 "pop %%rax;"
		 "jmp  *%0;"
		:
		: "m" (switch_to_address.run)
		);

	return 0;
#endif

}

#pragma GCC pop_options
#endif /* SATT_ASM_VERSION_IS_IN_USE */

#ifdef CONFIG_TSC_BEFORE_SLEEP
static int intel_idle_wrapper(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)
{
	uint64_t ctl;
	int retval;

	typeof(&intel_idle_wrapper)run;

	atomic_inc(&hijack_callers);

	run = intel_idle_address.run;

	/* Generate timestamp by toggling IPT trace stop-start */
	rdmsrl(SAT_MSR_IA32_IPT_CTL, ctl);

	if (ctl & TRACE_CTL_TRACEEN) {
		ctl &= ~TRACE_CTL_TRACEEN;
		wrmsrl(SAT_MSR_IA32_IPT_CTL, ctl);
		ctl |= TRACE_CTL_TRACEEN;
		wrmsrl(SAT_MSR_IA32_IPT_CTL, ctl);
	}

	retval = run(dev, drv, index);

	atomic_dec(&hijack_callers);
	return retval;
}
#endif
/*
 * Follow task fork
 */
static void task_fork_fair_wrapper(struct task_struct *p)
{
	typeof(&task_fork_fair_wrapper)run;

	atomic_inc(&hijack_callers);

	run = task_fork_fair_address.run;

	send_process(SAT_ORIGIN_FORK, p->pid, current->tgid, p->tgid,
		     virt_to_phys(p->mm && p->mm->pgd ? p->mm->pgd : 0), 0);

	run(p);

	atomic_dec(&hijack_callers);
}

static void mmap_wrapper(struct vm_area_struct *vma)
{
	//unsigned long retval;
	struct file *file = NULL;
	const char *map_name = NULL;
	typeof(&mmap_wrapper)run;

	atomic_inc(&hijack_callers);

	run = mmap_address.run;

	run(vma);

	if (vma) {
		file = vma->vm_file;
		if (file) {
			send_mmap(SAT_ORIGIN_MMAP,
					current->tgid, vma->vm_start,
					(vma->vm_end - vma->vm_start),
					(u64)vma->vm_pgoff,
					&file->f_path, NULL);
		} else {
			if (vma->vm_ops && vma->vm_ops->name &&
				vma->vm_flags & VM_EXEC) {
				map_name = vma->vm_ops->name(vma);
				if (map_name && strcmp(map_name, "[vdso]") == 0) {
					if (vma->vm_start > U32_MAX) {
						map_name = "/vdso64.so";
					} else {
						map_name = "/vdso32-sysenter.so";
					}
				}
				send_mmap(SAT_ORIGIN_MMAP,
						current->tgid, vma->vm_start,
						(vma->vm_end - vma->vm_start),
						(u64) 0, //vma->vm_pgoff,
						NULL, map_name);
			}
		}
	}

	atomic_dec(&hijack_callers);
}

static int do_munmap_wrapper(struct mm_struct *mm, unsigned long start,
			     size_t len)
{
	int retval;
	typeof(&do_munmap_wrapper)run;

	atomic_inc(&hijack_callers);

	run = do_munmap_address.run;

	retval = run(mm, start, len);

#if 0
	trace_printk(KERN_ALERT "SAT: PID %i: munmap(%p, %x)\n",
		     current->pid, (void *)start, len);
#endif
	send_munmap(SAT_ORIGIN_MUNMAP, current->tgid, start, len);

	atomic_dec(&hijack_callers);

	return retval;
}

#ifdef CONFIG_INTEL_PTI_STM
static void lnw_gpio_set_alt_wrapper(int gpio, int alt)
{
	typeof(&lnw_gpio_set_alt_wrapper)run;

	atomic_inc(&hijack_callers);

	/* FIXME: use names instead of numerics */
	if (trace_method != LOG_TO_PTI || gpio < 164 || gpio > 175) {
		run = lnw_gpio_set_alt_address.run;
		run(gpio, alt);
	}

	atomic_dec(&hijack_callers);
}

static int gpio_request_wrapper(unsigned gpio, const char *label)
{
	int retval;
	typeof(&gpio_request_wrapper)run;

	atomic_inc(&hijack_callers);

	/* FIXME: use names instead of numerics */
	if (trace_method != LOG_TO_PTI || gpio < 164 || gpio > 175) {
		run = gpio_request_address.run;
		retval = run(gpio, label);
	} else {
		/* Pretend that everything went fine */
		retval = 0;
	}

	atomic_dec(&hijack_callers);

	return retval;
}
#endif /* CONFIG_INTEL_PTI_STM */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
static void set_task_comm_wrapper(struct task_struct *task, char *buf)
#else
static void set_task_comm_wrapper(struct task_struct *task, char *buf, bool exec)
#endif
{
	typeof(&set_task_comm_wrapper)run;

	atomic_inc(&hijack_callers);

	run = set_task_comm_address.run;

	send_process(SAT_ORIGIN_SET_TASK_COMM, task->pid, 0, task->tgid,
		     virt_to_phys(task->mm &&
				  task->mm->pgd ? task->mm->pgd : 0),
		     buf);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
	run(task, buf);
#else
	run(task, buf, exec);
#endif
	atomic_dec(&hijack_callers);
}

/*
 * Symbols hookup table
 */

static struct symbol_hook symbol_hooks[] = {
	/* !! DO NOT MOVE __switch_to !!! MUST BE FIRST ITEM IN THE LIST */
	{ "__switch_to", &switch_to_address, switch_to_wrapper_ipt, 1 },

	/* proc_fork_connector or cgroup_post_fork or perf_event_fork
	 * depending kernel configuration
	 */
	{ "perf_event_fork", &task_fork_fair_address, task_fork_fair_wrapper, 0 },
	{ "perf_event_mmap", &mmap_address, mmap_wrapper, 0 },
	{ "do_munmap", &do_munmap_address, do_munmap_wrapper, 0 },
#ifdef CONFIG_INTEL_PTI_STM
	{ "lnw_gpio_set_alt", &lnw_gpio_set_alt_address,
						lnw_gpio_set_alt_wrapper, 0 },
	{ "gpio_request", &gpio_request_address, gpio_request_wrapper, 0 },
#endif /* CONFIG_INTEL_PTI_STM */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
	{ "set_task_comm", &set_task_comm_address, set_task_comm_wrapper, 0 },
#else
	{ "__set_task_comm", &set_task_comm_address, set_task_comm_wrapper, 0 },
#endif

#ifdef CONFIG_TSC_BEFORE_SLEEP
	{ "intel_idle", &intel_idle_address, intel_idle_wrapper, 1 },
#endif
	{ 0, 0, 0, 0 }
};

/*
static int intel_idle(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)

*/

static void printfail(const char *name)
{
	trace_printk(PKPRE
		     "SAT: warning: unable to implement protections for %s\n",
		     name);
}

static int __hijack_syscalls(void *unused)
{
	int i;

	for (i = 0; symbol_hooks[i].func; i++)
		symbol_hijack(symbol_hooks[i].sym);

	return 0;
}

static void send_kernel_codedump_info(void)
{
	struct kernsym _text_sym;
	struct kernsym _end_sym;

	if (!IN_ERR(find_symbol_address(&_text_sym, "_text")) || !IN_ERR(find_symbol_address(&_text_sym, "_stext")))
		if (!IN_ERR(find_symbol_address(&_end_sym, "_etext")))
#ifdef __i386__
			send_codedump((uint64_t)(uint32_t)_text_sym.addr, (uint64_t)(uint32_t) (_end_sym.addr - _text_sym.addr), "vmlinux");
#else
            send_codedump((uint64_t) _text_sym.addr, (uint64_t) (_end_sym.addr - _text_sym.addr), "vmlinux");
#endif
}

static void send_module_list(void)
{
	const char *name = "sat";
	struct module *mod = NULL;
	struct list_head *head = NULL;
	struct list_head *pos = NULL;

	mutex_lock(&module_mutex);

	if ((mod = find_module(name)) == NULL) {
		trace_printk(KERN_ALERT "SAT: kernel module not found!\n");
		goto EXIT;
	}

	/* XXX: This is a horrible hack that relies on our module
	 *      being the first module in a circular list and thus that
	 *      the head entry is immediately before ours
	 */
	head = (struct list_head *)(((uintptr_t)mod) +
			sizeof(struct module *));
	pos = head = head->prev;

	list_for_each(pos, head) {
		mod = list_entry(pos, struct module, list);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		/* Unformed modules should be skipped if possible,
		 * but the separate state for these wasn't introduced
		 * until the 3.8-kernel, so this needs to be conditional
		 */
		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0) */


// TODO: Add module size also into module message.

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		/* Skip dummy modules */
		if (!mod->name || !mod->core_layout.base)
			continue;

		send_module((unsigned long)mod->core_layout.base,
			(unsigned long)(mod->core_layout.size + mod->init_layout.size),
			mod->name);

#else
		/* Skip dummy modules */
		if (!mod->name || !mod->module_core)
			continue;

		send_module((unsigned long)mod->module_core,
			(unsigned long)(mod->init_size + mod->core_size), mod->name);
#endif
	}

EXIT:
	mutex_unlock(&module_mutex);
}

static int hijack_syscalls(void)
{
	int ret = 0, i;

	ret = kernfunc_init();
	if (IN_ERR(ret)) {
		trace_printk(KERN_ALERT "SAT: kernel function hookin failed\n");
		return ret;
	}


	for (i = 0; symbol_hooks[i].func; i++) {
		int tmp;
		if(symbol_hooks[i].partial){

			tmp = symbol_prepare_for_hijack_partial_copy(symbol_hooks[i].sym,
						symbol_hooks[i].name,
						symbol_hooks[i].func);
		} else {
			tmp = symbol_prepare_for_hijack(symbol_hooks[i].sym,
						symbol_hooks[i].name,
						symbol_hooks[i].func);
		}
		if (IN_ERR(tmp)) {
			ret = tmp;
			printfail(symbol_hooks[i].name);
		}
	}
	/* Easier access to switch to jump address in asm */
	switch_to_ptr = switch_to_address.new_addr;

	stop_machine(__hijack_syscalls, NULL, NULL);

	return ret;
}

static int __undo_hijack_syscalls(void *unused)
{
	int i;

	for (i = 0; symbol_hooks[i].func; i++)
		symbol_restore(symbol_hooks[i].sym);

	return 0;
}

static ssize_t read_control(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	int len;

	len = snprintf(control_buf, sizeof(control_buf), "%u\n", capture_on);

	if (len > count)
		return -EFAULT;

	if (copy_to_user(buf, control_buf, len))
		return -EFAULT;

	return simple_read_from_buffer(buf, count, ppos, control_buf, len);
}

static void __send_init(void)
{
	send_init(current->pid, current->tgid);
}

/*
 * Send init from both CPU to capture initial state
 */
static void send_init_to_sideband(void)
{
	int cpu;

	get_online_cpus();

	//for_each_present_cpu(cpu) {
	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu, (smp_call_func_t)__send_init,
					 NULL, true);
	}

	put_online_cpus();
}

static ssize_t write_control(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	unsigned control;

	if (count >= sizeof(control_buf))
		return -EFAULT;

	if (copy_from_user(control_buf, buf, count))
		return -EFAULT;

	control_buf[count] = '\0';

	if (sscanf(control_buf, "%u", &control) != 1)
		return -EINVAL;

	if (capture_on != control) {
		capture_on = control;
#if 0
		trace_printk(KERN_ALERT "SAT: switch capture %s\n",
			     capture_on ? "ON" : "OFF");
#endif

		if (capture_on) {
#ifdef EXTERNAL_TRACE_START_AND_STOP
			/* stop external memory polling */
			stop_trace_startup_poll();
#endif
			sat_start_tracing();
		} else {
			sat_stop_tracing();
#ifdef EXTERNAL_TRACE_START_AND_STOP
			/* re-start external memory polling */
			start_trace_startup_poll();
#endif
		}
	}

	return count;
}

#ifdef CONFIG_INTEL_PTI_STM
static void configure_gpios(void)
{
	int x;

	/* 164-175  2 */
	for (x = 164; x <= 175; x++)
		lnw_gpio_set_alt(x, LNW_ALT_2);
}
#endif /* CONFIG_INTEL_PTI_STM */

void sat_start_tracing(void)
{
	int cpu, cpus;
	cpus = 0;

	/* Create all cpus bit mask */
	for_each_present_cpu(cpu) {
		cpus |= 1 << cpu;
	}

	if (processor_trace_version == TRACE_IPT) {
		clear_offset();
	}
	/* Clears SB tracing buffer */
	sb_reset();

	send_pti_sb_identification_msg();

	trace_active = true;

	atomic_set(&hijack_callers, 0);

#ifdef PANIC_TRACER
	if (!panic_tracer || (panic_tracer && panic_sideband)) {
#endif /* PANIC_TRACER */

		if (!hooking_done) {

#ifdef CONFIG_INTEL_PTI_STM
		/* GPIO configuration has to be done before hooking */
		if (trace_method == LOG_TO_PTI)
			configure_gpios();
#endif /* CONFIG_INTEL_PTI_STM */

				hooking_done = true;
				/* XXX: we need error handling here */
				hijack_syscalls();
		}

		sb_buffer_init(NULL);

#ifdef PANIC_TRACER
	}
	if (panic_tracer)
		enable_panic_handler();
#endif /* PANIC_TRACER */

	/*
	 * Start Power monitoring if enabled
	 */
#ifdef POWER_MONITORING_FROM_FUEL_GAUGE
	if (power_monitor > 0) {
		start_power_monitoring();
	}
#endif /* POWER_MONITORING_FROM_FUEL_GAUGE */

	/*
	* Start tracing on all CPUs
	*/
	trace_start(cpus);
    intel_pt_running = true;
}

void sat_stop_tracing(void)
{
	trace_active = false;

    intel_pt_running = false;
//	if (processor_trace_version == TRACE_RTIT) {
		/* Disable tracing */
		trace_stop();
//	}

#ifdef POWER_MONITORING_FROM_FUEL_GAUGE
	if (power_monitor > 0) {
		stop_power_monitoring();
	}
#endif /* POWER_MONITORING_FROM_FUEL_GAUGE */

#ifdef PANIC_TRACER
	if (!panic_tracer || (panic_tracer && panic_sideband))
#endif /* PANIC_TRACER */
	{
		if (hooking_done) {
			hooking_done = false;
			stop_machine(__undo_hijack_syscalls, NULL, NULL);
		}
	}

	/* Disable panic handler hook */
#ifdef PANIC_TRACER
	if (panic_tracer)
		disable_panic_handler();
#endif /* PANIC_TRACER */
}

static const struct file_operations control_fops = {
	.read = read_control,
	.write = write_control,
};

static ssize_t read_ko_fetch_addr(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	int len;

#ifdef __i386__
	len = snprintf(control_buf, sizeof(control_buf), "0x%8.8x\n",
		       (uint32_t) ko_fetch_address);
#else
	len = snprintf(control_buf, sizeof(control_buf), "0x%16.16llx\n",
		       (uint64_t) ko_fetch_address);
#endif
	return simple_read_from_buffer(buf, count, ppos, control_buf, len);
}

static ssize_t write_ko_fetch_addr(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	if (count >= sizeof(control_buf))
		return -EFAULT;

	if (copy_from_user(control_buf, buf, count))
		return -EFAULT;

	control_buf[count] = '\0';

	if (sscanf(control_buf, "0x%llx", (uint64_t *) &ko_fetch_address) != 1)
		return -EINVAL;

	return count;
}

static ssize_t read_ko_fetch_size(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	int len;

	len = snprintf(control_buf, sizeof(control_buf), "%u\n",
		       (uint32_t) ko_fetch_length);
	return simple_read_from_buffer(buf, count, ppos, control_buf, len);
}

static ssize_t write_ko_fetch_size(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	if (count >= sizeof(control_buf))
		return -EFAULT;

	if (copy_from_user(control_buf, buf, count))
		return -EFAULT;

	control_buf[count] = '\0';

	if (sscanf(control_buf, "%u", &ko_fetch_length) != 1)
		return -EINVAL;

	return count;
}

#ifdef INDIRECT_MODULE_DUMP

static ssize_t read_ko_fetch_data(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	static unsigned char* buffer = NULL;
	static uint64_t* addr = 0;
	int len = ko_fetch_length;

	if (addr != ko_fetch_address || !buffer)
	{
		addr = ko_fetch_address;
		if (buffer) {
			vfree(buffer);
		}

		buffer = (unsigned char*) vmalloc(len);
		//printk("dump: 0x%lx (%d) => %lx\n", (long unsigned int) ko_fetch_address, len, (unsigned long)buffer);
		if (buffer) {
			memcpy(buffer, ko_fetch_address, len);
		} else {
			printk("SAT: ERROR - Can't allocate memory for module dump buffer! (%d)\n", len);
		}
	}

	if (buffer) {
		return simple_read_from_buffer(buf, count, ppos, (void *) buffer, len);
	}
	return 0;
}

#else

static ssize_t read_ko_fetch_data(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	int len;

	len = ko_fetch_length;

	return simple_read_from_buffer(buf, count, ppos, (void *) ko_fetch_address, len);
}
#endif

static const struct file_operations ko_fetch_addr_fops = {
	.read = read_ko_fetch_addr,
	.write = write_ko_fetch_addr,
};

static const struct file_operations ko_fetch_size_fops = {
	.read = read_ko_fetch_size,
	.write = write_ko_fetch_size,
};

static const struct file_operations ko_fetch_data_fops = {
	.read = read_ko_fetch_data,
};

#ifdef CONFIG_INTEL_PTI_STM
static int get_stp_mc(void)
{
	if (sideband_log_method != LOG_TO_PTI)
		return 0;

	mc = pti_request_masterchannel(STP_MC, "SAT");

	if (mc == NULL) {
		printk(KERN_ALERT "STI Master Channel allocation failed\n");
		return -1;
	}

	printk(KERN_ALERT "STI Master = %d\n", mc->master);
	printk(KERN_ALERT "STI Channel = %d\n", mc->channel);

	return 0;
}

static void release_stp_mc(void)
{
	pti_release_masterchannel(mc);
	mc = NULL;
}

/*
 * Enable 200MHz PTI clock
 */
static int sat_pti_clock_control(void)
{
	int ret = 0;
	/* Config 200Mhz and 16bit width PTI*/
	uint8_t wbuf[2] = { 0x49, 0x01 };

	/* Only modify PTI clock speed if we're logging to PTI */
	if (trace_method != LOG_TO_PTI &&
		sideband_log_method != LOG_TO_PTI)
		return 0;

	/* Anniedale B0 limits PTI clock to 100MHz */
	if (cpu_type == INTEL_MID_CPU_CHIP_ANNIEDALE) {
		wbuf[1] = 0x03;
	}

	if (intel_scu_ipc_command) {
		ret = intel_scu_ipc_command(IPC_CMD_PTI_CLOCK_CTRL, 0,
			wbuf, 2, NULL, 0);
	}
	else {
		printk(KERN_ALERT
			"SAT ERROR: intel_scu_ipc_command not supported for this platform\n");
		ret = -EFAULT;
	}

	if (ret)
		printk(KERN_ALERT
		       "SAT ERROR: PTI clk 200MHz set failed reason %#x\n",
		       ret);

	return ret;
}
#endif /* CONFIG_INTEL_PTI_STM */


/*
 * Send various useful sideband information
 * that will be used in post processing
 *
 * Implementation of sideband buffer swap work
 */
void sb_buffer_init(struct work_struct *work) {
	int i;

	send_kernel_codedump_info();
	send_module_list();
	for (i = 0; symbol_hooks[i].func; i++)
		send_hook((unsigned long)symbol_hooks[i].sym->addr,
			(unsigned long)symbol_hooks[i].sym->new_addr,
			(unsigned long)symbol_hooks[i].func,
			symbol_hooks[i].sym->size,
			symbol_hooks[i].name);

	for (i = 0; i <= SCHEDULE_ID_COUNT_MAX; i++)
		send_schedule_id((uint64_t)schedule_inject[i], (uint8_t)i);

	print_all_tasks();

	send_init_to_sideband();
}

/*
 * sat_module_exit() - module exit function, called on module removal
 */
static void sat_module_exit(void)
{
	int i;
#ifdef CONFIG_INTEL_PTI_STM
	release_stp_mc();
#endif /* CONFIG_INTEL_PTI_STM */

#ifdef PANIC_TRACER
	exit_panic_tracer();
#endif /* PANIC_TRACER */

	if (hooking_done) {
		hooking_done = false;
		stop_machine(__undo_hijack_syscalls, NULL, NULL);
	}
	/* Make sure there is no cores executing hooked __switch_to()
	   function before removing function copy from memory */
	schedule();

	for (i = 0; symbol_hooks[i].func; i++)
		symbol_free_copy(symbol_hooks[i].sym);

	debugfs_remove_recursive(sat_dir_dent);

	if (processor_trace_version == TRACE_RTIT || processor_trace_version == TRACE_IPT) {
		intel_pt_exit();
	}
	sb_exit();

	trace_printk(KERN_ALERT "SAT: module stop. Out, out, brief candle!\n");
}

/*
 * Check which Intel Processor Trace version is supported
 */
static bool is_intel_pt_supported(void) {
	unsigned int eax, ebx, ecx, edx;
	__cpuid_count(0x7, 0, eax, ebx, ecx, edx);
	if (ebx & CPU_SUPPORT_INTEL_PT)
		return true;

	return false;
}


static bool get_ctc_tsc_ratio(uint32_t *tsc, uint32_t *ctc) {
    unsigned int eax, ebx, ecx, edx;
	__cpuid_count(0x15, 0, eax, ebx, ecx, edx);
    *tsc = ebx;
    *ctc = eax;
	return true;
}

/*
 * sat_module_init() - module init function; called on module insertion
 *
 * Return: 0 if successfully loaded, non-zero on failure.
 */
static int sat_module_init(void)
{
	u32 reg;
	int ret = 0;
	int cpu;
	printk(KERN_ALERT "SAT: module start, I bear a charmed life.\n");

	if (is_intel_pt_supported()) {
		/* Initial check to figure out if MSR registers for RTIT are open */
		for_each_present_cpu(cpu) {
			ret = rdmsr_safe_on_cpu(cpu, SAT_MSR_IA32_IPT_CTL, &reg, &reg);
			if (IN_ERR(ret)) {
				printk(KERN_ERR "SAT: MSR read failed! cpu=%d, reg=0x%x\n",cpu, SAT_MSR_IA32_IPT_CTL);
				return ret;
			}
		}
        get_ctc_tsc_ratio(&tma_ratio_tsc, &tma_ratio_ctc);
		processor_trace_version = TRACE_IPT;
		/* Modify symbol_hooks for schedule */
	} else {
		/* Check if RTIT is supported */
		/* Initial check to figure out if MSR registers for RTIT are open */
		for_each_present_cpu(cpu) {
			ret = rdmsr_safe_on_cpu(cpu, SAT_MSR_IA32_RTIT_CTL, &reg, &reg);
			if (IN_ERR(ret)) {
				printk(KERN_ERR "SAT: MSR read failed! cpu=%d, reg=0x%x\n",cpu, SAT_MSR_IA32_RTIT_CTL);
				return ret;
			}
		}
		processor_trace_version = TRACE_RTIT;
		/* Modify symbol_hooks for schedule */
		symbol_hooks[0].func = (void*) switch_to_wrapper_rtit;
	}

/* Panic tracer check */
#ifdef PANIC_TRACER
	if ( panic_tracer ) {
		if ( trace_method == 0 ) {
			printk(KERN_ERR "SAT: panic tracer does not work with PTI-tracing.\n");
			return -EFAULT;
		}
		/* Force trace only kernel */
		exclude_kernel = false;
		exclude_userspace = true;
		ret = init_panic_tracer();
		if (ret)
			return ret;
	}
#endif /* PANIC_TRACER */

#ifdef CONFIG_INTEL_PTI_STM
        cpu_type = intel_mid_identify_cpu();
        printk(KERN_ALERT "SAT: cpu = %d\n", cpu_type);

	if (get_stp_mc() == -1) {
		printk(KERN_INFO "SAT: STP channel allocation out of memory\n");
		return -ENOMEM;
	}
#endif /* CONFIG_INTEL_PTI_STM */

	atomic_set(&hijack_callers, 0);

	sat_dir_dent = debugfs_create_dir("sat", NULL);

	if (!sat_dir_dent) {
		printk(KERN_ERR "SAT: could not create SAT folder\n");
		return -1;
	}

	capture_on = 0;
	control_file = debugfs_create_file("trace_enable", 0644,
					   sat_dir_dent, NULL, &control_fops);

	if (!control_file) {
		printk(KERN_ERR "SAT: could not create control file\n");
		return -1;
	}

	ko_fetch_addr = debugfs_create_file("ko_fetch_addr", 0644,
			sat_dir_dent, NULL, &ko_fetch_addr_fops);

	if (!ko_fetch_addr) {
		printk(KERN_ERR "SAT: could not create ko_fetch_addr file\n");
		return -1;
	}

	ko_fetch_size = debugfs_create_file("ko_fetch_size", 0644,
			sat_dir_dent, NULL, &ko_fetch_size_fops);

	if (!ko_fetch_size) {
		printk(KERN_ERR "SAT: could not create ko_fetch_size file\n");
		return -1;
	}

	ko_fetch_data = debugfs_create_file("ko_fetch_data", 0444,
			sat_dir_dent, NULL, &ko_fetch_data_fops);

	if (!ko_fetch_data) {
		printk(KERN_ERR "SAT: could not create ko_fetch_data file\n");
		return -1;
	}

	/* Init SB memory tracer */
	INIT_WORK(&sb_init_work, sb_buffer_init);
	ret = sb_init(sat_dir_dent);

	if (ret != 0) {
		printk(KERN_ERR "SAT: sb_init failed\n");
		sat_module_exit();
		return ret;
	}

	if (processor_trace_version == TRACE_RTIT || processor_trace_version == TRACE_IPT) {
		/* Init memory tracer */
		ret = intel_pt_init(sat_dir_dent, exclude_kernel, exclude_userspace, processor_trace_version);

		if (ret != 0) {
			printk(KERN_ERR "SAT: intel_pt_init failed\n");
			sat_module_exit();
			return ret;
		}
	}

	/* Init Power Monitor */
#ifdef POWER_MONITORING_FROM_FUEL_GAUGE
	if (power_monitor > 0) {
		ret = init_power_monitor();
		if (ret != 0) {
			printk(KERN_ERR "SAT: power monitor init failed\n");
			sat_module_exit();
			return ret;
		}
	}
#endif /* POWER_MONITORING_FROM_FUEL_GAUGE */

#ifdef CONFIG_INTEL_PTI_STM
	ret = sat_pti_clock_control();

	if (ret != 0) {
		printk(KERN_ERR "SAT: pti clock control failed\n");
		sat_module_exit();
		return ret;
	}
#endif /* CONFIG_INTEL_PTI_STM */

	return 0;
}

module_init(sat_module_init);
module_exit(sat_module_exit);
MODULE_LICENSE("GPL");
MODULE_VERSION("1.4.0");
MODULE_AUTHOR("S&S");
MODULE_DESCRIPTION("SAT - kernel module helper");
