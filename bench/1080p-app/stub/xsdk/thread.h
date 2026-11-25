#pragma once
#include <pthread.h>
#include <stdint.h>
typedef unsigned HANDLE;
typedef int NTSTATUS;
HANDLE xsdk_pthread_handle(pthread_t);
NTSTATUS xsdk_thread_place(HANDLE,unsigned);
NTSTATUS xsdk_thread_set_priority(HANDLE,int32_t,int32_t*);
