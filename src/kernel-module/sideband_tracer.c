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
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/timer.h>
#include <linux/cpu.h>
#include <linux/slab.h>

#include "sideband_tracer.h"
#include "config.h"
#include "sat_payload.h"

#define stream_formatstring	"cpu%u_sideband"

int sideband_buffer_count=1;

typedef struct sb_info_type {
	int active;
	int offset[2];
	struct dentry *sideband_data;
	char* sb_buf[2];

	int   tmp_buf_size;
	char *tmp_buf;
} sb_info_t;

struct sideband_tracer {
	sb_info_t sb_info;
};

static size_t sideband_bufsize = SIDEBAND_BUFFER_SIZE;
static size_t sideband_switch_trigger =
	(SIDEBAND_BUFFER_SIZE / SIDEBAND_SWITCH_TRIGGER_LEVEL) *
	(SIDEBAND_SWITCH_TRIGGER_LEVEL - 1);

DEFINE_PER_CPU(struct sideband_tracer, sideband_tracer) = {
	.sb_info.active = 0,
	.sb_info.offset = {0, 0},
	.sb_info.sb_buf = {NULL, NULL},
	.sb_info.tmp_buf_size = 0,
	.sb_info.tmp_buf = NULL,
};

static int open_sideband_fop(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

ssize_t get_sideband_data(char *target, int start_pos, int size, int *sb_total_size, sb_info_t *sb_info)
{
	ssize_t chunk_size = 0;
	int non_active_buf = (sb_info->active + 1) % 2;

	*sb_total_size = sb_info->offset[sb_info->active] +
			sb_info->offset[non_active_buf];

	if (size==0)
		size = *sb_total_size;

	/* move on the "*ppos" location first */
	chunk_size = size;

	if(sb_info->offset[non_active_buf] > start_pos) {
		/* Read from previous (full) buffer */
		if(sb_info->offset[non_active_buf] < start_pos + size)
			chunk_size = sb_info->offset[non_active_buf] - start_pos;
		memcpy(target, sb_info->sb_buf[non_active_buf] + start_pos, chunk_size);
		if(chunk_size < size) {
			int read_off = chunk_size;
			chunk_size = size - chunk_size;
			if(chunk_size > sb_info->offset[sb_info->active])
				chunk_size = sb_info->offset[sb_info->active];
			memcpy(target + read_off, sb_info->sb_buf[sb_info->active], chunk_size);
			chunk_size = read_off + chunk_size;
		}
	}
	/* if sb_info->offset[non_active_buf] <= *ppos  */
	else {
		/* Jump to second (active) buffer */
		int read_off = start_pos - sb_info->offset[non_active_buf];
		if(size > sb_info->offset[sb_info->active] - read_off)
			chunk_size = sb_info->offset[sb_info->active] - read_off;
		memcpy(target, sb_info->sb_buf[sb_info->active] + read_off, chunk_size);
	}
	return chunk_size;
}

static ssize_t read_sideband_data_fop(struct file *file, char __user *user_buf,
		size_t count, loff_t *ppos)
{
	int sb_total_size = 0;
	ssize_t size = 0;
	int cpu = (int)(long)file->private_data;
	sb_info_t *sb_info = &per_cpu(sideband_tracer, cpu).sb_info;

	/* Allocate temporary consecutive buffer */
	if(!*ppos || count > sb_info->tmp_buf_size) {
		if(sb_info->tmp_buf) {
			vfree(sb_info->tmp_buf);
			sb_info->tmp_buf_size = 0;
		}

		sb_info->tmp_buf = vmalloc(count);
		if (!sb_info->tmp_buf)
			return -ENOMEM;

		sb_info->tmp_buf_size = count;
	}

	size = get_sideband_data(sb_info->tmp_buf, (int)*ppos, count, &sb_total_size, sb_info);

	size = simple_read_from_buffer(user_buf, size, ppos,
		(sb_info->tmp_buf-*ppos), sb_total_size);

	return size;
}


static const struct file_operations sideband_data_fops = {
	.open = open_sideband_fop,
	.read = read_sideband_data_fop
};

void sb_reset(void)
{
	int i;
	int cpu;

	get_online_cpus();

	for_each_present_cpu(cpu) {
		sb_info_t *sb_info = &per_cpu(sideband_tracer, cpu).sb_info;
		sb_info->active = 0;
		for(i=0; i<sideband_buffer_count; i++) {
			sb_info->offset[i] = 0;
			memset(sb_info->sb_buf[i], 0, sideband_bufsize);
			if  (sideband_log_method == LOG_TO_RAM) {
				memcpy(sb_info->sb_buf[i], SIDEBAND_VERSION, sizeof(SIDEBAND_VERSION)-1);
				sb_info->offset[i] = sizeof(SIDEBAND_VERSION)-1;
			}
		}
	}

	put_online_cpus();
}

void sb_exit(void)
{
	int i;
	int cpu;

	get_online_cpus();

	for_each_present_cpu(cpu) {
		sb_info_t *sb_info = &per_cpu(sideband_tracer, cpu).sb_info;

		for(i=0; i<sideband_buffer_count; i++) {
			if (sb_info->sb_buf[i]) {
				vfree(sb_info->sb_buf[i]);
				sb_info->sb_buf[i] = NULL;
			}
		}
		if (sb_info->tmp_buf) {
			vfree(sb_info->tmp_buf);
			sb_info->tmp_buf = NULL;
		}
	}

	put_online_cpus();
}

int sb_init(struct dentry *sat_dent)
{
	int i;
	char *buf;
	int len;
	int cpu;

	bool successful = true;

	/* Figure out the maximum length of int in characters and allocate
	 * a buffer of necessary size; we could hardcode this,
	 * but that'd be non-portable between 32/64-bit platforms.
	 * This bit is only used when we log to RAM.
	 */
	len = snprintf(NULL, 0, "%u", (unsigned)-1);
	len += strlen(stream_formatstring);
	buf = kmalloc(len + 1, GFP_KERNEL);

#ifdef PANIC_TRACER
	if (panic_sideband)
		ring_buffer_tracing = 1;
#endif /* PANIC_TRACER */

	if (ring_buffer_tracing)
		sideband_buffer_count = 2;

	get_online_cpus();

	for_each_present_cpu(cpu) {
		if (successful) {
			sb_info_t *sb_info = &per_cpu(sideband_tracer, cpu).sb_info;
			sb_info->active = 0;
			for(i=0; i<sideband_buffer_count; i++) {
				sb_info->sb_buf[i] = vmalloc(sideband_bufsize);
				if (!sb_info->sb_buf[i]) {
					while(i>0) {
						vfree(sb_info->sb_buf[--i]);
						sb_info->sb_buf[i] = NULL;
					}
					return -ENOMEM;
				}
				sb_info->offset[i] = 0;
				memset(sb_info->sb_buf[i], 0, sideband_bufsize);
			}

			scnprintf(buf, len, stream_formatstring, cpu);

			sb_info->sideband_data =
				debugfs_create_file(buf, S_IRUSR | S_IRGRP,
					sat_dent,
					(void *)(long)cpu,
					&sideband_data_fops);

			if (!sb_info->sideband_data) {
				printk(KERN_ALERT "SAT: sideband data handle for cpu %d creating failed\n", cpu);
				successful = false;
			}
		}
	}
	put_online_cpus();

	if (!successful) {
		sb_exit();
		return -1;
	}
	return 0;
}

void sb_trace(u8 *data, int count)
{
	sb_info_t *sb_info = &(this_cpu_ptr(&sideband_tracer)->sb_info);
	if (sb_info->offset[sb_info->active] + count < sideband_bufsize) {
		/* Write to active buffer */
		memcpy(sb_info->sb_buf[sb_info->active] +
			sb_info->offset[sb_info->active],
			data,
			count);

		sb_info->offset[sb_info->active] += count;
		if(ring_buffer_tracing &&
			(sb_info->offset[sb_info->active] >= sideband_switch_trigger))
		{
			if(sb_info->active)
				sb_info->active = 0;
			else
				sb_info->active = 1;

			sb_info->offset[sb_info->active] = 0;
		}
	}
}
