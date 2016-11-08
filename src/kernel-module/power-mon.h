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
#ifndef POWER_MON_H
#define POWER_MON_H

#include "config.h"

#ifdef POWER_MONITORING_FROM_FUEL_GAUGE
int init_power_monitor(void);
void start_power_monitoring(void);
void stop_power_monitoring(void);
#endif /* POWER_MONITORING_FROM_FUEL_GAUGE */

#endif /* POWER_MON_H */
