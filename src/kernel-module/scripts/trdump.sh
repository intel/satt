#!/usr/bin/env bash

cat /sys/kernel/debug/sat/cpu1_stream > cpu1.bin
cat /sys/kernel/debug/sat/cpu2_stream > cpu2.bin
cat /sys/kernel/debug/sat/cpu1_offset > offset.txt
cat /sys/kernel/debug/sat/cpu2_offset >> offset.txt
cat /sys/kernel/debug/sat/sideband_data >> sideband.bin

