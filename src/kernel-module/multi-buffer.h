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
#ifndef MULTI_BUFFER_H
#define MULTI_BUFFER_H

void init_buffer_hack_timers(void);
void start_buffer_hack_timer(void);
void stop_buffer_hack_timer(void);

#endif /* MULTI_BUFFER_H */
