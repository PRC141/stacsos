/* SPDX-License-Identifier: MIT */

/* StACSOS - Kernel
 *
 * Copyright (c) University of St Andrews 2024
 * Tom Spink <tcs6@st-andrews.ac.uk>
 */
#include <stacsos/kernel/sched/alg/rr.h>

// *** COURSEWORK NOTE *** //
// This will be where you are implementing your round-robin scheduling algorithm.
// Please edit this file in any way you see fit.  You may also edit the rr.h
// header file.

using namespace stacsos::kernel::sched;
using namespace stacsos::kernel::sched::alg;

void round_robin::add_to_runqueue(tcb &tcb) {
    // append given TCB to end of list
    runqueue_.append(&tcb);
}

void round_robin::remove_from_runqueue(tcb &tcb) { 
    // optimisation: check if list is empty, so nothing to remove
    // returns to prevent crash
    if (runqueue_.empty()) {
        return;
    }

    // remove given TCB from list
    runqueue_.remove(&tcb);
}

tcb *round_robin::select_next_task(tcb *current) { 
    // optimisation: check if list is empty, so cannot select TCB, return null
    if (runqueue_.empty()) {
		return nullptr;
	}

    // optimisation: check if only one TCB in list, just return it
    if (runqueue_.count() == 1) {
        return runqueue_.first();
    }

    // take first TCB in list, put it to the back and return it (rr algorithm)
    return runqueue_.rotate();
}