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
#ifndef SAT_TRACER_H
#define SAT_TRACER_H

#include <linux/perf_event.h>
#include <linux/mutex.h>
#include "config.h"

#define SAT_MSR_IA32_PEBS_ENABLE		0x000003f1
#define SAT_MSR_IA32_DS_AREA		0x00000600
#define SAT_MSR_IA32_PERF_CAPABILITIES	0x00000345
#define SAT_MSR_IA32_RTIT_CTL		0x00000768
#define SAT_MSR_IA32_RTIT_STATUS		0x00000769
#define SAT_MSR_IA32_RTIT_CNTP		0x0000076b
#define SAT_MSR_IA32_RTIT_EVENTS		0x0000076c
#define SAT_MSR_IA32_RTIT_LIP0		0x00000760
#define SAT_MSR_IA32_RTIT_LIP1		0x00000761
#define SAT_MSR_IA32_RTIT_LIP2		0x00000762
#define SAT_MSR_IA32_RTIT_LIP3		0x00000763
#define SAT_MSR_IA32_RTIT_LAST_LIP		0x0000076e
#define SAT_MSR_IA32_RTIT_CR3_MATCH		0x00000777
#define SAT_MSR_IA32_RTIT_PKT_CNT		0x0000077c
#define SAT_MSR_IA32_RTIT_BASE_ADDR		0x00000770
#define SAT_MSR_IA32_RTIT_LIMIT_MASK	0x00000771
#define SAT_MSR_IA32_RTIT_OFFSET		0x00000772
#define SAT_MSR_IA32_RTIT_TNT_BUFF		0x0000077d
#define SAT_MSR_IA32_RTIT_LAST_CALL_NLIP	0x0000076f
#define SAT_MSR_IA32_TSC_ADJUST		0x0000003b

#define TRACE_CTL_TRACEEN		BIT(0)
#define TRACE_CTL_CYCLEACC		BIT(1)
#define TRACE_CTL_OS			BIT(2)
#define TRACE_CTL_USR			BIT(3)
#define TRACE_CTL_STS_ON_CR3	BIT(4)
#define TRACE_CTL_FABRIC_EN		BIT(6)
#define TRACE_CTL_CR3EN			BIT(7)
#define TRACE_CTL_TOPA			BIT(8)
#define TRACE_CTL_MTC_EN		BIT(9)
#define TRACE_CTL_STS_EN		BIT(10)
#define TRACE_CTL_TSC_EN		BIT(10)
#define TRACE_CTL_CMPRS_RET		BIT(11)
#define TRACE_CTL_LESS_PKTS		BIT(12)
#define TRACE_CTL_TRACE_ACTIVE	BIT(13)
#define TRACE_CTL_BRANCH_EN		BIT(13)
#define TRACE_CTL_MTC_RANGE_MASK	(3UL << 14)
#define TRACE_CTL_MTC_FREQ(x)		((x) << 14)
#define TRACE_CTL_CYC_THRESH(x)		((x) << 19)
#define TRACE_CTL_PSB_FREQ(x)		((x) << 24)
#define TRACE_CTL_ADDR0_CFG(x)		((x) << 32)
#define TRACE_CTL_ADDR1_CFG(x)		((x) << 36)

#define TRACE_CTL_IPT_MTC_FREQ_READ(x)	(((x) >> 14) & 0xF)

#define CPU_SUPPORT_INTEL_PT	BIT(25)
#define SAT_MSR_IA32_IPT_CTL			0x00000570
#define SAT_MSR_IA32_IPT_STATUS			0x00000571
#define SAT_MSR_IA32_IPT_CR3_MATCH		0x00000572
#define SAT_MSR_IA32_IPT_OUTPUT_BASE		0x00000560
#define SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS	0x00000561

#define IPT_OUTPUT_OFFSET_READ(x) (((x) >> 32) & 0xffffffff)
#define IPT_OUTPUT_OFFSET_RESET(x) ((x) & 0xffffffff)

#define SAT_MSR_IA32_IPT_ADDR0_A		0x00000580
#define SAT_MSR_IA32_IPT_ADDR0_B		0x00000581
#define SAT_MSR_IA32_IPT_ADDR1_A		0x00000582
#define SAT_MSR_IA32_IPT_ADDR1_B		0x00000583

DECLARE_PER_CPU(struct processor_tracers, processor_tracers);

#define MSR_CHECK_ERR(val)						\
	do {								\
		if (val)						\
			pr_err(" line %d: error %d\n", __LINE__, val);	\
	} while (0)

struct symbol_hook {
	char *name;            /* name of the function to be hooked */
	struct kernsym *sym;   /* kernsym structure */
	void *func;            /* pointer to wrapper function */
	char partial;          /* partial copy flag: 0=full copy, 1=partial copy.
	                          Indicates whether function is executed fully from
	                          copied location or partly from copied and original
	                          location */
};

/* ToPA table */
typedef union {
	unsigned long long address;
	struct {
		unsigned long long
			END   :1,
			rsvd1 :1,
			INT   :1,
			rsvd2 :1,
			STOP  :1,
			rsvd3 :1,
			SIZE  :4,
			rsvd4 :2,
			ADDR  :52;
		};
} topa_table;

typedef struct {
	phys_addr_t   phys;
	topa_table*   table;
} topa_item_type;


typedef struct {
	int              size;
	topa_item_type*  pool;
} trace_topa_pool_type;


typedef struct {
	phys_addr_t		phys;
	void*			virt;
} trace_buffer_addr_type;

typedef struct {
	/* index to first free buffer */
	int			index;
	/* Total number of buffers available in the pool */
	int			size;
	raw_spinlock_t		lock;
	trace_buffer_addr_type	*address;
} trace_buffer_pool_type;

struct trace_conf {
	__u32	exclude_kernel:1,
	exclude_user:1,
	__reserved__:30;
	int	cpu;
	int     trace_format;
};

typedef struct {
	void			*virt;
	struct dentry		*cpu_stream;
	struct dentry		*cpu_offset;
	/* consumed buffers from the trace buffer pool */
	unsigned long		*offsets;
	int			*buffer_id;
	int			active_idx;
	/* total size of trace data in consumed buffers */
	unsigned long		size;
} trace_type;

struct processor_tracers {
	__u8		enabled:1,
	__reserved__:7;
	trace_type	*trace;
};

inline void generate_STS(void);

int reserve_trace_buffers(struct trace_conf *config);

void release_trace_buffers(void);

int trace_start(int cpus);

void trace_stop(void);

void clear_offset(void);

int intel_pt_init(struct dentry *trace_dir_dent,
	      bool exclude_kernel, bool exclude_userspace, int format);
void intel_pt_exit(void);

#endif /* SAT_TRACER_H */
