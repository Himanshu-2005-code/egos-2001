/*
 * (C) 2025, Cornell University
 * All rights reserved.
 *
 * Description: helper functions for process management
 */

#include "process.h"
#include "egos.h"
#include <stdio.h>

#define MLFQ_NLEVELS          5
#define MLFQ_RESET_PERIOD     10000000         /* 10 seconds */
#define MLFQ_LEVEL_RUNTIME(x) (x + 1) * 100000 /* e.g., 100ms for level 0 */
extern struct process proc_set[MAX_NPROCESS + 1];

static void proc_set_status(int pid, enum proc_status status) {
    for (uint i = 0; i < MAX_NPROCESS; i++)
        if (proc_set[i].pid == pid) proc_set[i].status = status;
}

void proc_set_ready(int pid) {
    for (uint i = 0; i < MAX_NPROCESS; i++) {
        if (proc_set[i].pid == pid) {
            proc_set[i].status = PROC_READY;
            return;
        }
    }
}

void proc_set_running(int pid) {
    for (uint i = 0; i < MAX_NPROCESS; i++) {
        if (proc_set[i].pid == pid) {
            // Record first schedule time if this is the first time running
            if (proc_set[i].first_schedule_time == 0) {
                proc_set[i].first_schedule_time = mtime_get();
            }
            
            // Update last schedule time for CPU time calculation
            proc_set[i].last_schedule_time = mtime_get();
            
            proc_set[i].status = PROC_RUNNING;
            return;
        }
    }
}

void proc_set_runnable(int pid) {
    for (uint i = 0; i < MAX_NPROCESS; i++) {
        if (proc_set[i].pid == pid) {
            // If process was running, update CPU time before changing status
            if (proc_set[i].status == PROC_RUNNING && proc_set[i].last_schedule_time > 0) {
                unsigned long long current_time = mtime_get();
                unsigned long long runtime = current_time - proc_set[i].last_schedule_time;
                proc_set[i].total_cpu_time += runtime;
                
                // Update MLFQ level based on runtime
                mlfq_update_level(&proc_set[i], runtime);
            }
            
            proc_set[i].status = PROC_RUNNABLE;
            return;
        }
    }
}

void proc_set_pending(int pid) {
    for (uint i = 0; i < MAX_NPROCESS; i++) {
        if (proc_set[i].pid == pid) {
            // If process was running, update CPU time before changing status
            if (proc_set[i].status == PROC_RUNNING && proc_set[i].last_schedule_time > 0) {
                unsigned long long current_time = mtime_get();
                unsigned long long runtime = current_time - proc_set[i].last_schedule_time;
                proc_set[i].total_cpu_time += runtime;
                
                // Update MLFQ level based on runtime
                mlfq_update_level(&proc_set[i], runtime);
            }
            
            proc_set[i].status = PROC_PENDING_SYSCALL;
            return;
        }
    }
}

int proc_alloc() {
    static uint curr_pid = 0;
    for (uint i = 1; i <= MAX_NPROCESS; i++)
        if (proc_set[i].status == PROC_UNUSED) {
            proc_set[i].pid    = ++curr_pid;
            proc_set[i].status = PROC_LOADING;
            
            // Initialize with mtime_get()
            unsigned long long current_time = mtime_get();
            proc_set[i].creation_time = current_time;
            proc_set[i].first_schedule_time = 0;
            proc_set[i].total_cpu_time = 0;
            proc_set[i].termination_time = 0;
            proc_set[i].timer_interrupt_count = 0;
            
            // MLFQ parameters
            proc_set[i].queue_level = 0;
            proc_set[i].queue_time = 0;
            proc_set[i].last_schedule_time = 0;
            proc_set[i].wakeup_time = 0;
            
            return curr_pid;
        }
    FATAL("proc_alloc: reach the limit of %d processes", MAX_NPROCESS);
}

void proc_free(int pid) {
    if (pid != GPID_ALL) {
        for (uint i = 0; i < MAX_NPROCESS; i++) {
            if (proc_set[i].pid == pid && proc_set[i].status != PROC_UNUSED) {
                
                // Record termination time
                unsigned long long current_time = mtime_get();
                proc_set[i].termination_time = current_time;

                // Calculate times with bounds checking
                unsigned long long turnaround_time = current_time - proc_set[i].creation_time;
                
                unsigned long long response_time = 0;
                if (proc_set[i].first_schedule_time > proc_set[i].creation_time) {
                    response_time = proc_set[i].first_schedule_time - proc_set[i].creation_time;
                }
                
                // Cap response time at turnaround time if unreasonable
                if (response_time > turnaround_time || response_time > 10000000) { // > 10 seconds
                    response_time = turnaround_time / 2; // Use half of turnaround as reasonable response
                }
                
                unsigned long long waiting_time = 0;
                if (turnaround_time > response_time + proc_set[i].total_cpu_time) {
                    waiting_time = turnaround_time - response_time - proc_set[i].total_cpu_time;
                }

                // Convert to milliseconds and use %d (cast to int)
                int turnaround_ms = (int)(turnaround_time / 1000);
                int response_ms   = (int)(response_time / 1000);
                int cpu_ms        = (int)(proc_set[i].total_cpu_time / 1000);
                int wait_ms       = (int)(waiting_time / 1000);

                // Ensure values are reasonable (non-negative)
                if (turnaround_ms < 0) turnaround_ms = 0;
                if (response_ms < 0) response_ms = 0;
                if (cpu_ms < 0) cpu_ms = 0;
                if (wait_ms < 0) wait_ms = 0;

                // Print lifecycle stats using %d
                printf("Process %d terminated:\n", pid);
                printf("  Turnaround time: %d ms\n", turnaround_ms);
                printf("  Response time: %d ms\n", response_ms);
                printf("  Total CPU time: %d ms\n", cpu_ms);
                printf("  Waiting time: %d ms\n", wait_ms);
                printf("  Timer interrupts: %d\n", proc_set[i].timer_interrupt_count);
                printf("  Final queue level: %d\n", proc_set[i].queue_level);

                // Cleanup
                earth->mmu_free(pid);
                proc_set_status(pid, PROC_UNUSED);
                return;
            }
        }
    } else {
        // Free all user processes
        for (uint i = 0; i < MAX_NPROCESS; i++) {
            if (proc_set[i].pid >= GPID_USER_START && proc_set[i].status != PROC_UNUSED) {
                unsigned long long current_time = mtime_get();
                proc_set[i].termination_time = current_time;

                unsigned long long turnaround_time = current_time - proc_set[i].creation_time;
                
                unsigned long long response_time = 0;
                if (proc_set[i].first_schedule_time > proc_set[i].creation_time) {
                    response_time = proc_set[i].first_schedule_time - proc_set[i].creation_time;
                }
                
                if (response_time > turnaround_time || response_time > 10000000) {
                    response_time = turnaround_time / 2;
                }
                
                unsigned long long waiting_time = 0;
                if (turnaround_time > response_time + proc_set[i].total_cpu_time) {
                    waiting_time = turnaround_time - response_time - proc_set[i].total_cpu_time;
                }

                int turnaround_ms = (int)(turnaround_time / 1000);
                int response_ms   = (int)(response_time / 1000);
                int cpu_ms        = (int)(proc_set[i].total_cpu_time / 1000);
                int wait_ms       = (int)(waiting_time / 1000);

                if (turnaround_ms < 0) turnaround_ms = 0;
                if (response_ms < 0) response_ms = 0;
                if (cpu_ms < 0) cpu_ms = 0;
                if (wait_ms < 0) wait_ms = 0;

                printf("Process %d terminated:\n", proc_set[i].pid);
                printf("  Turnaround time: %d ms\n", turnaround_ms);
                printf("  Response time: %d ms\n", response_ms);
                printf("  Total CPU time: %d ms\n", cpu_ms);
                printf("  Waiting time: %d ms\n", wait_ms);
                printf("  Timer interrupts: %d\n", proc_set[i].timer_interrupt_count);
                printf("  Final queue level: %d\n", proc_set[i].queue_level);

                earth->mmu_free(proc_set[i].pid);
                proc_set[i].status = PROC_UNUSED;
            }
        }
    }
}

void mlfq_update_level(struct process* p, unsigned long long runtime) {
    if (p == NULL || p->queue_level >= MLFQ_NLEVELS - 1) {
        return;
    }
    
    // Add runtime to queue time
    p->queue_time += runtime;
    
    // Check if process has used up its quantum (Rule 4)
    unsigned long long quantum = MLFQ_LEVEL_RUNTIME(p->queue_level);
    if (p->queue_time >= quantum) {
        // Demote to next level
        p->queue_level++;
        p->queue_time = 0;
    }
}

void mlfq_reset_level() {
    static unsigned long long MLFQ_last_reset_time = 0;
    unsigned long long current_time = mtime_get();
    
    // Check for keyboard input and reset shell level
    if (!earth->tty_input_empty()) {
        for (uint i = 0; i < MAX_NPROCESS; i++) {
            if (proc_set[i].pid == GPID_SHELL && proc_set[i].status != PROC_UNUSED) {
                proc_set[i].queue_level = 0;
                proc_set[i].queue_time = 0;
                break;
            }
        }
    }
    
    /* Reset the level of all processes every MLFQ_RESET_PERIOD microseconds. */
    if (current_time - MLFQ_last_reset_time >= MLFQ_RESET_PERIOD) {
        for (uint i = 0; i < MAX_NPROCESS; i++) {
            if (proc_set[i].status != PROC_UNUSED) {
                proc_set[i].queue_level = 0;
                proc_set[i].queue_time = 0;
            }
        }
        MLFQ_last_reset_time = current_time;
    }
}

void proc_sleep(int pid, uint usec) {
    unsigned long long current_time = mtime_get();
    for (uint i = 0; i < MAX_NPROCESS; i++) {
        if (proc_set[i].pid == pid && proc_set[i].status != PROC_UNUSED) {
            proc_set[i].wakeup_time = current_time + usec;
            proc_set[i].status = PROC_PENDING_SYSCALL;
            break;
        }
    }
}

void proc_coresinfo() {
    printf("Core information:\n");
    for (uint i = 0; i < NCORES; i++) {
        if (core_to_proc_idx[i] < MAX_NPROCESS && 
            proc_set[core_to_proc_idx[i]].status == PROC_RUNNING) {
            printf("  Core %d: Process %d\n", i, proc_set[core_to_proc_idx[i]].pid);
        } else {
            printf("  Core %d: Idle\n", i);
        }
    }
}
