DISCONTINUATION OF PROJECT.

This project will no longer be maintained by Intel.

Intel has ceased development and contributions including, but not limited to, maintenance, bug fixes, new releases, or updates, to this project. 

Intel no longer accepts patches to this project.

If you have an ongoing need to use this project, are interested in independently developing it, or would like to maintain patches for the open source software community, please create your own fork of this project. 
# SATT Software Analyze Trace Tool

Experimental Linux SW tool to trace, process and analyze full stack SW traces utilizing Intel HW tracing block Intel PT (Intel Processor Trace).

![alt text](https://raw.githubusercontent.com/01org/satt/master/doc/img/sat-intro-gui.jpg)

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

### Install needed dependencies
Ubuntu 16.04 tracing PC example:
```
sudo apt install build-essential scons libelf-dev python-pip git binutils-dev autoconf libtool libiberty-dev zlib1g-dev python-dev python-virtualenv postgresql-9.5 libpq-dev
```

### Clone or Download SATT tool
```
git clone https://github.com/01org/satt.git
```

### Install
```
./bin/satt install --ui

```
Installer will ask sudo access rights when needed
 * Adds the satt command to to path
 * Adds the satt to bash completion (sudo)
 * Download and compile disassembler (Capstone)
 * Compile SATT parser
 * Install python virtual-env under <satt>/bin/env folder
 * Install needed python packaged to virtual-env

When --ui flag used
 * Adds satt user for postgres db (sudo)
 * Adds satt db for postgres db (sudo)

### Build UI
```
satt devel build-ui

```

## Usage

Simple instructions how to use SATT

### Configure
```
satt config
```

### Build SATT kernel module
```
satt build
```

### Tracing
*NOTE: sudo needed in case tracing local machine*
```
satt trace
```

### Process the traces
*NOTE: sudo needed in case processing trace taken from local machine*
```
satt process <given-trace-name>
```

### Import & Launch UI
```
satt visualize <given-trace-name>
```

## Disclimer

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
