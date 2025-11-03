/* SPDX-License-Identifier: MIT */

/* StACSOS - Kernel
 *
 * Copyright (c) University of St Andrews 2024, 2025
 * Tom Spink <tcs6@st-andrews.ac.uk>
 */
#include <stacsos/kernel/debug.h>
#include <stacsos/kernel/mem/page-allocator-buddy.h>
#include <stacsos/kernel/mem/page.h>
#include <stacsos/memops.h>

using namespace stacsos;
using namespace stacsos::kernel;
using namespace stacsos::kernel::mem;

// Represents the contents of a free page, that can hold useful metadata.
struct page_metadata {
	page *next_free;
};

/**
 * @brief Dumps out (via the debugging routines) the current state of the buddy page allocator's free lists
 */
void page_allocator_buddy::dump() const
{
	// Print out a header, so we can quickly identify this output in the debug stream.
	dprintf("*** buddy page allocator - free list ***\n");

	// Loop over each order that our allocator is responsible for, from zero up to *and
	// including* LastOrder.
	for (int order = 0; order <= LastOrder; order++) {
		// Print out the order number (with a leading zero, so that it's nicely aligned)
		dprintf("[%02u] ", order);

		// Get the pointer to the first free page in the free list.
		page *current_free_page = free_list_[order];

		// While there /is/ currently a free page in the list...
		while (current_free_page) {
			// Print out the extents of this page, i.e. its base address (at byte granularity), up to and including the last
			// valid address.  Remember: these are PHYSICAL addresses.
			dprintf("%lx--%lx ", current_free_page->base_address(), (current_free_page->base_address() + ((1 << order) << PAGE_BITS)) - 1);

			// Advance to the next page, by interpreting the free page as holding metadata, and reading
			// the appropriate field.
			current_free_page = ((page_metadata *)current_free_page->base_address_ptr())->next_free;
		}

		// New line for the next order.
		dprintf("\n");
	}
}

/**
 * @brief Inserts pages that are known to be free into the buddy allocator.
 *
 * ** You are required to implement this function **
 *
 * @param range_start The first page in the range.
 * @param page_count The number of pages in the range.
 */
void page_allocator_buddy::insert_free_pages(page &range_start, u64 page_count) {
	page* cur = &range_start;
	u64 remaining = page_count; 

	while (remaining > 0) {
		// find largest order to which cur is aligned and block fits within remaining pages
		int order = LastOrder;
		while (order > 0 && (!block_aligned(order, cur->pfn()) || pages_per_block(order) > remaining)) {
			order--;
		}

		// call free pages method to insert and also coalesce upwards
		// also takes cares of tracking total_free
		free_pages(*cur, order);

		// move by block size
		const u64 step = pages_per_block(order);
		cur += step;
		remaining -= step;
	}
}

/**
 * @brief
 *
 * @param order
 * @param block_start
 */
void page_allocator_buddy::insert_free_block(int order, page &block_start)
{
	// assert order in range
	assert(order >= 0 && order <= LastOrder);

	// assert block_start aligned to order
	assert(block_aligned(order, block_start.pfn()));

	page *target = &block_start;
	page **slot = &free_list_[order];
	while (*slot && *slot < target) {
		// slot = &((*slot)->next_free_);
		slot = &((page_metadata *)((*slot)->base_address_ptr()))->next_free;
	}

	assert(*slot != target);

	((page_metadata *)target->base_address_ptr())->next_free = *slot;
	*slot = target;
}

/**
 * @brief
 *
 * @param order
 * @param block_start
 */
void page_allocator_buddy::remove_free_block(int order, page &block_start)
{
	// assert order in range
	assert(order >= 0 && order <= LastOrder);

	// assert block_start aligned to order
	assert(block_aligned(order, block_start.pfn()));

	page *target = &block_start;
	page **candidate_slot = &free_list_[order];
	while (*candidate_slot && *candidate_slot != target) {
		candidate_slot = &((page_metadata *)(*candidate_slot)->base_address_ptr())->next_free; // &((*candidate_slot)->next_free_);
	}

	// assert candidate block exists
	assert(*candidate_slot == target);

	*candidate_slot = ((page_metadata *)target->base_address_ptr())->next_free;
	((page_metadata *)target->base_address_ptr())->next_free = nullptr;

	// target->next_free_ = nullptr;
}

/**
 * @brief
 *
 * ** You are required to implement this function **
 * @param order
 * @param block_start
 */
void page_allocator_buddy::split_block(int order, page &block_start) {
	// check positive order and alignment
	assert(order > 0);
	assert(block_aligned(order, block_start.pfn()));

	// remove parent block
	remove_free_block(order, block_start);

	// separate into 2
	int lower = order - 1;
	u64 half = pages_per_block(lower);
	const u64 left_pfn  = block_start.pfn();
	const u64 right_pfn = left_pfn + half;
	page &left  = page::get_from_pfn(left_pfn);
	page &right = page::get_from_pfn(right_pfn);

	// insert both halves into lower order free list
	insert_free_block(lower, left);
	insert_free_block(lower, right);
}

/**
 * @brief
 *
 * @param order
 * @param buddy
 */
void page_allocator_buddy::merge_buddies(int order, page &buddy) {
	// check order is in range and alignment
	assert(order >= 0 && order <= LastOrder);
	assert(block_aligned(order, buddy.pfn()));

	u64 base = pages_per_block(order);
	u64 pfn  = buddy.pfn();
	u64 other_pfn = pfn ^ base;

	// find other buddy descriptor
	page &other  = page::get_from_pfn(other_pfn);

	// remove both buddies from their order's free list
	remove_free_block(order, buddy);
	remove_free_block(order, other);

	// calculate merged block start
	u64 merged_base_pfn = pfn & ~((u64)base);
	page &merged = page::get_from_pfn(merged_base_pfn);

	// insert merged block into next order free list
	insert_free_block(order + 1, merged);
}

/**
 * @brief
 *
 * ** You are required to implement this function **
 * @param order
 * @param flags
 * @return page*
 */
page *page_allocator_buddy::allocate_pages(int order, page_allocation_flags flags) {
	// check order in range
	if (order < 0 || order > LastOrder) {
		return nullptr;
	}

	// find the first non-empty free list at or above order
	for (int o = order; o <= LastOrder; ++o) {
		if (free_list_[o]) {
			// take first free block
			page *block = free_list_[o];
			remove_free_block(o, *block);
			u64 base_pfn = block->pfn();

			// if it's too big, split down until we reach order
			int cur = o;
			while (cur > order) {
				// split into order - 1
				--cur;
            	u64 right_pfn = base_pfn + pages_per_block(cur);
            	page &right = page::get_from_pfn(right_pfn);
            	insert_free_block(cur, right);
            	// keep left (base_pfn) for next iteration
			}

			// track free pages
			total_free_ -= pages_per_block(order);

			// if flags given, zero fill
			page &ret = page::get_from_pfn(base_pfn);
			if ((flags & page_allocation_flags::zero) == page_allocation_flags::zero) {
				memops::pzero(ret.base_address_ptr(), pages_per_block(order));
			}
			return &ret;
		}
	}
	// not enough memory for given order
	return nullptr;
}

// helper method
bool page_allocator_buddy::is_in_free_list(int order, page *p) const {
	// check order in range
	assert(order >= 0 && order <= LastOrder);

	// go through free list of given order to check if given page is present
    page *cur = free_list_[order];
    while (cur) {
        if (cur == p) {
			return true;
		} 
		// move pointer
        cur = ((page_metadata*)cur->base_address_ptr())->next_free;
    }
    return false;
}

/**
 * @brief
 *
 * ** You are required to implement this function **
 * @param block_start
 * @param order
 */
void page_allocator_buddy::free_pages(page &block_start, int order) {
	// check order is in range and alignment
	assert(order >= 0 && order <= LastOrder);
	assert(block_aligned(order, block_start.pfn()));

	// insert freed block into its order free list and track
	insert_free_block(order, block_start);
	total_free_ += pages_per_block(order);

	// merge upwards while buddy is also free
	int cur_order = order;
	page *cur_block = &block_start;

	while (cur_order < LastOrder) {
		u64 base = pages_per_block(cur_order);
		u64 pfn  = cur_block->pfn();
		u64 buddy_pfn = pfn ^ base;

		page *buddy = &page::get_from_pfn(buddy_pfn);

		// if buddy isn't free, stop merging
		if (!is_in_free_list(cur_order, buddy)) {
			break;
		}

		// if free, merge buddy blocks
		// this removes them from cur_order free list and inserts merged into cur_order + 1 free list
		merge_buddies(cur_order, *cur_block);

		// calculate merged pfn and repeat from there
		u64 merged_base_pfn = pfn & ~((u64)base);
		page *merged = &page::get_from_pfn(merged_base_pfn);

		cur_block = merged;
		cur_order++;
	}
}
