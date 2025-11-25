#include <stacsos/console.h>
#include <stacsos/memops.h>
#include <stacsos/user-syscall.h>

using namespace stacsos;

// types of sorting of directory entries
enum class sort_mode {
	none, // no sorting (random order)
	name, // sort by name (alphabetically)
	size // sort by file size (smallest to largest)
};

// if error occurs, print accepted usage
static void print_usage() {
	console::get().write("error: usage: ls [-l or -n or -s or -ln or -ls] <path>\n");
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

	// tracker for sorting mode (default none)
	sort_mode mode = sort_mode::none;

	// skip any leading spaces
	while (*cmdline == ' ') {
		cmdline++;
	}

	// parse flags, allowed:
	// "-l" long mode
	// "-n" sort by name
	// "-s" sort by size
	// "-ln" and "-ls" combinations accepted 
	if (*cmdline == '-') {
		cmdline++;
		
		char flag1 = 0;
		char flag2 = 0;

		// read only max 2 flags
		while(*cmdline && *cmdline != ' ') {
			char flag = *cmdline++;

			if (flag1 == 0) {
				flag1 = flag;
			} else if (flag2 == 0) {
				flag2 = flag;
			} else { // if more than 2 flag chars given, print accepted usage
				print_usage();
				return 1;
			}
		}

		// interpret flags to check if combo is valid
		if (flag1 == 'l') {
			long_mode = true;
			if (flag2 == 0) {
				// just -l
			} else if (flag2 == 'n') { // -ln
				mode = sort_mode::name;
			} else if (flag2 == 's') { // -ls
				mode = sort_mode::size;
			} else { // invalid combo after -l
				print_usage();
				return 1;
			}
		} else if (flag1 == 'n' && flag2 == 0) { // -n
			mode = sort_mode::name;
		} else if (flag1 == 's' && flag2 == 0) { // -s
			mode = sort_mode::size;
		} else {
			// invalid combo
			print_usage();
			return 1;
		}
	}

	// skip spaces before given file path
	while (*cmdline == ' ') {
		cmdline++;
	}

    // if no path given, print accepted usage
	if (*cmdline == '\0') {
		print_usage();
		return 1;
	}

    // extracted path from args
	const char *path = cmdline;

	// OPTIMISATION: 2 PHASE API

	// first call to get how many entries are there from kernel
	auto res1 = syscalls::listdir(path, nullptr, 0);

    // analyse possible error results
	if (res1.code == syscall_result_code::not_found) {
		console::get().writef("error: path '%s' not found\n", path);
		return 1;
	} else if (res1.code == syscall_result_code::not_supported) {
		console::get().writef("error: path '%s' is not a directory\n", path);
		return 1;
	} else if (res1.code != syscall_result_code::ok) {
		console::get().writef("error: listdir failed for '%s'\n", path);
		return 1;
	}

	// get number of entries
	u64 count = res1.length;

	// knowing number of entries, only have to allocate exactly enough 
	// space for all directory entries in user memory
	// (instead of having static limit)
	u64 bytes = count * sizeof(directory_entry);
	auto alloc = syscalls::alloc_mem(bytes);

	// check allocation successful
	if (alloc.code != syscall_result_code::ok || alloc.ptr == nullptr) {
		console::get().write("error: memory allocation for directory entries failed\n");
		return 1;
	}

	// interpret pointer to the allocated array of directory entry structs
	auto *entries = reinterpret_cast<directory_entry *>(alloc.ptr);

	// second call to actually get the directory entries into the buffer
	auto res2 = syscalls::listdir(path, entries, count);

	// check call sussessful
	if (res2.code != syscall_result_code::ok) {
		console::get().writef("error: listdir failed for '%s'\n", path);
		return 1;
	}

	// number of entries actually written
	// if works correctly should be equal to count
	u64 actual = res2.length;

	// calculate the biggest name length for proper allignment in output
	u64 max_name_len = 0;
	if (long_mode) {
		for (u64 i = 0; i < actual; i++) {
			auto &e = entries[i];

			u64 name_len = memops::strlen(e.name);
			if (name_len > max_name_len) {
				max_name_len = name_len;
			}
		}
	}

	// sort by name or size if flags given
	// using selection sort O(n^2)
	if (mode == sort_mode::name) {
		for (u64 i = 0; i + 1 < actual; i++) {
			for (u64 j = i + 1; j < actual; j++) {
				if (memops::strcmp(entries[i].name, entries[j].name) > 0) {
					directory_entry tmp = entries[i];
					entries[i] = entries[j];
					entries[j] = tmp;
				}
			}
		}
	} else if (mode == sort_mode::size) {
		for (u64 i = 0; i + 1 < actual; i++) {
			for (u64 j = i + 1; j < actual; j++) {
				if (entries[i].size > entries[j].size) { // smallest to biggest
					directory_entry tmp = entries[i];
					entries[i] = entries[j];
					entries[j] = tmp;
				}
			}
		}
	}

    // iterate over list of entries, printing each with appropriate info (depending on flag)
	for (u64 i = 0; i < actual; i++) {
		auto &e = entries[i];

		// skip entries "." and ".." (current and parent dirs)
		if (memops::strcmp(e.name, ".") == 0 || memops::strcmp(e.name, "..") == 0) {
			continue;
		}

		// if "-l" isn't given, just print file names
		if (!long_mode) {
            console::get().writef("%s\n", e.name);
		} else { // if it is, print file type, name and size nicely alligned
			char type_char = (e.type == 1) ? 'D' : 'F'; // as type is defined as 0 for file and 1 for directory
			
			// first just write type and name
			console::get().writef("[%c] %s", type_char, e.name);

			if (e.type == 1) { // if dir, no size
				console::get().write("\n");
			} else { // if file, first pad with whitespaces, then print size
				// calculate number of spaces needed 
				u64 name_len = memops::strlen(e.name);
				u64 padding = 5; // min distance of 5 spaces

				// padding is difference between max length and this name's length
				if (max_name_len > name_len) {
					padding += (max_name_len - name_len);
				}

				// print calculated number of spaces
				for (u64 p = 0; p < padding; p++) {
					console::get().write(" ");
				}

				// print alligned size
				console::get().writef("%lu\n", e.size);
			}
		}
	}
	return 0;
}
