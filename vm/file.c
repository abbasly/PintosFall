/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"

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
    file_page->zero_bytes = 0;
    file_page->ofs = 0;    file_page->cnt = 0;
    file_page->file = NULL;
    file_page->read_bytes = 0;
    return true;
}


static bool check_conds(struct file_page *file_page, void *kva){
    return !file_read_at(file_page->file, kva, file_page->read_bytes, file_page->ofs);
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
    
    if (!check_conds(file_page, kva)){}
    else { return false;}
    memset(kva + file_page->read_bytes, 0, file_page->zero_bytes);
    return true;
}

void set_dirty(struct thread *t, struct file_page *file_page, struct page *page, bool value){
    if (!pml4_is_dirty(t->pml4, page->va)){}
    else {
        file_write_at(file_page->file, page->frame->kva, page->file.read_bytes, page->file.ofs);
        pml4_set_dirty(t->pml4, page->va, false);
    }
}
/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
    struct thread *t = page->frame->thread;

    set_dirty(t, file_page, page, false);
    palloc_free_page(page->frame->kva);
    pml4_clear_page(t->pml4, page->va);
    page->frame = NULL;
    return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
    struct thread *t = thread_current();

    set_dirty(t, file_page, page, false);

    if (page->frame == NULL){}
    else {
        lock_acquire(&lock_fr);
        list_remove(&page->frame->frame_elem);
        lock_release(&lock_fr);
		free(page->frame);
	}
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
        struct file *file, off_t offset) {
            
    int cnt;
    int i = 0;
    int j = 0;
    struct file *file_ropen = file_reopen(file);

    
    if(length % PGSIZE){
        cnt = (int)(length / PGSIZE) + 1;
    } else {
        cnt = (int)(length / PGSIZE);
    }

    while(i < cnt){
        if (spt_find_page(&thread_current()->spt, addr)){
            return NULL;
        }
        i++;
    }

    void* addr_init = addr;

    while(j < cnt){
        size_t pg_bytes_read;
        if(length < PGSIZE){
            pg_bytes_read = length;
        } else{
            pg_bytes_read = PGSIZE;
        }
        size_t pg_bytes_zero = PGSIZE - pg_bytes_read;

        struct container *cont = calloc(1, sizeof(struct container));

        cont->cnt = cnt;
        cont->zero_bytes = pg_bytes_zero;
        cont->read_bytes = pg_bytes_read;
        cont->file = file_ropen;
        cont->ofs = offset;


        if (vm_alloc_page_with_initializer(VM_FILE, addr,
                                            writable, lazy_load_segment, (void *)cont)){}
        else{
            free(cont);
            return false;
        }

        offset += pg_bytes_read;
        length -= pg_bytes_read;
        addr += PGSIZE;
        j++;
    }

    return addr_init;

}

/* Do the munmap */
void
do_munmap (void *addr) {
	// Free all pages that have the same file while circling the spt
	struct thread *t = thread_current();
	struct page *pg = spt_find_page(&t->spt, addr);
    struct file *file = pg->file.file;
	struct supplemental_page_table *src = &t->spt;
    int i = 0;
	int count = pg->file.cnt;

    while(i < count){
        struct page *pgg = spt_find_page(src, addr + (PGSIZE * i));
		if (!pml4_is_dirty(t->pml4, pgg->va)){}
        else {
			file_write_at(file, pgg->frame->kva, pgg->file.read_bytes, pgg->file.ofs);
            pml4_set_dirty(t->pml4, pg->va, false);
		}
		hash_delete(&t->spt.pages, &pgg->hash_elem);
        i++;
    }
}
