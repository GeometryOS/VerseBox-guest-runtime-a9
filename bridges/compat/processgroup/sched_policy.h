/*
 * AOSP 9 keeps SchedPolicy in cutils; the project's Scheduling.cpp includes
 * the newer <processgroup/sched_policy.h> path. Forward to the A9 header so
 * the enum values stay identical.
 */
#ifndef A9VM_PROCESSGROUP_SCHED_POLICY_H
#define A9VM_PROCESSGROUP_SCHED_POLICY_H
#include <cutils/sched_policy.h>
#endif
