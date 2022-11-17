/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "include/threads/vaddr.h"
#include "include/threads/mmu.h"
#include "include/lib/kernel/bitmap.h"


// TODO_ANAR
static struct bitmap *table_swp; // Table to distinguish between available and busy areas on swap disk
const size_t secs_for_pg = PGSIZE / DISK_SECTOR_SIZE; // Value for managing swap space in page size units

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	lock_init(&lock_bit);
	swap_disk = disk_get(1,1); // Create swap space on disk
	size_t swap_size = disk_size(swap_disk)/8UL; // Total number of swap slots required
	table_swp = bitmap_create(swap_size); // Create as many swappables as swap_size

}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	struct anon_page *anon_pg = &page->anon;
	anon_pg->slot_number = 0;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	lock_acquire(&lock_bit);
	size_t swap_slot = anon_page->slot_number;

	int cnt = 0;
	while(cnt< secs_for_pg){
		disk_read(swap_disk, (swap_slot * secs_for_pg) + cnt, page->frame->kva + (DISK_SECTOR_SIZE*cnt));
		cnt++;
	}
	bool value = false;
	ASSERT (table_swp != NULL);
	if (value)
		bitmap_mark (table_swp, swap_slot);
	else
		bitmap_reset (table_swp, swap_slot);
	lock_release(&lock_bit);
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct thread *t = page->frame->thread;

	lock_acquire(&lock_bit);
	size_t available_slt = bitmap_scan_and_flip(table_swp, 0, 1, false);
	lock_release(&lock_bit);

	int cnt = 0;
	while(cnt < secs_for_pg){
		disk_write(swap_disk, (available_slt * secs_for_pg) + cnt, page->frame->kva + (DISK_SECTOR_SIZE*cnt));
		cnt++;
	}
	anon_page->slot_number = available_slt;
	palloc_free_page(page->frame->kva);
    pml4_clear_page(t->pml4, page->va);
    page->frame = NULL;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if (page->frame == NULL){}
	else {
		lock_acquire(&lock_fr);
		list_remove(&page->frame->frame_elem);
		lock_release(&lock_fr);
		free(page->frame);
	}
	return;
}
