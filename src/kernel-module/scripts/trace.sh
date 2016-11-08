#!/usr/bin/env bash

sleep 1
echo 1 > /sys/kernel/debug/sat/trace_enable

sleep 0.1

# run your your workload here

sleep 0.1 

echo 0 > /sys/kernel/debug/sat/trace_enable

sleep 1

