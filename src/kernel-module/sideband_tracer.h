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
#ifndef SIDEBAND_TRACER_H
#define SIDEBAND_TRACER_H

int sb_init(struct dentry *sat_dent);
void sb_exit(void);
void sb_reset(void);
void sb_trace(u8 *data, int count);

#endif /* SIDEBAND_TRACER_H */
