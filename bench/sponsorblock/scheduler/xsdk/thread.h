#pragma once
#include <pthread.h>
#include <stdint.h>
// Adaptador exclusivo da bancada Linux: preserva a ordem de prioridades com nice.
using HANDLE=void*;
using NTSTATUS=int32_t;
#define XSDK_CURRENT_THREAD nullptr
#define NT_SUCCESS(value) ((value)>=0)
NTSTATUS xsdk_thread_move(uint32_t);
NTSTATUS xsdk_thread_sleep(uint32_t);
uint32_t xsdk_thread_hardware_thread();
NTSTATUS xsdk_thread_set_priority(HANDLE,int32_t,int32_t*);
int f34_getschedparam(pthread_t,int*,sched_param*);
#define pthread_getschedparam f34_getschedparam
