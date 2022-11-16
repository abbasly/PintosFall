/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
    /* Set up the handler */
    page->operations = &file_ops;   
    struct file_page *file_page = &page->file;

    file_page->cnt = 0;
    file_page->file = NULL;
    file_page->read_bytes = 0;
    file_page->zero_bytes = 0;
    file_page->ofs = 0;

    return true;
}


/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
    // printf("\n-----------file_swap_in_entry------------\n");
	struct file_page *file_page UNUSED = &page->file;
    
    if (!file_read_at(file_page->file, kva, file_page->read_bytes, file_page->ofs)){
        return false;
    }
    memset(kva + file_page->read_bytes, 0, file_page->zero_bytes);

    // printf("\n-----------file_swap_out_entry------------\n");

    return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
    // printf("\n-----------file_swap_out_entry------------\n");

	struct file_page *file_page UNUSED = &page->file;
    struct thread *t = page->frame->thread;

    if (pml4_is_dirty(t->pml4, page->va)){
        file_write_at(file_page->file, page->frame->kva, page->file.read_bytes, page->file.ofs);
        pml4_set_dirty(t->pml4, page->va, false);
    }
    // printf("\n-----------file_swap_out_mid------------\n");
    palloc_free_page(page->frame->kva);
    pml4_clear_page(t->pml4, page->va);
    page->frame = NULL;

    // printf("\n-----------file_swap_out_end------------\n");
    return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
    struct thread *t = thread_current();

    if (pml4_is_dirty(t->pml4, page->va)){
			file_write_at(file_page->file, page->frame->kva, page->file.read_bytes, page->file.ofs);
            pml4_set_dirty(t->pml4, page->va, false);
    }

    if (page->frame != NULL){
        lock_acquire(&frame_lock);
        list_remove(&page->frame->frame_elem);
        lock_release(&frame_lock);
		free(page->frame);
	}
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
        struct file *file, off_t offset) {
    
    struct file *reopen_file = file_reopen(file);

    int cnt = length % PGSIZE ? (int)(length / PGSIZE) + 1 : (int)(length / PGSIZE);

    for (int i = 0; i < cnt; i++){
        if (spt_find_page(&thread_current()->spt, addr)){
            return NULL;
        }
    }

    void* origin_addr = addr;

    for (int i = 0; i < cnt; i++){

        size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct aux_data *aux_dt = calloc(1, sizeof(struct aux_data));

        aux_dt->cnt = cnt;
        aux_dt->file = reopen_file;
        aux_dt->ofs = offset;
        aux_dt->read_bytes = page_read_bytes;
        aux_dt->zero_bytes = page_zero_bytes;


        if (!vm_alloc_page_with_initializer(VM_FILE, addr,
                                            writable, lazy_load_segment, (void *)aux_dt)){
            free(aux_dt);
            return false;
        }

        length -= page_read_bytes;
        offset += page_read_bytes;
        addr += PGSIZE;
    }

    return origin_addr;

}

/* Do the munmap */
void
do_munmap (void *addr) {
	// spt를 돌면서 동일한 파일을 갖고 있는 페이지들을 모두 프리해줌
	struct thread *t = thread_current();
	struct page *page = spt_find_page(&t->spt, addr);

	struct supplemental_page_table *src = &t->spt;

	struct file *file = page->file.file;

	int cnt = page->file.cnt;

	for (int i = 0; i < cnt; i++){

		struct page *p = spt_find_page(src, addr + (PGSIZE * i));

		if (pml4_is_dirty(t->pml4, p->va)){
			file_write_at(file, p->frame->kva, p->file.read_bytes, p->file.ofs);
            pml4_set_dirty(t->pml4, page->va, false);
		}

		hash_delete(&t->spt.pages, &p->hash_elem);
		// free(p);
	}
}
