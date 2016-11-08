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
/*
* Forked from code originally written by alexander.shishkin@intel.com
*/
#include <linux/bitops.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <linux/mman.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <asm/perf_event.h>
#include <asm/insn.h>
#include <cpuid.h>

#include "config.h"
#include "sat-tracer.h"
#include "sat-buffer.h"

#include "multi-buffer.h"

#ifdef EXTERNAL_TRACE_START_AND_STOP
#include "ext-start-stop.h"
#endif /* EXTERNAL_TRACE_START_AND_STOP */

static struct dentry *trace_bufsize_dent;
size_t trace_bufsize = TRACE_BUFFER_SIZE;

trace_buffer_pool_type buffer_pool;
trace_topa_pool_type topa_pool;

#define PT_BASE_ADDR	0xfdc00000ULL
#define PT_CPU_OFFSET	0x100ULL
#define PT_PKT_MASK	    0x20000

#define stream_filename		"cpu_stream"
#define stream_formatstring	"cpu%u_stream"

#define offset_filename		"cpu_offset"
#define offset_formatstring	"cpu%u_offset"

#ifdef EXTERNAL_TRACE_START_AND_STOP
static struct dentry *sat_start_top_mem_addr;
#endif /* EXTERNAL_TRACE_START_AND_STOP */

static void trace_clear_config(void);

static void trace_debug_ipt(void)
{
	/* Print IPT MSR registers here */
	u64 status, ctl, cr3m, base, maskptrs;
	u64 addr0a, addr0b, addr1a, addr1b;

	rdmsrl(SAT_MSR_IA32_IPT_CTL, ctl);
	rdmsrl(SAT_MSR_IA32_IPT_STATUS, status);
	rdmsrl(SAT_MSR_IA32_IPT_CR3_MATCH, cr3m);
	rdmsrl(SAT_MSR_IA32_IPT_OUTPUT_BASE, base);
	rdmsrl(SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS, maskptrs);
	rdmsrl(SAT_MSR_IA32_IPT_ADDR0_A, addr0a);
	rdmsrl(SAT_MSR_IA32_IPT_ADDR0_B, addr0b);
	rdmsrl(SAT_MSR_IA32_IPT_ADDR1_A, addr1a);
	rdmsrl(SAT_MSR_IA32_IPT_ADDR1_B, addr1b);

	printk("MSR DEBUG:\n");
	printk("SAT_MSR_IA32_IPT_CTL=0x%llx\n", ctl);
	printk("SAT_MSR_IA32_IPT_STATUS=0x%llx\n", status);
	printk("SAT_MSR_IA32_IPT_CR3_MATCH=0x%llx\n", cr3m);
	printk("SAT_MSR_IA32_IPT_OUTPUT_BASE=0x%llx\n", base);
	printk("SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS=0x%llx\n", maskptrs);
	printk("SAT_MSR_IA32_IPT_ADDR0_A=0x%llx\n", addr0a);
	printk("SAT_MSR_IA32_IPT_ADDR0_B=0x%llx\n", addr0b);
	printk("SAT_MSR_IA32_IPT_ADDR1_A=0x%llx\n", addr1a);
	printk("SAT_MSR_IA32_IPT_ADDR1_B=0x%llx\n", addr1b);
}

static void trace_debug_rtit(void)
{
	u64 status, ctl, lip0, lip1, lip2, lip3, cr3m, tntb, pktc, offset, base;
	u32 events, nlip, limit;
	rdmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);
	rdmsrl(SAT_MSR_IA32_RTIT_STATUS, status);
	rdmsrl(SAT_MSR_IA32_RTIT_EVENTS, events);
	rdmsrl(SAT_MSR_IA32_RTIT_OFFSET, offset);
	rdmsrl(SAT_MSR_IA32_RTIT_LIP0, lip0);
	rdmsrl(SAT_MSR_IA32_RTIT_LIP1, lip1);
	rdmsrl(SAT_MSR_IA32_RTIT_LIP2, lip2);
	rdmsrl(SAT_MSR_IA32_RTIT_LIP3, lip3);
	rdmsrl(SAT_MSR_IA32_RTIT_CR3_MATCH, cr3m);
	rdmsrl(SAT_MSR_IA32_RTIT_TNT_BUFF, tntb);
	rdmsrl(SAT_MSR_IA32_RTIT_LAST_CALL_NLIP, nlip);
	rdmsrl(SAT_MSR_IA32_RTIT_PKT_CNT, pktc);
	rdmsrl(SAT_MSR_IA32_RTIT_BASE_ADDR, base);
	rdmsrl(SAT_MSR_IA32_RTIT_LIMIT_MASK, limit);

	pr_debug("SAT_MSR_IA32_RTIT_CTL=0x%llx\n", ctl);
	pr_debug("SAT_MSR_IA32_RTIT_STATUS=0x%llx\n", status);
	pr_debug("SAT_MSR_IA32_RTIT_EVENTS=0x%x\n", events);
	pr_debug("SAT_MSR_IA32_RTIT_OFFSET=0x%llx\n", offset);
	pr_debug("SAT_MSR_IA32_RTIT_LIP0=0x%llx\n", lip0);
	pr_debug("SAT_MSR_IA32_RTIT_LIP1=0x%llx\n", lip1);
	pr_debug("SAT_MSR_IA32_RTIT_LIP2=0x%llx\n", lip2);
	pr_debug("SAT_MSR_IA32_RTIT_LIP3=0x%llx\n", lip3);
	pr_debug("SAT_MSR_IA32_RTIT_CR3M=0x%llx\n", cr3m);
	pr_debug("SAT_MSR_IA32_RTIT_TNT_BUFF=0x%llx\n", tntb);
	pr_debug("SAT_MSR_IA32_RTIT_LAST_CALL_NLIP=0x%u\n", nlip);
	pr_debug("SAT_MSR_IA32_RTIT_PKT_CNT=0x%llx\n", pktc);

	pr_debug("SAT_MSR_IA32_RTIT_BASE_ADDR=0x%llx\n", base);
	pr_debug("SAT_MSR_IA32_RTIT_LIMIT_MASK=0x%x\n", limit);
}

static void __trace_run_config_ipt(bool start)
{
	u64 ctl;
	u64 status;
	u64 offset;

	rdmsrl(SAT_MSR_IA32_IPT_CTL, ctl);

	if (start) {
		wrmsrl(SAT_MSR_IA32_IPT_STATUS, 0);
		if (trace_method == LOG_TO_RAM) {
			trace_type *trace = this_cpu_ptr(&processor_tracers)->trace;
			wrmsrl(SAT_MSR_IA32_IPT_OUTPUT_BASE,
				topa_pool.pool[trace->buffer_id[trace->active_idx]].phys);
		}
		// Reset IPT offset
		rdmsrl(SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS, offset);
		wrmsrl(SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS, IPT_OUTPUT_OFFSET_RESET(offset));
		// Trace Enable
		ctl |= TRACE_CTL_TRACEEN;
	} else {
		// Trace Disable
		ctl &= ~TRACE_CTL_TRACEEN;
	}

	rdmsrl(SAT_MSR_IA32_IPT_STATUS, status);
	printk("SAT: status-pre : %llx\n", status);

	wrmsrl(SAT_MSR_IA32_IPT_CTL, ctl);

	rdmsrl(SAT_MSR_IA32_IPT_STATUS, status);
	printk("SAT: status-post : %llx\n", status);

	if (!start) {
		printk("**** SATT TRACE STOPPED *****\n");
#if 0
		pr_debug("%s\n", start ? "START" : "STOP");
#endif

	} else
		printk("**** SATT TRACE STARTED *****\n");
	trace_debug_ipt();
}

static void __trace_run_config_rtit(bool start)
{
	u64 ctl;

	/* flip FilterEn and trace_active */
	rdmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);

	if (start) {
		/* Extra config to get around Lauterbach functionality */
		ctl |= TRACE_CTL_MTC_EN;
		ctl |= TRACE_CTL_STS_EN;
		ctl |= TRACE_CTL_STS_ON_CR3;
		ctl |= TRACE_CTL_CMPRS_RET;
		ctl |= TRACE_CTL_MTC_RANGE_MASK; // 11b for MTC in every 8192 ticks
		wrmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);

		if (trace_method == LOG_TO_RAM) {
			trace_type *trace = this_cpu_ptr(&processor_tracers)->trace;
			wrmsrl(SAT_MSR_IA32_RTIT_BASE_ADDR,
				buffer_pool.address[trace->buffer_id[trace->active_idx]].phys);
		}
		wrmsrl(SAT_MSR_IA32_RTIT_OFFSET, 0);
		wrmsrl(SAT_MSR_IA32_RTIT_PKT_CNT, PT_PKT_MASK);
		wrmsrl(SAT_MSR_IA32_RTIT_TNT_BUFF, 1);
		wrmsrl(SAT_MSR_IA32_RTIT_LAST_CALL_NLIP, 0);

		ctl |= TRACE_CTL_TRACE_ACTIVE;
		wrmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);
		ctl |= TRACE_CTL_TRACEEN;
		wrmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);
	} else {
		/* Generate STS timestamp into processor trace stream */
		generate_STS();
		/* When disabling trace, disable TraceActive first,
		 * then TraceEn */
		ctl &= ~TRACE_CTL_TRACE_ACTIVE;
		wrmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);
		ctl &= ~TRACE_CTL_TRACEEN;
		wrmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);
	}

	if (!start) {
#if 0
		pr_debug("%s\n", start ? "START" : "STOP");
#endif
		trace_debug_rtit();
	}
}

static void trace_config_start(void)
{
	if (trace_method == LOG_TO_RAM
#ifdef PANIC_TRACER
		&& !panic_tracer
#endif /* PANIC_TRACER */
		)
	{
		start_buffer_hack_timer();
	}

	if (processor_trace_version == TRACE_RTIT) {
		__trace_run_config_rtit(true);
	} else if (processor_trace_version == TRACE_IPT) {
		__trace_run_config_ipt(true);
	}
}

static void trace_config_stop(void)
{
	if (processor_trace_version == TRACE_RTIT) {
		__trace_run_config_rtit(false);
	} else if (processor_trace_version == TRACE_IPT) {
		__trace_run_config_ipt(false);
	}
	if (trace_method == LOG_TO_RAM
#ifdef PANIC_TRACER
		&& !panic_tracer
#endif /* PANIC_TRACER */
		)
	{
		stop_buffer_hack_timer();
	}
}

/*
 * Get the biggest support MTC timer value
 */
static int get_max_mtc_support(void) {
	unsigned int eax, ebx, ecx, edx;
	unsigned int id = 0x14 & 0x80000000;
	if (__get_cpuid_max (id, 0) < 0x14)
		return -1;
	__cpuid_count(0x14, 1, eax, ebx, ecx, edx);
	id = fls(((eax >> 16) & 0xffff));
	if (id == 0)
		return -1;
	id = id -1;

	return (int) id;
}

static void __trace_config_ipt(struct trace_conf *config)
{
	u64 reg;
	int max_mtc=0;

	/* Make Intel Processor Trace initial configuration */
	wrmsrl(SAT_MSR_IA32_IPT_STATUS, 0);
	if (trace_method == LOG_TO_RAM) {
		trace_type *trace = this_cpu_ptr(&processor_tracers)->trace;
		wrmsrl(SAT_MSR_IA32_IPT_OUTPUT_BASE,
			topa_pool.pool[trace->buffer_id[trace->active_idx]].phys);
		wrmsrl(SAT_MSR_IA32_IPT_STATUS, 0);
		wrmsrl(SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS, 0);
	}

	reg = 0;
	if (!config->exclude_kernel) {
		reg |= TRACE_CTL_OS;
	}
	if (!config->exclude_user) {
		reg |= TRACE_CTL_USR;
	}
	if (trace_method != LOG_TO_RAM) {
		reg |= TRACE_CTL_FABRIC_EN;
	}
	reg |= TRACE_CTL_TOPA;
	reg |= TRACE_CTL_TSC_EN;
	reg |= TRACE_CTL_BRANCH_EN;
	max_mtc = get_max_mtc_support();
	if (max_mtc < 0) {
		printk(KERN_ERR "SAT: ERROR MTC is not supported!\n");
		max_mtc = 0;
	} else {
		reg |= TRACE_CTL_MTC_EN;
		reg |= TRACE_CTL_MTC_FREQ(max_mtc);
		reg |= TRACE_CTL_PSB_FREQ(PSB_FREQ_VALUE);
	}
	printk("SAT: value to IPT_CTL: 0x%llx\n", reg);
	wrmsrl(SAT_MSR_IA32_IPT_CTL, reg);

}

static void __trace_config_rtit(struct trace_conf *config)
{
	u64 reg, base;
	u32 cpu;

	if (rdmsr_safe(SAT_MSR_IA32_RTIT_CTL, &reg, &reg) < 0) {
		pr_debug("SAT: SAT_MSR_IA32_RTIT_CTL register read failed!");
		return;
	}

	cpu = raw_smp_processor_id();

	if (trace_method == LOG_TO_RAM) {
		trace_type *trace = this_cpu_ptr(&processor_tracers)->trace;
		wrmsrl(SAT_MSR_IA32_RTIT_BASE_ADDR,
			buffer_pool.address[trace->buffer_id[trace->active_idx]].phys);
		/* Max trace size */
		wrmsrl(SAT_MSR_IA32_RTIT_LIMIT_MASK, TRACE_BUFFER_SIZE - 1);
	}
	else if (trace_method == LOG_TO_PTI) {
		rdmsrl(SAT_MSR_IA32_RTIT_BASE_ADDR, base);
		base &= ~(0xF << 8);
		base |= cpu << 8;
		wrmsrl(SAT_MSR_IA32_RTIT_BASE_ADDR, base);
	}
	wrmsrl(SAT_MSR_IA32_RTIT_EVENTS, 0x37);
	wrmsrl(SAT_MSR_IA32_RTIT_LIP0, 0);
	wrmsrl(SAT_MSR_IA32_RTIT_LIP1, 0);
	wrmsrl(SAT_MSR_IA32_RTIT_LIP2, 0);
	wrmsrl(SAT_MSR_IA32_RTIT_LIP3, 0);
	wrmsrl(SAT_MSR_IA32_RTIT_OFFSET, 0);
	wrmsrl(SAT_MSR_IA32_RTIT_CR3_MATCH, 0);
	wrmsrl(SAT_MSR_IA32_RTIT_PKT_CNT, PT_PKT_MASK);

	reg = 0;
	reg |= TRACE_CTL_STS_ON_CR3;
	reg |= TRACE_CTL_CMPRS_RET;

	if (!config->exclude_kernel)
		reg |= TRACE_CTL_OS;

	if (!config->exclude_user)
		reg |= TRACE_CTL_USR;

	reg |= TRACE_CTL_MTC_EN;
	reg |= TRACE_CTL_MTC_RANGE_MASK;
	reg |= TRACE_CTL_STS_EN;

	pr_debug("Processor ID=%d\n", cpu);
	pr_debug("MSR_IPT_CTL=0x%llx\n", reg);
	if (trace_method == LOG_TO_RAM)
	{
		trace_type *trace = this_cpu_ptr(&processor_tracers)->trace;
		pr_debug("IPT_BASE_ADDR=0x%16llx\n",
			(u64)buffer_pool.address[trace->buffer_id[trace->active_idx]].phys);
	}
	else if (trace_method == LOG_TO_PTI)
	{
		rdmsrl(SAT_MSR_IA32_RTIT_BASE_ADDR, base);
		pr_debug("IPT_BASE_ADDR=0x%16llx\n", base);
	}

	pr_debug("IPT_LIMIT_MASK=0x%lx\n", TRACE_BUFFER_SIZE - 1);
	pr_debug("IPT BUFFER SIZE=0x%lx\n", (unsigned long)TRACE_BUFFER_SIZE);

	pr_debug("Config CPU %d\n", config->cpu);
	if (wrmsr_safe(SAT_MSR_IA32_RTIT_CTL, reg, 0) < 0)
		pr_warn("Failed to enable processor trace on cpu %d\n", config->cpu);
}

static void trace_config_on_cpu(int cpu, struct trace_conf *config)
{
	if (processor_trace_version == TRACE_RTIT) {
		smp_call_function_single(cpu, (smp_call_func_t)__trace_config_rtit,
			config, true);
	} else if (processor_trace_version == TRACE_IPT) {
		smp_call_function_single(cpu, (smp_call_func_t)__trace_config_ipt,
			config, true);
	}
}

int reserve_trace_buffers(struct trace_conf *config)
{
	int cpu, err=0;

	/* Create trace buffer pool first before per_cpu reservation */
	if (trace_method == LOG_TO_RAM) {
		err = create_trace_buffer_pool();
		if (err)
			return err;
	}

	get_online_cpus();

	for_each_present_cpu(cpu) {
		err = init_trace_ram_alloc(cpu, GFP_ATOMIC);
		if (err)
			break;

		/* Configure processor trace registers */
		trace_config_on_cpu(cpu, config);
	}

	put_online_cpus();

	if (err) {
		for_each_present_cpu(cpu) {
			exit_trace_ram_alloc(cpu);
		}

		return -ENOMEM;
	}

	return 0;
}

void release_trace_buffers(void)
{
	int cpu;

	pr_debug("%s\n", __func__);
	get_online_cpus();

	for_each_present_cpu(cpu) {
		exit_trace_ram_alloc(cpu);
	}

	put_online_cpus();

	/* Free trace buffer pool allocation */
	release_trace_buffer_pool();
}

/*
* cpus as bitmask
*/
int trace_start(int cpus)
{
	int cpu;

	if (processor_trace_version != NO_TRACE)
	{
		get_online_cpus();

		/* Reset buffer pool reservations */
		if (trace_method == LOG_TO_RAM)
		{
			reinit_trace_buffer_pool();
		}

		for_each_present_cpu(cpu) {
			if (!((1 << cpu) & cpus))
				continue;

			smp_call_function_single(cpu,
				(smp_call_func_t)trace_config_start,
				NULL, true);
		}

		put_online_cpus();
	}
	return 0;
}

void trace_stop(void)
{
	int cpu;

	if (processor_trace_version != NO_TRACE)
	{
		get_online_cpus();

		for_each_present_cpu(cpu) {
			smp_call_function_single(cpu,
				(smp_call_func_t)trace_config_stop,
				NULL, true);
			smp_mb();
		}

		put_online_cpus();
	}
}


static void __clear_offset_ipt(void)
{
	u64 offset;
	trace_type *trace = this_cpu_ptr(&processor_tracers)->trace;

	// Reset IPT offset
	rdmsrl(SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS, offset);
	wrmsrl(SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS, IPT_OUTPUT_OFFSET_RESET(offset));
	reset_trace_buffers(trace);
}

void clear_offset(void)
{
	int cpu;
	get_online_cpus();

	for_each_present_cpu(cpu) {
		smp_call_function_single(cpu,
			(smp_call_func_t)__clear_offset_ipt,
			NULL, true);
		smp_mb();
	}

	put_online_cpus();
}


static void read_offset(int *offset)
{
	long long _offset = 0;

	if (processor_trace_version == TRACE_RTIT) {
		rdmsrl(SAT_MSR_IA32_RTIT_OFFSET, _offset);
	}
	else if (processor_trace_version == TRACE_IPT) {
		rdmsrl(SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS, _offset);
		/* [6:0]   LowerMask
		 * [31:7]  MaskOrTableOffset
		 * [63:32] OutputOffset
		 */
		_offset = (_offset >> 32) & 0xffffffff;
	}

	*offset = _offset;
}

static int open_fop(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t read_offset_fop(struct file *file, char __user *user_buf,
		size_t count, loff_t *ppos)
{
	unsigned int len;

	int offset;
	int failsafe;
	unsigned int *ptr;
	static char buf[2048];
	char tmpbuf[64];

	int cpu;

	cpu = (int)(long)file->private_data;

	if (cpu < 0)
		return -EBADF;

	if (trace_method == LOG_TO_PTI)
	{
		smp_call_function_single(cpu, (smp_call_func_t)read_offset,
				 &offset, true);
		len = sprintf(buf, "0x%08x\n", offset);
	}
	else
	{
		trace_type *trace = per_cpu(processor_tracers, cpu).trace;
		if(!*ppos) {
			int i;
			sprintf(buf, "Buffers cpu%d:\n", cpu);
			for(i=0; i<=trace->active_idx; i++) {
				ptr = (unsigned int*) ((unsigned char*)buffer_pool.address[trace->buffer_id[i]].virt +
					TRACE_BUFFER_SIZE - 4);
				failsafe = *ptr;
				sprintf(tmpbuf, " %d: add:%p off:0x%08lx fsafe:0x%08x\n",
					i, buffer_pool.address[trace->buffer_id[i]].virt, trace->offsets[i], failsafe);
				if(sizeof(buf) >= strlen(buf) + strlen(tmpbuf) + 1)
					strcat(buf, tmpbuf);
			}
			sprintf(tmpbuf, " size: 0x%lx\n\n", trace->size);
			if(sizeof(buf) >= strlen(buf) + strlen(tmpbuf) + 1)
				strcat(buf, tmpbuf);
		}
		len=strlen(buf);
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t read_stream_fop(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	static char *tmp_buf = 0;
	static int tmp_buf_size = 0;
	static int buf_start_idx = 0;
	static int buf_start_offset = 0;
	int counter;
	int i;

	trace_type *trace;
	ssize_t size = 0;
	int offset;
	int cpu;
	unsigned long base;

	cpu = (int)(long)file->private_data;

	if (cpu < 0)
		return -EBADF;

	trace = per_cpu(processor_tracers, cpu).trace;


	if (trace_method == LOG_TO_PTI)
	{
		smp_call_function_single(cpu, (smp_call_func_t)read_offset,
			&offset, true);

		if (!trace->virt) {
			if (processor_trace_version == TRACE_RTIT)
				rdmsrl(SAT_MSR_IA32_RTIT_BASE_ADDR, base);
			else // (processor_trace_version == TRACE_IPT)
				rdmsrl(SAT_MSR_IA32_IPT_OUTPUT_BASE, base);
			trace->virt = ioremap_cache(base, TRACE_BUFFER_SIZE);
			if (!trace->virt)
				return -ENOMEM;
		}

		tmp_buf = vmalloc(TRACE_BUFFER_SIZE);

		if (!tmp_buf)
			return -ENOMEM;

		/* Wrap buffer around */
		memcpy(tmp_buf, trace->virt + offset, TRACE_BUFFER_SIZE - offset);
		memcpy(tmp_buf + TRACE_BUFFER_SIZE - offset, trace->virt, offset);
		size = simple_read_from_buffer(user_buf, count, ppos,
			tmp_buf, TRACE_BUFFER_SIZE);
		vfree(tmp_buf);
	}
	else
	{
		/* Allocate temporary consecutive buffer */
		if(!*ppos || count > tmp_buf_size)
		{

			if(tmp_buf) {
				vfree(tmp_buf);
				tmp_buf_size = 0;
			}

			tmp_buf = vmalloc(count);
			if (!tmp_buf)
				return -ENOMEM;

			tmp_buf_size = count;
		}

		/* move on the "*ppos" location first */
		buf_start_offset = 0;
		buf_start_idx = 0;
		for(i=0, counter=0; i <= trace->active_idx; i++)
		{
			if((counter + trace->offsets[i]) > *ppos)
			{
				buf_start_offset = *ppos - counter;
				buf_start_idx = i;
				break;
			}
			counter += trace->offsets[i];
		}

		/* copy from buffer to tmp_buf until "count" amount of data reached */
		for(i=buf_start_idx, counter=0; i<=trace->active_idx && counter < count; i++)
		{
			if(counter + (trace->offsets[i] - buf_start_offset) >= count)
				/* There is enough data in current buffer to copy */
				size = count - counter;
			else
				/* Current buffer does not have enough data,  */
				size = trace->offsets[i] - buf_start_offset;

			memcpy(tmp_buf+counter,
				buffer_pool.address[trace->buffer_id[i]].virt +
				buf_start_offset, size);

			counter += size;
			if(buf_start_offset) buf_start_offset = 0;
		}

		size = simple_read_from_buffer(user_buf, count, ppos,
			(tmp_buf-*ppos), trace->size);
	}
	return size;
}

#ifdef EXTERNAL_TRACE_START_AND_STOP
static ssize_t read_start_stop_mem_addr(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	char buf[32];
	unsigned int len;

	/* XXX: Should use %pa once we switch to 3.10 */
	len = sprintf(buf, "0x%16llx\n",
		(u64)virt_to_phys(trace_mem_run_control_flag));

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}
#endif /* EXTERNAL_TRACE_START_AND_STOP */

static const struct file_operations trace_cpu_stream_fops = {
	.open = open_fop,
	.read = read_stream_fop,
};

static const struct file_operations trace_cpu_offset_fops = {
	.open = open_fop,
	.read = read_offset_fop,
};

#ifdef EXTERNAL_TRACE_START_AND_STOP
static const struct file_operations sat_start_top_mem_addr_fops = {
	.read = read_start_stop_mem_addr,
};
#endif /* EXTERNAL_TRACE_START_AND_STOP */

static int create_log_to_ram_debugfs_entries(struct dentry *trace_dir_dent)
{
	char *buf;
	int len;
	int cpu;

	/* Figure out the maximum length of int in characters and allocate
	 * a buffer of necessary size; we could hardcode this,
	 * but that'd be non-portable between 32/64-bit platforms.
	 * This bit is only used when we log to RAM.
	 */
	len = snprintf(NULL, 0, "%u", (unsigned)-1);
	len += max(strlen(stream_filename), strlen(offset_filename));
	buf = kmalloc(len + 1, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	get_online_cpus();

	for_each_present_cpu(cpu) {
		trace_type *trace = per_cpu(processor_tracers, cpu).trace;

		scnprintf(buf, len, stream_formatstring, cpu);

		trace->cpu_stream =
			debugfs_create_file(buf, S_IRUSR | S_IRGRP,
				trace_dir_dent,
				(void *)(long)cpu,
				&trace_cpu_stream_fops);

		if (IS_ERR(trace->cpu_stream))
			pr_err("Can't create %s entry.\n", buf);

		scnprintf(buf, len, offset_formatstring, cpu);

		trace->cpu_offset =
			debugfs_create_file(buf, S_IWUSR | S_IRUGO,
				trace_dir_dent,
				(void *)(long)cpu,
				&trace_cpu_offset_fops);

		if (IS_ERR(trace->cpu_offset))
			pr_err("Can't create %s entry.\n", buf);

	}

	put_online_cpus();
	kfree(buf);

	return 0;
}

int intel_pt_init(struct dentry *trace_dir_dent,
	bool exclude_kernel, bool exclude_userspace, int format)
{
	struct trace_conf conf;
	processor_trace_version = format;

	conf.exclude_kernel = exclude_kernel;
	conf.exclude_user = exclude_userspace;
	conf.trace_format = processor_trace_version;

	trace_bufsize_dent =
		debugfs_create_size_t("bufsize", S_IWUSR | S_IRUGO,
			trace_dir_dent, &trace_bufsize);

	if (IS_ERR(trace_bufsize_dent)) {
		pr_warn("Can't create debugfs entries.\n");
		return -ENOENT;
	}

	/* Reserve memory and Configure Intel PT */
	if (reserve_trace_buffers(&conf))
		return -ENOMEM;

	if (trace_method == LOG_TO_RAM)
	{
		/* Only create file handles for stream/offset if we log to RAM */
		create_log_to_ram_debugfs_entries(trace_dir_dent);
	}


#ifdef EXTERNAL_TRACE_START_AND_STOP
	trace_mem_run_control_flag = kmalloc(sizeof(int), GFP_KERNEL);

	if (!trace_mem_run_control_flag)
		return -ENOMEM;

	*trace_mem_run_control_flag = 0;

	sat_start_top_mem_addr =
		debugfs_create_file("run_control_addr", S_IRUSR | S_IRGRP,
			trace_dir_dent, NULL,
			&sat_start_top_mem_addr_fops);

	if (IS_ERR(sat_start_top_mem_addr))
		pr_warn("Can't create run_control_addr entry.\n");

	setup_trace_startup_poll();
	start_trace_startup_poll();

#endif /* EXTERNAL_TRACE_START_AND_STOP */

	if (trace_method == LOG_TO_RAM)
		init_buffer_hack_timers();

	return 0;
}

inline void generate_STS(void)
{
	if (processor_trace_version == TRACE_RTIT) {
#ifdef __i386__
		/* x86 impl */
		asm ("push %%eax;"
		"mov  %%cr3, %%eax;"
		"mov  %%eax, %%cr3;"
		"pop  %%eax;"
		:
		:
		);
#else /* __i386__ */
		/* x86_64 impl */
		asm ("push %%rax;"
		"mov  %%cr3, %%rax;"
		"mov  %%rax, %%cr3;"
		"pop  %%rax;"
		:
		:
		);
#endif /* __i386__ */
	}
}

static void trace_clear_config(void)
{
	u64 reg;

	if (processor_trace_version == TRACE_RTIT) {
		/* MSR access has cheched at the beginning */
		rdmsr_safe(SAT_MSR_IA32_RTIT_CTL, &reg, &reg);
		wrmsrl(SAT_MSR_IA32_RTIT_CTL, 0);
		wrmsrl(SAT_MSR_IA32_RTIT_EVENTS, 0);
		wrmsrl(SAT_MSR_IA32_RTIT_LIP0, 0);
		wrmsrl(SAT_MSR_IA32_RTIT_LIP1, 0);
		wrmsrl(SAT_MSR_IA32_RTIT_LIP2, 0);
		wrmsrl(SAT_MSR_IA32_RTIT_LIP3, 0);
		wrmsrl(SAT_MSR_IA32_RTIT_OFFSET, 0);
		wrmsrl(SAT_MSR_IA32_RTIT_CR3_MATCH, 0);
		wrmsrl(SAT_MSR_IA32_RTIT_TNT_BUFF, 1);
		wrmsrl(SAT_MSR_IA32_RTIT_LAST_CALL_NLIP, 0);
		wrmsrl(SAT_MSR_IA32_RTIT_PKT_CNT, 0);
	} else if (processor_trace_version == TRACE_IPT) {
		wrmsrl(SAT_MSR_IA32_IPT_CTL, 0);
		wrmsrl(SAT_MSR_IA32_IPT_STATUS, 0);
		wrmsrl(SAT_MSR_IA32_IPT_CR3_MATCH, 0);
		wrmsrl(SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS, 0);
		wrmsrl(SAT_MSR_IA32_IPT_ADDR0_A, 0);
		wrmsrl(SAT_MSR_IA32_IPT_ADDR0_B, 0);
		wrmsrl(SAT_MSR_IA32_IPT_ADDR1_A, 0);
		wrmsrl(SAT_MSR_IA32_IPT_ADDR1_B, 0);
	}
}

void intel_pt_exit(void)
{
	int cpu;
	get_online_cpus();

	for_each_present_cpu(cpu) {
		smp_call_function_single(cpu,
			(smp_call_func_t)trace_clear_config,
			NULL, true);
	}
	put_online_cpus();

	release_trace_buffers();

#ifdef EXTERNAL_TRACE_START_AND_STOP
	exit_trace_startup_poll();
	kfree(trace_mem_run_control_flag);
	trace_mem_run_control_flag = NULL;
#endif /* EXTERNAL_TRACE_START_AND_STOP */
}
