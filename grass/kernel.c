/*
 * (C) 2025, Cornell University
 * All rights reserved.
 *
 * Description: kernel ≈ 2 handlers
 *   intr_entry() handles timer and device interrupts.
 *   excp_entry() handles system calls and faults (e.g., invalid memory access).
 */

#include "process.h"
#include <string.h>

uint core_in_kernel;
uint core_to_proc_idx[NCORES];
struct process proc_set[MAX_NPROCESS + 1];
/* proc_set[0] is a place holder for idle cores. */

#define curr_proc_idx core_to_proc_idx[core_in_kernel]
#define curr_pid      proc_set[curr_proc_idx].pid
#define curr_status   proc_set[curr_proc_idx].status
#define curr_saved    proc_set[curr_proc_idx].saved_registers

static void intr_entry(uint);
static void excp_entry(uint);

void kernel_entry() {
    /* With the kernel lock, only one core can enter this point at any time. */
    asm("csrr %0, mhartid" : "=r"(core_in_kernel));

    /* Save the process context. */
    asm("csrr %0, mepc" : "=r"(proc_set[curr_proc_idx].mepc));
    memcpy(curr_saved, SAVED_REGISTER_ADDR, SAVED_REGISTER_SIZE);

    uint mcause;
    asm("csrr %0, mcause" : "=r"(mcause));
    (mcause & (1 << 31)) ? intr_entry(mcause & 0x3FF) : excp_entry(mcause);

    /* Restore the process context. */
    asm("csrw mepc, %0" ::"r"(proc_set[curr_proc_idx].mepc));
    memcpy(SAVED_REGISTER_ADDR, curr_saved, SAVED_REGISTER_SIZE);
}

#define INTR_ID_TIMER   7
#define EXCP_ID_ECALL_U 8
#define EXCP_ID_ECALL_M 11
static void proc_yield();
static void proc_try_syscall(struct process* proc);

static void excp_entry(uint id) {
    if (id >= EXCP_ID_ECALL_U && id <= EXCP_ID_ECALL_M) {
        /* Copy the system call arguments from user space to the kernel. */
        uint syscall_paddr = earth->mmu_translate(curr_pid, SYSCALL_ARG);
        memcpy(&proc_set[curr_proc_idx].syscall, (void*)syscall_paddr,
               sizeof(struct syscall));
        proc_set[curr_proc_idx].syscall.status = PENDING;

        proc_set_pending(curr_pid);
        proc_set[curr_proc_idx].mepc += 4;
        proc_try_syscall(&proc_set[curr_proc_idx]);
        proc_yield();
        return;
    }
    /* Student's code goes here (System Call & Protection | Virtual Memory). */

    /* Kill the current process if curr_pid is a user application. */
    if (curr_pid >= GPID_USER_START) {
        printf("Process %d killed due to exception %d\n", curr_pid, id);
        proc_free(curr_pid);
        proc_yield();
        return;
    }

    /* Student's code ends here. */
    FATAL("excp_entry: kernel got exception %d", id);
}

static void intr_entry(uint id) {
    if (id != INTR_ID_TIMER) FATAL("excp_entry: kernel got interrupt %d", id);
    /* Student's code goes here (Preemptive Scheduler). */

    /* Update the process lifecycle statistics. */
    if (curr_proc_idx > 0 && curr_proc_idx <= MAX_NPROCESS) {
        struct process* curr_proc = &proc_set[curr_proc_idx];
        curr_proc->timer_interrupt_count++;
        
        // Update CPU time for current process
        ulonglong current_time = mtime_get();
        if (curr_proc->last_schedule_time > 0) {
            ulonglong runtime = current_time - curr_proc->last_schedule_time;
            curr_proc->total_cpu_time += runtime;  // THIS LINE SHOULD WORK
            
            // Update MLFQ level based on runtime
            mlfq_update_level(curr_proc, runtime);
        }
        
        // Update last_schedule_time for next calculation
        curr_proc->last_schedule_time = current_time;  // IMPORTANT!
    }

    /* Student's code ends here. */
    proc_yield();
}

static void proc_yield() {
    if (curr_status == PROC_RUNNING) proc_set_runnable(curr_pid);

    /* Student's code goes here (Multiple Projects). */

    /* [Preemptive Scheduler]
     * Measure and record lifecycle statistics for the *current* process.
     * Modify the loop below to find the next process to schedule with MLFQ.
     * [System Call & Protection]
     * Do not schedule a process that should still be sleeping at this time. */

    // Call MLFQ reset (Rule 5)
    mlfq_reset_level();

    int next_idx = MAX_NPROCESS;
    int min_level = MLFQ_LEVELS; // Start with worst possible level
    ulonglong current_time = mtime_get();

    // MLFQ Scheduler: Find runnable process with lowest level number (Rule 1)
    for (uint i = 1; i <= MAX_NPROCESS; i++) {
        struct process* p = &proc_set[i];
        
        // Check if process should wake up from sleep
        if (p->status == PROC_PENDING_SYSCALL && p->wakeup_time > 0 && 
            current_time >= p->wakeup_time) {
            p->wakeup_time = 0;
            p->status = PROC_RUNNABLE;
        }

        if (p->status == PROC_PENDING_SYSCALL) proc_try_syscall(p);

        // Skip processes that are sleeping
        if (p->wakeup_time > 0 && current_time < p->wakeup_time) {
            continue;
        }

        // MLFQ: Select process with lowest queue level (highest priority)
        if ((p->status == PROC_READY || p->status == PROC_RUNNABLE) && 
            p->queue_level < min_level) {
            min_level = p->queue_level;
            next_idx = i;
        }
    }

    // If no high priority process found, find any runnable process
    if (next_idx == MAX_NPROCESS) {
        for (uint i = 1; i <= MAX_NPROCESS; i++) {
            struct process* p = &proc_set[i];
            
            if (p->status == PROC_PENDING_SYSCALL) proc_try_syscall(p);

            // Skip sleeping processes
            if (p->wakeup_time > 0 && current_time < p->wakeup_time) {
                continue;
            }

            if (p->status == PROC_READY || p->status == PROC_RUNNABLE) {
                next_idx = i;
                break;
            }
        }
    }

    if (next_idx < MAX_NPROCESS) {
        /* [Preemptive Scheduler]
         * Measure and record lifecycle statistics for the *next* process.
         * [System Call & Protection | Multicore & Locks]
         * Modify mstatus.MPP to enter machine or user mode after mret. */
        
        struct process* next_proc = &proc_set[next_idx];
        
        // Update lifecycle statistics for next process
        if (next_proc->first_schedule_time == 0) {
            next_proc->first_schedule_time = mtime_get();
        }
        next_proc->last_schedule_time = mtime_get();

        // Set mstatus.MPP to user mode for user processes
        if (next_proc->pid >= GPID_USER_START) {
            asm("csrr t0, mstatus");
            asm("li t1, ~(3 << 11)");  // Clear MPP bits
            asm("and t0, t0, t1");
            asm("li t1, (0 << 11)");   // Set MPP to user mode (0)
            asm("or t0, t0, t1");
            asm("csrw mstatus, t0");
        } else {
            // Set mstatus.MPP to machine mode for kernel processes
            asm("csrr t0, mstatus");
            asm("li t1, ~(3 << 11)");  // Clear MPP bits
            asm("and t0, t0, t1");
            asm("li t1, (3 << 11)");   // Set MPP to machine mode (3)
            asm("or t0, t0, t1");
            asm("csrw mstatus, t0");
        }

    } else {
        /* [Multicore & Locks]
         * Release the kernel lock.
         * [Multicore & Locks | System Call & Protection]
         * Set curr_proc_idx to 0; Reset the timer;
         * Enable interrupts by setting the mstatus.MIE bit to 1;
         * Wait for the next interrupt using the wfi instruction. */

        // No process to run, become idle
        curr_proc_idx = 0;
        earth->timer_reset(core_in_kernel);
        
        // Enable interrupts
        asm("csrr t0, mstatus");
        asm("ori t0, t0, 0x8");  // Set MIE bit
        asm("csrw mstatus, t0");
        
        // Wait for interrupt
        asm("wfi");
        return;

        // FATAL("proc_yield: no process to run on core %d", core_in_kernel);
    }
    /* Student's code ends here. */

    curr_proc_idx = next_idx;
    earth->mmu_switch(curr_pid);
    earth->mmu_flush_cache();
    if (curr_status == PROC_READY) {
        /* Setup argc, argv and program counter for a newly created process. */
        curr_saved[0]                = APPS_ARG;
        curr_saved[1]                = APPS_ARG + 4;
        proc_set[curr_proc_idx].mepc = APPS_ENTRY;
    }
    proc_set_running(curr_pid);
    earth->timer_reset(core_in_kernel);
}

static void proc_try_send(struct process* sender) {
    for (uint i = 0; i < MAX_NPROCESS; i++) {
        struct process* dst = &proc_set[i];
        if (dst->pid == sender->syscall.receiver &&
            dst->status != PROC_UNUSED) {
            /* Return if dst is not receiving or not taking msg from sender. */
            if (!(dst->syscall.type == SYS_RECV &&
                  dst->syscall.status == PENDING))
                return;
            if (!(dst->syscall.sender == GPID_ALL ||
                  dst->syscall.sender == sender->pid))
                return;

            dst->syscall.status = DONE;
            dst->syscall.sender = sender->pid;
            /* Copy the system call arguments within the kernel PCB. */
            memcpy(dst->syscall.content, sender->syscall.content,
                   SYSCALL_MSG_LEN);
            return;
        }
    }
    FATAL("proc_try_send: unknown receiver pid=%d", sender->syscall.receiver);
}

static void proc_try_recv(struct process* receiver) {
    if (receiver->syscall.status == PENDING) return;

    /* Copy the system call struct from the kernel back to user space. */
    uint syscall_paddr = earth->mmu_translate(receiver->pid, SYSCALL_ARG);
    memcpy((void*)syscall_paddr, &receiver->syscall, sizeof(struct syscall));

    /* Set the receiver and sender back to RUNNABLE. */
    proc_set_runnable(receiver->pid);
    proc_set_runnable(receiver->syscall.sender);
}

static void proc_try_syscall(struct process* proc) {
    switch (proc->syscall.type) {
    case SYS_RECV:
        proc_try_recv(proc);
        break;
    case SYS_SEND:
        proc_try_send(proc);
        break;
    default:
        FATAL("proc_try_syscall: unknown syscall type=%d", proc->syscall.type);
    }
}