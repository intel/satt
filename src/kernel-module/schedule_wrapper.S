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

#define JUMP_TO_SWITCH_TO \
    jmp *(switch_to_ptr)(%rip);

#ifdef __i386__
#define FUNC_IN \
    push %eax; \
    push %esi; \
    push %edi; \
    push %ecx; \
    push %edx;
#else
#define FUNC_IN \
    push %rax; \
    push %rsi; \
    push %rdi;
#endif

#ifdef __i386__
#define FUNC_OUT \
    pop %edx; \
    pop %ecx; \
    pop %edi; \
    pop %esi; \
    pop %eax;
#else
#define FUNC_OUT \
    pop %rdi; \
    pop %rsi; \
    pop %rax;
#endif

#ifdef __i386__
#define FUNC_OUT_RTIT \
    pop %edx; \
    pop %ecx; \
    pop %edi; \
    pop %esi; \
    mov %cr3, %eax; \
    mov %eax, %cr3; \
    pop %eax;
#else
#define FUNC_OUT_RTIT \
    pop %rdi; \
    pop %rsi; \
    mov %cr3, %rax; \
    mov %rax, %cr3; \
    pop %rax;
#endif


    .text;
    .code64;

    .global switch_to_wrapper_ipt;
    .type switch_to_wrapper_ipt, @function;
switch_to_wrapper_ipt:
    FUNC_IN
    call send_schedule;
    FUNC_OUT;
    JUMP_TO_SWITCH_TO;

    .text;
    .code64;

    .global switch_to_wrapper_rtit;
    .type switch_to_wrapper_rtit, @function;
switch_to_wrapper_rtit:
    FUNC_IN
    call send_schedule;
    FUNC_OUT_RTIT;
    JUMP_TO_SWITCH_TO;
