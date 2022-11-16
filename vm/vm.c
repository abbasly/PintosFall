/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include <string.h>
#include "include/userprog/process.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_list);
	lock_init(&frame_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;
	upage = pg_round_down(upage);
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = (struct page *)calloc(1, sizeof(struct page));
		
		bool (*initializer)(struct page *, enum vm_type, void *);
		switch (VM_TYPE(type))
		{
		case VM_ANON:
			initializer = anon_initializer;
			break;
		case VM_FILE:
			initializer = file_backed_initializer;
			break;
		default:
			goto err;
		}
		uninit_new(page, upage, init, type, aux, initializer);

		/* TODO: Insert the page into the spt. */
		page->writable = writable;
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/* 지정된 supplemental page table에서 va에 해당하는 struct page를 찾는다. 실패 시 NULL을 반환한다. */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function. */
	struct hash_elem *e;

	page = (struct page *)calloc(1, sizeof(struct page));
	page->va = pg_round_down(va);

	e = hash_find(&spt->pages, &page->hash_elem);
	free(page);

	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	/* TODO: Fill this function. */

	// page->va = pg_round_down(page->va);
	if (hash_insert(&spt->pages, &page->hash_elem) == NULL)
	{
		return true;
	}
	return false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
}

/* Get the struct frame, that will be evicted. */
/* 프레임테이블을 순회하며 희생자 페이지를 고르는 함수 */
static struct frame *
vm_get_victim(void)
{
	// 희생자 페이지를 결정하는 동안, 다른 프로세스에서도 희생자 페이지를 고르면 안되기 때문에 lock을 걸어줌
	lock_acquire(&frame_lock);
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */
	// clock 알고리즘. 
	
	struct list_elem *elem;
	struct frame *frame;

	for (elem = list_begin(&frame_list); elem != list_end(&frame_list);
		 elem = list_next(elem)){
		
		frame = list_entry(elem, struct frame, frame_elem);
		
		bool access = pml4_is_accessed(frame->thread->pml4, frame->page->va);

		if (!access){ // access bit가 0이라면 당첨
			victim = frame;
			list_remove(&frame->frame_elem); // victim 변수에 담은 뒤, 프레임 테이블에서 제거
			break;
		}else{
			pml4_set_accessed(frame->thread->pml4, frame->page->va, false); // 1이라면 0으로 변환해줌
		}
		
	}
	/* 찾은 희생자 페이지가 없다면 프레임 리스트의 첫번째 녀석을 희생자 페이지로 결정 -> 모든 프레임이 0으로 세팅되었기 때문 */
	if (victim == NULL){
		victim = list_entry(list_pop_front(&frame_list), struct frame, frame_elem);
		
	}
	lock_release(&frame_lock);
	// printf("\nvm_get_victim() end %p\n", victim);
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	// printf("\nvm_evict_frame() entry\n");
	struct frame *victim UNUSED = vm_get_victim(); // 희생자 프레임 얻기
	/* TODO: swap out the victim and return the evicted frame. */
	if (swap_out(victim->page)){ // 해당 프레임의 페이즈를 swap out
		return victim;
	}
	// printf("\nvm_evict_frame() fail\n");
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	frame = (struct frame *)calloc(1, sizeof(struct frame));

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);

	void *kva = palloc_get_page(PAL_USER);

	if (kva == NULL) // 물리 메모리가 가득 찼을 경우 kva == NULL
	{
		// printf("\nvm_get_frame() handling entry\n");
		struct frame* evict_frame = vm_evict_frame(); // 메모리가 가득 찼으므로 희생자 페이지를 결정 후
		if(evict_frame){
			kva = palloc_get_page(PAL_USER); // 재 할당 요청
			free(evict_frame);
			// printf("\nvm_get_frame() handling end \n");
		}else{
			// PANIC("vm_evict_frame() = NULL");
		}
		
	}

	frame->thread = thread_current();
	frame->kva = kva;

	lock_acquire(&frame_lock);
	list_push_back(&frame_list, &frame->frame_elem);
	lock_release(&frame_lock);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
	vm_alloc_page(VM_ANON | VM_MARKER_0, addr, true);
	vm_claim_page(addr);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	if (not_present)
	{	
		page = spt_find_page(spt, addr);

		if (page == NULL)
		{
			void* rsp = (void*)user ? f->rsp : thread_current()->rsp;

			if (USER_STACK>addr && addr>=rsp-8 && addr >= USER_STACK - (1<<20))
			{
				vm_stack_growth(pg_round_down(addr));
				return true;
			}
			return false;
		}	
		return vm_do_claim_page(page);
	}
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function */
	struct thread *cur = thread_current();
	page = spt_find_page(&cur->spt, va);
	if (page == NULL)
	{
		return false;
	}
	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *cur = thread_current();

	if (!pml4_get_page(cur->pml4, page->va) && pml4_set_page(cur->pml4, page->va, frame->kva, page->writable))
	{
		return swap_in(page, frame->kva);
	}

	return false;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	hash_init(&spt->pages, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator i;
	hash_first(&i, &src->pages);
	while (hash_next(&i))
	{
		struct page *p = hash_entry(hash_cur(&i), struct page, hash_elem);

		enum vm_type type = p->operations->type;
		void *va = p->va;
		bool writable = p->writable;
		struct aux_data *aux_dt = calloc(1, sizeof(struct aux_data));
		switch (VM_TYPE(type))
		{
		case VM_UNINIT:
			memcpy(aux_dt, p->uninit.aux, sizeof(struct aux_data));
			if (!vm_alloc_page_with_initializer(p->uninit.type, va, writable, p->uninit.init, aux_dt))
			{
				free(aux_dt);
				return false;
			}
			break;
		case VM_ANON:
		case VM_FILE:
			free(aux_dt);
			if (!(vm_alloc_page(type, va, writable) && vm_claim_page(va)))
			{
				return false;
			}
			struct page *page = spt_find_page(dst, va);
			memcpy(page->frame->kva, p->frame->kva, PGSIZE);
			break;
		default:
			PANIC("SPT COPY PANIC!\n");
			break;
		}
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	// pml4 is dirty / set dirty  활용?
	hash_destroy(&spt->pages, page_destructor);
}

/* Returns a hash value for page p. */
unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool page_less(const struct hash_elem *a_,
			   const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}

void page_destructor(struct hash_elem *elem, void *aux)
{
	struct page *p = hash_entry(elem, struct page, hash_elem);
	vm_dealloc_page(p);
}