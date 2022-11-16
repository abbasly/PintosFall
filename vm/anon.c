/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "include/threads/vaddr.h"
#include "include/threads/mmu.h"
#include "include/lib/kernel/bitmap.h"


static struct bitmap *swap_table; // swap disk에서 사용 가능한 영역과 사용중인 영역을 구분하기 위한 테이블
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE; // 스왑 영역을 페이지 사이즈 단위로 관리하기 위한 값

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
	/* 필요에 따라 수정 가능한 함수 - 아직까지 수정 안 함. */
	/* TODO: Set up the swap_disk. */
	lock_init(&bitmap_lock);
	swap_disk = disk_get(1,1); // 디스크에 스왑 공간을 생성
	size_t swap_size = disk_size(swap_disk)/SECTORS_PER_PAGE; // 필요한 스왑슬롯의 총 갯수
	swap_table = bitmap_create(swap_size); // swap_size만큼 swaptable을 생성
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	/* 필요에 따라 수정 가능한 함수 - 일단 true를 반환하도록 수정함. */
	struct anon_page *anon_page = &page->anon;
	// anon_page->type = type;
	anon_page->slot_number = 0;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	// printf("\n-----------swap_in_entry------------\n");
	struct anon_page *anon_page = &page->anon;
	lock_acquire(&bitmap_lock);
	size_t swap_slot = anon_page->slot_number;

	for(int i = 0; i < SECTORS_PER_PAGE; i++){
		disk_read(swap_disk, (swap_slot * SECTORS_PER_PAGE) + i, page->frame->kva + (DISK_SECTOR_SIZE*i));
	}

	bitmap_set(swap_table, swap_slot, false);
	lock_release(&bitmap_lock);
	// printf("\n-----------swap_in_end------------\n");
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	// printf("\n-----------swap_out_entry------------\n");
	struct anon_page *anon_page = &page->anon;
	struct thread *t = page->frame->thread;

	/* 비트맵을 처음부터 순회해 false 값을 가진 비트를 하나 찾는다.
	   즉, 페이지를 할당받을 수 있는 swap slot을 하나 찾는다. */
	lock_acquire(&bitmap_lock);
	size_t empty_slot = bitmap_scan_and_flip(swap_table, 0, 1, false);
	lock_release(&bitmap_lock);
	// printf("\nbitmap size:%d\n", bitmap_size(&swap_table));
	// printf("\n-----------swap_out_mid1 empty_slot:%d------------\n",(long)empty_slot);

	for(int i = 0; i < SECTORS_PER_PAGE; i++){
		disk_write(swap_disk, (empty_slot * SECTORS_PER_PAGE) + i, page->frame->kva + (DISK_SECTOR_SIZE*i));
	}

	// printf("\n-----------swap_out_mid2------------\n");
	anon_page->slot_number = empty_slot;
	palloc_free_page(page->frame->kva);
    pml4_clear_page(t->pml4, page->va);
    page->frame = NULL;

	// printf("\n-----------swap_out_end------------\n");

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if (page->frame != NULL){
		lock_acquire(&frame_lock);
		list_remove(&page->frame->frame_elem);
		lock_release(&frame_lock);
		
		free(page->frame);
	}
	return;
}
