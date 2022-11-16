#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;

struct lock bitmap_lock;

struct anon_page {
    enum vm_type type;
    int slot_number;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
