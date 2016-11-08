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
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/perf_event.h>
#include <asm/insn.h>
#include <asm-generic/sizes.h>

#include "config.h"
#include "sat-tracer.h"
#include "sat-buffer.h"


DEFINE_PER_CPU(struct processor_tracers, processor_tracers) = {
	.enabled = 0,
};

extern trace_buffer_pool_type buffer_pool;
extern trace_topa_pool_type topa_pool;

int init_trace_ram_alloc(int cpu, gfp_t gfp)
{
	trace_type *trace;
	int node = cpu_to_node(cpu);

	trace = kmalloc_node(sizeof(*trace), gfp | __GFP_ZERO, node);
	if (!trace)
		return -ENOMEM;
	if (trace_method == LOG_TO_RAM) {
		trace->offsets = kmalloc_node(buffer_pool.size * sizeof(*trace->offsets),
			gfp | __GFP_ZERO, node);
		if (!trace->offsets) {
			kfree(trace);
			return -ENOMEM;
		}
		trace->buffer_id = kmalloc_node(buffer_pool.size * sizeof(*trace->buffer_id),
			gfp | __GFP_ZERO, node);
		if (!trace->buffer_id) {
			kfree(trace->offsets);
			kfree(trace);
			return -ENOMEM;
		}
		if(reset_trace_buffers(trace)) {
			kfree(trace->buffer_id);
			kfree(trace->offsets);
			kfree(trace);
			return -ENOMEM;
		}
	}
	else if (trace_method == LOG_TO_PTI) {
		trace->virt = 0;
	}

	per_cpu(processor_tracers, cpu).trace = trace;

	return 0;
}

void exit_trace_ram_alloc(int cpu)
{
	trace_type *trace = per_cpu(processor_tracers, cpu).trace;

	if (!trace)
		return;

	if (trace->virt)
		iounmap(trace->virt);
	if (trace->offsets)
		kfree(trace->offsets);
	if (trace->buffer_id)
		kfree(trace->buffer_id);

	per_cpu(processor_tracers, cpu).trace = NULL;

	kfree(trace);
}


int request_trace_buffer(trace_type *trace)
{
	int new_buf_idx = -1;

	raw_spin_lock(&buffer_pool.lock);
	if(buffer_pool.index < (buffer_pool.size)) {
		new_buf_idx = buffer_pool.index;
		buffer_pool.index += 1;
	}
	raw_spin_unlock(&buffer_pool.lock);

	return new_buf_idx;
}

int reset_trace_buffers(trace_type *trace)
{
	int i;

	trace->active_idx = 0;
	trace->size = 0;

	for (i = 0; i < buffer_pool.size; ++i) {
		trace->offsets[i] = 0;
		trace->buffer_id[i] = -1;
	}
	trace->buffer_id[trace->active_idx] = request_trace_buffer(trace);
	if(trace->buffer_id[trace->active_idx] < 0)
		return -ENOMEM;

	return 0;
}

void reinit_trace_buffer_pool(void)
{
	int i;
	unsigned int *ptr;

	buffer_pool.index = 0;
	for (i = 0; i < buffer_pool.size; i++) {
		ptr = (unsigned int*) ((unsigned char*)buffer_pool.address[i].virt + TRACE_BUFFER_SIZE - 4);
		*ptr = 0xDEADBEEF;
	}
}

void release_trace_buffer_pool(void)
{
	int i;

	for (i = 0; i < buffer_pool.size; i++) {
		free_pages((unsigned long)buffer_pool.address[i].virt, TRACE_BUFFER_ORDER);
        if (processor_trace_version == TRACE_IPT) {
		      free_pages((unsigned long)topa_pool.pool[i].table, 0);
        }
		buffer_pool.address[i].virt = 0;
		buffer_pool.address[i].phys = 0;
	}
	buffer_pool.size = 0;
	kfree(buffer_pool.address);
	kfree(topa_pool.pool);
}


int create_topa_pool(int buffer_pool_size)
{
	int i;
	void* virt_add;

	topa_pool.size = buffer_pool_size;
	topa_pool.pool = kmalloc(topa_pool.size *
		sizeof(*topa_pool.pool), __GFP_ZERO);
	for (i = 0; i < buffer_pool_size; i++)
	{
		virt_add = (void*) __get_free_pages(GFP_KERNEL | __GFP_ZERO, 0);
		if (virt_add) {
			topa_pool.pool[i].table = (topa_table*) (virt_add);
			topa_pool.pool[i].phys = (phys_addr_t) virt_to_phys(virt_add);
			topa_pool.pool[i].table[0].address = buffer_pool.address[i].phys;
			topa_pool.pool[i].table[0].SIZE = 10;
			topa_pool.pool[i].table[1].address = topa_pool.pool[i].phys;
			topa_pool.pool[i].table[1].END = 1;
		} else {
			printk(KERN_ERR "SAT: Can't allocate topa list!!\n");
			return -ENOMEM;
		}
	}
	return 0;
}


int create_trace_buffer_pool(void)
{
	int i;
	unsigned long virt_add;

	raw_spin_lock_init(&buffer_pool.lock);
	buffer_pool.index = 0;
	buffer_pool.size = 0;

	/* Try to find as many free pages as possible until
	 * max_trace_buffers limit reached
	 */
	buffer_pool.address = kmalloc(max_trace_buffers *
		sizeof(*buffer_pool.address), __GFP_ZERO);
	if (!buffer_pool.address)
		return -ENOMEM;


	for (i = 0; i < max_trace_buffers; i++) {
		virt_add = __get_free_pages(GFP_KERNEL, TRACE_BUFFER_ORDER);
		if(virt_add) {
			buffer_pool.address[i].virt = (void*)virt_add;
			buffer_pool.address[i].phys = virt_to_phys((void*)virt_add);
			printk("SAT: buffer_pool.address[%d].virt: %p\n",
				i, buffer_pool.address[i].virt);
			printk("SAT: buffer_pool.address[%d].phys: 0x%lx\n",
				i, (unsigned long) buffer_pool.address[i].phys);
			/* debug: initialize buffers with "0xEE" to detect fetch issues */
			memset(buffer_pool.address[i].virt, 0xEE, TRACE_BUFFER_SIZE);
			buffer_pool.size += 1;
		} else {
			break;
		}
	}
	printk(KERN_INFO "SAT: %d/%d buffers got for buffer pool\n", buffer_pool.size, max_trace_buffers);
	/* Update max_trace_buffers parameter with actual buffer count */
	max_trace_buffers = buffer_pool.size;

	/* Exit in case zero buffers reserved */
	if (!buffer_pool.size)
		return -ENOMEM;

    if (processor_trace_version == TRACE_RTIT) {
		return 0;
	}
	else if (processor_trace_version == TRACE_IPT) {
        return create_topa_pool(buffer_pool.size);
    }

    return 0;

}
