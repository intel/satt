# SATT Software Analyze Trace Tool

Experimental Linux SW tool to trace, process and analyze full stack SW traces utilizing Intel HW tracing block Intel PT (Intel Processor Trace).

## Overview

SATT allows to trace Linux based OSes running in X86 which has Intel PT tracing block. Intel PT feature needs to be enabled in HW.

Currently it is possible to trace full SW stack from the Linux based system E.g. Android or Ubuntu linux.

Tracing does not need any additional HW, but Intel PT trace is collected in to RAM. In addition to HW instruction trace data, SATT collects needed info from running kernel, e.g. scheduling and memory map information needed to generate execution flow. Post-processing will generate function flow with timing and instruction count of each thread.

Web based UI will allow to study execution in function level from All CPU's, Processes, Threads and modules.

## License

 * SATT kernel module under GNU General Public License version 2.
 * Rest of the SATT tool is licensed under Apache License, Version 2.0.

## Dependencies

  Needed libraries to build and use SATT

  packages:
```
  build-essential scons libelf-dev python-pip git binutils-dev autoconf libtool libiberty-dev zlib1g-dev python-dev (python-virtualenv) postgresql-9.x libpq-dev
```

## Install SATT

### Fetch SATT tool and it's dependencies

  1. Create new empty directory for satt tool
  2. Copy satt/devel/get-satt.sh file to the created directory
  3. run get-satt.sh in the created directory:
```
./get-satt.sh
```

### Install
```
./bin/satt install --ui

```

### Build UI
```
satt devel build-ui

```

## Usage

Simple instructions how to use SATT

### Configure
```
satt config
satt build
```

### Tracing
*NOTE: sudo needed in case tracing local machine*
```
(sudo) satt trace
```

### Process the traces
*NOTE: sudo needed in case processing trace taken from local machine*
```
(sudo) satt process <given-trace-name>
```

### Import & Launch UI
```
satt visualize <given-trace-name>
```

## Disclimer

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
