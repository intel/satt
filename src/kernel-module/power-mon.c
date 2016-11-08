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

#include "power-mon.h"

#ifdef POWER_MONITORING_FROM_FUEL_GAUGE

#include <linux/device.h>
#include <linux/power_supply.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include "sat_payload.h"
#include "main.h"

#define ROUGH_MS_TO_NS(x)	(x << 20)

static struct power_supply *psy = NULL;
static ktime_t period;
static struct hrtimer pmtimer;

static char avg_current_enabled = false;
static char voltage_now_enabled = false;

/* Workqueue reading voltage and current */
struct work_struct  pm_work;
void pm_get_power(struct work_struct *work);

static struct power_supply *get_psy_battery(void)
{
    struct class_dev_iter iter;
    struct device *dev;
    static struct power_supply *pst;

    class_dev_iter_init(&iter, power_supply_class, NULL, NULL);
    while ((dev = class_dev_iter_next(&iter))) {
        pst = (struct power_supply *)dev_get_drvdata(dev);
        if (pst->type == POWER_SUPPLY_TYPE_BATTERY) {
            class_dev_iter_exit(&iter);
            return pst;
        }
    }
    class_dev_iter_exit(&iter);

    return NULL;
}

/* Reading values from the fuel gauge */
static int read_fuel_gauge(int *value, int supply)
{
    union power_supply_propval val;
    int ret;

    if (!psy)
        return -EINVAL;

    ret = psy->get_property(psy, supply, &val);
    if (!ret)
        *value = (val.intval);

    return ret;
}

void pm_get_power(struct work_struct *work) {
    int volt=0, curr=0;
    char msg_name[5] = "pm\000";
    sat_msg umsg;
    sat_msg_generic *msg = &umsg.generic;

    if (avg_current_enabled &&
        !read_fuel_gauge(&curr, POWER_SUPPLY_PROP_CURRENT_AVG)) {
    }
    if (voltage_now_enabled &&
        !read_fuel_gauge(&volt, POWER_SUPPLY_PROP_VOLTAGE_NOW)) {
    }

    msg->header.size = sizeof(*msg);
    msg->header.type = SAT_MSG_GENERIC;
    strncpy(msg->name, msg_name, 5);
    memset(msg->data, '\0', sizeof(msg->data));
    sprintf(msg->data, "%d|%d", curr, volt);
    msg->header.size -= sizeof(msg->data) - (strlen(msg->data) + 1);

    send_sat_msg(&umsg);
}

static enum hrtimer_restart pm_timer_callback(struct hrtimer *timer)
{
    schedule_work(&pm_work);
    hrtimer_forward_now(timer, period);
    return HRTIMER_RESTART;
}

int init_power_monitor()
{
    int x=0;

    psy = get_psy_battery();
    if (!psy)
        return -EINVAL;

#ifdef DEBUG
    printk(KERN_DEBUG "SAT bat = %s\n", psy->name);
    printk(KERN_DEBUG "SAT bat type = %d\n", psy->type);
    for(x=0 ; x<psy->num_properties ; x++) {
        printk(KERN_DEBUG "SAT bat prop = %d\n", psy->properties[x]);
    }
#endif /* DEBUG */

    for (x=0 ; x<psy->num_properties ; x++) {
        if (psy->properties[x] == POWER_SUPPLY_PROP_CURRENT_AVG) {
            avg_current_enabled = true;
        }
        if (psy->properties[x] == POWER_SUPPLY_PROP_VOLTAGE_NOW) {
            voltage_now_enabled = true;
        }
    }

    if (!(avg_current_enabled || voltage_now_enabled))  {
        printk(KERN_DEBUG "SAT Power Monitor failed to get supported properties\n");
        return -EIO;
    }

    /* Init SB memory tracer */
    INIT_WORK(&pm_work, pm_get_power);

    hrtimer_init(&pmtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    pmtimer.function = pm_timer_callback;

    return 0;
}

void start_power_monitoring(void)
{
    long unsigned delay_in_ms = (long unsigned) power_monitor;
    if (0==delay_in_ms || delay_in_ms > 1000)
        return;
    schedule_work(&pm_work);
    period = ktime_set(0, ROUGH_MS_TO_NS(delay_in_ms));
    hrtimer_start(&pmtimer, period, HRTIMER_MODE_REL_PINNED);
}

void stop_power_monitoring(void)
{
    hrtimer_cancel(&pmtimer);
}

#endif /* POWER_MONITORING_FROM_FUEL_GAUGE */