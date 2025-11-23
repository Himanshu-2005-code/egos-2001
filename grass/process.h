#pragma once

#include "egos.h"
#include "syscall.h"

enum proc_status {
    PROC_UNUSED,
    PROC_LOADING,
    PROC_READY,
    PROC_RUNNING,
    PROC_RUNNABLE,
    PROC_PENDING_SYSCALL
};

#define MAX_NPROCESS        16
#define SAVED_REGISTER_NUM  32
#define SAVED_REGISTER_SIZE SAVED_REGISTER_NUM * 4
#define SAVED_REGISTER_ADDR (void*)(EGOS_STACK_TOP - SAVED_REGISTER_SIZE)

// MLFQ constants
#define MLFQ_LEVELS 5
#define MLFQ_BASE_QUANTUM 100  // milliseconds
#define MLFQ_RESET_INTERVAL 10000 // 10 seconds

struct process {
    int pid;
    struct syscall syscall;
    enum proc_status status;
    uint mepc, saved_registers[SAVED_REGISTER_NUM];
    
    /* Student's code goes here (Preemptive Scheduler | System Call). */
    
    // Lifecycle statistics
    unsigned long long creation_time;
    unsigned long long first_schedule_time;
    unsigned long long total_cpu_time;
    unsigned long long termination_time;
    int timer_interrupt_count;
    
    // MLFQ scheduling information
    int queue_level;           // Current queue level (0-4)
    unsigned long long queue_time;      // Time spent in current queue level
    unsigned long long last_schedule_time; // When was this process last scheduled
    
    // For sleep functionality
    unsigned long long wakeup_time;     // When to wake up sleeping process
    
    /* Student's code ends here. */
};

unsigned long long mtime_get();

int proc_alloc();
void proc_free(int);
void proc_set_ready(int);
void proc_set_running(int);
void proc_set_runnable(int);
void proc_set_pending(int);

void mlfq_reset_level();
void mlfq_update_level(struct process* p, unsigned long long runtime);
void proc_sleep(int pid, uint usec);
void proc_coresinfo();

extern uint core_to_proc_idx[NCORES];
