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
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include "sat-tracer.h"
#include "sat-buffer.h"
#include "config.h"

#define MS_TO_NS(x)	(x * 1E6L)

extern trace_buffer_pool_type buffer_pool;
extern trace_topa_pool_type topa_pool;

DEFINE_PER_CPU(struct hrtimer, hrtimers);

static int active;
static ktime_t ktime;
static raw_spinlock_t timer_lock;
static uint32_t msr_offset_address;
static uint8_t  msr_offset_shift = 32;
static uint32_t msr_offset_mask = 0xffffffff;
static uint32_t msr_base_address;

inline void __trace_timer_run_config(bool start)
{
	u64 ctl;

	if (processor_trace_version == TRACE_RTIT) {
		/* flip FilterEn and trace_active */
		rdmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);

		if (start) {
			ctl |= TRACE_CTL_TRACE_ACTIVE;
			wrmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);
			ctl |= TRACE_CTL_TRACEEN;
			wrmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);

		} else {
			ctl &= ~TRACE_CTL_TRACE_ACTIVE;
			wrmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);
			ctl &= ~TRACE_CTL_TRACEEN;
			wrmsrl(SAT_MSR_IA32_RTIT_CTL, ctl);
		}
	} else if (processor_trace_version == TRACE_IPT) {
		rdmsrl(SAT_MSR_IA32_IPT_CTL, ctl);
		if (start) {
			ctl |= TRACE_CTL_TRACEEN;
		} else {
			ctl &= ~TRACE_CTL_TRACEEN;
		}
		wrmsrl(SAT_MSR_IA32_IPT_CTL, ctl);
	}
}

static enum hrtimer_restart hr_timer_callback(struct hrtimer *timer)
{
	enum hrtimer_restart timer_ret = HRTIMER_RESTART;
	u64 offset;
	trace_type *trace;
	int new_buf_idx = 0;

	/* Read offset */
	rdmsrl(msr_offset_address, offset);
	offset = (offset >> msr_offset_shift) & msr_offset_mask;

	/* Check if we should change processor tracing buffer? */
	if (offset + TRACE_BUFFER_SIZE / 3 < TRACE_BUFFER_SIZE) {
		/* Just restart timer, let's check offset status on next run */
		hrtimer_forward_now(timer, ktime);
		return HRTIMER_RESTART;
	} else {
		/* Stop tracing */
		__trace_timer_run_config(false);

		//__trace_timer_run_config(false);
		trace = this_cpu_ptr(&processor_tracers)->trace;
		new_buf_idx = request_trace_buffer(trace);
		raw_spin_lock(&timer_lock);
		if(active)
		{
			/* Read offset again */
			rdmsrl(msr_offset_address, offset);
			offset = (offset >> msr_offset_shift) & msr_offset_mask;
			/* Store Offset point */
			trace->offsets[trace->active_idx] = offset;
			/* Calculate total recorded trace data size */
			trace->size += offset;
			if (new_buf_idx >= 0)
			{
				trace->active_idx += 1;
				trace->buffer_id[trace->active_idx] = new_buf_idx;
			}
		} else {
			/* if timer already disabled, then don't prepare new buffers anymore */
			new_buf_idx = -1;
		}
		raw_spin_unlock(&timer_lock);

		if (new_buf_idx >= 0) {
			/* Move Base pointer */
			if (processor_trace_version == TRACE_RTIT) {
				wrmsrl(msr_base_address,
					buffer_pool.address[trace->buffer_id[trace->active_idx]].phys);
			} else if (processor_trace_version == TRACE_IPT) {
				wrmsrl(msr_base_address,
					topa_pool.pool[trace->buffer_id[trace->active_idx]].phys);
			}

			/* Reset offset */
			rdmsrl(msr_offset_address, offset);
			offset = offset & ~(msr_offset_mask << msr_offset_shift);
			wrmsrl(msr_offset_address, offset);

			/* Restart timer */
			hrtimer_forward_now(timer, ktime);

			/* Start tracing again */
			__trace_timer_run_config(true);
			timer_ret = HRTIMER_RESTART;
		} else {
			/* All buffers already in use - end tracing for this core */

			/* Stop tracing */
			__trace_timer_run_config(false);
			timer_ret = HRTIMER_NORESTART;
		}
		return timer_ret;
	}
}

void init_buffer_hack_timers(void)
{
	int cpu;
	active = false;
	if (processor_trace_version == TRACE_IPT) {
		msr_offset_address = SAT_MSR_IA32_IPT_OUTPUT_MASK_PTRS;
		msr_offset_shift = 32;
		msr_offset_mask = 0xffffffff;
		msr_base_address = SAT_MSR_IA32_IPT_OUTPUT_BASE;
	} else {
		/* Default is RTIT */
		msr_offset_address = SAT_MSR_IA32_RTIT_OFFSET;
		msr_offset_shift = 0;
		msr_offset_mask = 0xffffffff;
		msr_base_address = SAT_MSR_IA32_RTIT_BASE_ADDR;
	}

	for_each_present_cpu(cpu) {
		struct hrtimer *hrtimer;
		hrtimer = &per_cpu(hrtimers, cpu);
		hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		hrtimer->function = hr_timer_callback;
	}
	raw_spin_lock_init(&timer_lock);
}

/* Running in specific CPU's context */
void start_buffer_hack_timer(void)
{
	trace_type *trace;
	unsigned long delay_in_ms = 2L;
	struct hrtimer *hrtimer = this_cpu_ptr(&hrtimers);
	active = true;

	trace = this_cpu_ptr(&processor_tracers)->trace;
	reset_trace_buffers(trace);

	ktime = ktime_set(0, MS_TO_NS(delay_in_ms));
	hrtimer_start(hrtimer, ktime, HRTIMER_MODE_REL_PINNED);
}

void stop_buffer_hack_timer(void)
{
	trace_type *trace;
	struct hrtimer *hrtimer;
	u64 offset;

	hrtimer = this_cpu_ptr(&hrtimers);
	hrtimer_cancel(hrtimer);

	trace = this_cpu_ptr(&processor_tracers)->trace;
	rdmsrl(msr_offset_address, offset);
	offset = (offset >> msr_offset_shift) & msr_offset_mask;

	raw_spin_lock(&timer_lock);
	if(!trace->offsets[trace->active_idx]) {
		trace->offsets[trace->active_idx] = offset;
		trace->size += offset;
	}
	active = false;
	raw_spin_unlock(&timer_lock);
}
