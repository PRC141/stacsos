/* SPDX-License-Identifier: MIT */

/* StACSOS - Utility Library
 *
 * Copyright (c) University of St Andrews 2024
 * Tom Spink <tcs6@st-andrews.ac.uk>
 */
#pragma once

namespace stacsos {
enum class syscall_result_code : u64 { ok = 0, not_found = 1, not_supported = 2 };

enum class syscall_numbers {
	exit = 0,
	open = 1,
	close = 2,
	read = 3,
	pread = 4,
	write = 5,
	pwrite = 6,
	set_fs = 7,
	set_gs = 8,
	alloc_mem = 9,
	start_process = 10,
	wait_for_process = 11,
	start_thread = 12,
	stop_current_thread = 13,
	join_thread = 14,
	sleep = 15,
	poweroff = 16,
	ioctl = 17,
	listdir = 18 // P3: new system call for listing directories
};

struct syscall_result {
	syscall_result_code code;
	u64 data;
} __packed;

// P3: new struct to store properties of a directory/file, 
// a list of which will be returned by the listdir system call
struct directory_entry {
	char name[64]; // file name: string of fixed size (name will be truncated if longer than 63 chars), ensured null-termination
	u64 size; // file size: in bytes of files, can be 0 for directories
	u8 type; // type of file: 0 for file or 1 for directory
} __packed;
} // namespace stacsos
