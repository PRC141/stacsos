#include <stacsos/console.h>
#include <stacsos/memops.h>
#include <stacsos/user-syscall.h>

using namespace stacsos;

// if error occurs, print accepted usage
static void print_usage() {
	console::get().write("error: usage: ls [-l] <path>\n");
}

// main method to return the list of directories
int main(const char *cmdline) {
    // if no command lines arguments were provided, print accepted usage
	if (!cmdline || memops::strlen(cmdline) == 0) {
		print_usage();
		return 1;
	}

    // tracker for if its necessary to print the extra file info (size and type)
	bool long_mode = false;

	// skip any leading spaces
	while (*cmdline == ' ') {
		++cmdline;
	}

	// check for optional "-l" flag
	if (*cmdline == '-') {
		++cmdline;
		if (*cmdline == 'l') {
			long_mode = true; // switch tracker to true
			++cmdline;
		} else { // if invalid flag, print accepted usage
			print_usage();
			return 1;
		}
	}

	// skip spaces before given file path
	while (*cmdline == ' ') {
		++cmdline;
	}

    // if no path given, print accepted usage
	if (*cmdline == '\0') {
		print_usage();
		return 1;
	}

    // extracted path from args
	const char *path = cmdline;

	// set limit on number of directory entries to ensure buffer isn't overflowed
	const u64 max_entries = 128;

	// initialise list to store the directory entry structs
	directory_entry entries[max_entries];

    // make the system call
	auto res = syscalls::listdir(path, entries, max_entries);

    // analyse possible error results
	if (res.code == syscall_result_code::not_found) {
		console::get().writef("error: path '%s' not found\n", path);
		return 1;
	} else if (res.code == syscall_result_code::not_supported) {
		console::get().writef("error: path '%s' is not a directory\n", path);
		return 1;
	} else if (res.code != syscall_result_code::ok) {
		console::get().writef("error: listdir failed for '%s'\n", path);
		return 1;
	}

	// get number of entries
	u64 count = res.length;

    // iterate over list of entries, printing each with appropriate info (depending on flag)
	for (u64 i = 0; i < count; ++i) {
		auto &e = entries[i];

		// skip entries "." and ".." (current and parent dirs)
		if (memops::strcmp(e.name, ".") == 0 || memops::strcmp(e.name, "..") == 0) {
			continue;
		}

		// if "-l" isn't given, just print file names
		if (!long_mode) {
            console::get().writef("%s\n", e.name);
		} else { // if it is, print file type, name and size
			char type_char = (e.type == 1) ? 'D' : 'F'; // as type is defined as 0 for file and 1 for directory
			console::get().writef("[%c] %s %lu\n", type_char, e.name, e.size);
		}
	}
	return 0;
}
