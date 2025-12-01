/*
* Course: CS 416/518
* NetID: ki120, bys8
* Name: Kelvin Ihezue, Bryan Shangguan
*/

#include "my_vm64.h"
#include <string.h>
#include <pthread.h>

// global declarations
static pthread_mutex_t phys_bitmap_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t vm_lock          = PTHREAD_MUTEX_INITIALIZER; // For VMA list
static pthread_mutex_t init_lock        = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pt_lock          = PTHREAD_MUTEX_INITIALIZER; // For Page Table modifications
static pthread_mutex_t tlb_lock         = PTHREAD_MUTEX_INITIALIZER;

struct tlb tlb_store;

static unsigned long long tlb_lookups = 0;
static unsigned long long tlb_misses  = 0;
static unsigned long long tlb_time    = 0;

static int vm_initialized             = 0;
static unsigned char *g_physical_mem  = NULL;
static unsigned char *g_phys_bitmap   = NULL;
static pde_t         *g_pml4_root     = NULL;

#define NUM_PHYS_FRAMES (MEMSIZE / PGSIZE)

// VMA list for VMA
struct vm_area {
    uint64_t start;
    uint64_t size; // in pages
    struct vm_area *next;
};
static struct vm_area *g_vm_head = NULL;

// bitmap helpers
int bitmap_test(int index, unsigned char *bitmap) {
    int byte_idx = index >> 3;
    int bit_idx  = index & 0x7;
    return (bitmap[byte_idx] & (1 << bit_idx)) != 0;
}

void bitmap_set(int index, unsigned char *bitmap) {
    int byte_idx = index >> 3;
    int bit_idx  = index & 0x7;
    bitmap[byte_idx] |= (1 << bit_idx);
}

void bitmap_clear(int index, unsigned char *bitmap) {
    int byte_idx = index >> 3;
    int bit_idx  = index & 0x7;
    bitmap[byte_idx] &= ~(1 << bit_idx);
}

int get_free_phys_frame() {
    // linear search
    for (int i = 0; i < NUM_PHYS_FRAMES; i++) {
        if (!bitmap_test(i, g_phys_bitmap)) {
            return i;
        }
    }
    return -1;
}

// setup
void set_physical_mem(void) {
    pthread_mutex_lock(&init_lock);
    if (vm_initialized) {
        pthread_mutex_unlock(&init_lock);
        return;
    }

    g_physical_mem = (unsigned char *)malloc(MEMSIZE);
    if (!g_physical_mem) {
        pthread_mutex_unlock(&init_lock);
        return;
    }
    memset(g_physical_mem, 0, MEMSIZE);

    int phys_frames = NUM_PHYS_FRAMES;
    int bm_bytes = (phys_frames + 7) / 8;
    g_phys_bitmap = (unsigned char *)calloc(bm_bytes, 1);

    // allocate PML4, root
    g_pml4_root = (pde_t *)calloc(PML4_ENTRIES, sizeof(pde_t));

    // init TLB
    for (int i = 0; i < TLB_ENTRIES; i++) {
        tlb_store.valid[i] = false;
    }

    vm_initialized = 1;
    pthread_mutex_unlock(&init_lock);
}

// TLB
int TLB_add(void *va, void *pa) {
    pthread_mutex_lock(&tlb_lock);
    uint64_t v = (uint64_t)va;
    uint64_t p = (uint64_t)pa;
    uint64_t vpn = v >> PT_SHIFT;
    uint64_t pfn = p >> PT_SHIFT;

    unsigned long index = vpn % TLB_ENTRIES;

    tlb_store.valid[index] = true;
    tlb_store.vpn[index]   = vpn;
    
    tlb_store.pfn[index]   = (pfn << PT_SHIFT) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;

    pthread_mutex_unlock(&tlb_lock);
    return 0;
}

pte_t *TLB_check(void *va) {
    pthread_mutex_lock(&tlb_lock);
    uint64_t vpn = (uint64_t)va >> PT_SHIFT;

    unsigned long index = vpn % TLB_ENTRIES;

    if (tlb_store.valid[index] && tlb_store.vpn[index] == vpn) {
        tlb_store.last_used[index] = ++tlb_time;
        pte_t *ret = &tlb_store.pfn[index];
        pthread_mutex_unlock(&tlb_lock);
        return ret;
    }

    pthread_mutex_unlock(&tlb_lock);
    return NULL;
}

void print_TLB_missrate(void) {
    double rate = tlb_lookups == 0 ? 0 : (double)tlb_misses / tlb_lookups;
    fprintf(stderr, "TLB miss rate: %lf\n", rate);
}

// page table
pte_t *translate(pde_t *pgdir, void *va) {
    if (!pgdir) return NULL;
    tlb_lookups++;

    pte_t *tlb_result = TLB_check(va);
    if (tlb_result != NULL) {
        return tlb_result;
    }
    
    uint64_t v = (uint64_t)va;
    
    uint64_t pml4_idx = PML4_INDEX(v);
    if (!(pgdir[pml4_idx] & PTE_PRESENT)) return NULL;
    
    uint64_t pdpt_pfn = pgdir[pml4_idx] >> PT_SHIFT;
    pde_t *pdpt = (pde_t *)&g_physical_mem[pdpt_pfn * PGSIZE];
    
    uint64_t pdpt_idx = PDPT_INDEX(v);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return NULL;
    
    uint64_t pd_pfn = pdpt[pdpt_idx] >> PT_SHIFT;
    pde_t *pd = (pde_t *)&g_physical_mem[pd_pfn * PGSIZE];
    
    uint64_t pd_idx = PD_INDEX(v);
    if (!(pd[pd_idx] & PTE_PRESENT)) return NULL;
    
    uint64_t pt_pfn = pd[pd_idx] >> PT_SHIFT;
    pte_t *pt = (pte_t *)&g_physical_mem[pt_pfn * PGSIZE];
    
    uint64_t pt_idx = PT_INDEX(v);
    if (!(pt[pt_idx] & PTE_PRESENT)) return NULL;
    
    return &pt[pt_idx];
}

int map_page(pde_t *pgdir, void *va, void *pa) {
    pthread_mutex_lock(&pt_lock);
    
    uint64_t v = (uint64_t)va;
    uint64_t p = (uint64_t)pa;
    
    uint64_t pml4_idx = PML4_INDEX(v);
    uint64_t pdpt_idx = PDPT_INDEX(v);
    uint64_t pd_idx   = PD_INDEX(v);
    uint64_t pt_idx   = PT_INDEX(v);
    
    // lvl 4 -> 3
    if (!(pgdir[pml4_idx] & PTE_PRESENT)) {
        pthread_mutex_lock(&phys_bitmap_lock);
        int frame = get_free_phys_frame();
        if (frame == -1) { 
            pthread_mutex_unlock(&phys_bitmap_lock); 
            pthread_mutex_unlock(&pt_lock);
            return -1; 
        }
        bitmap_set(frame, g_phys_bitmap);
        pthread_mutex_unlock(&phys_bitmap_lock);
        
        memset(&g_physical_mem[frame * PGSIZE], 0, PGSIZE);
        pgdir[pml4_idx] = ((uint64_t)frame << PT_SHIFT) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    
    uint64_t pdpt_pfn = pgdir[pml4_idx] >> PT_SHIFT;
    pde_t *pdpt = (pde_t *)&g_physical_mem[pdpt_pfn * PGSIZE];
    
    // lvl 3 -> 2
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        pthread_mutex_lock(&phys_bitmap_lock);
        int frame = get_free_phys_frame();
        if (frame == -1) { 
            pthread_mutex_unlock(&phys_bitmap_lock); 
            pthread_mutex_unlock(&pt_lock);
            return -1; 
        }
        bitmap_set(frame, g_phys_bitmap);
        pthread_mutex_unlock(&phys_bitmap_lock);
        
        memset(&g_physical_mem[frame * PGSIZE], 0, PGSIZE);
        pdpt[pdpt_idx] = ((uint64_t)frame << PT_SHIFT) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    
    uint64_t pd_pfn = pdpt[pdpt_idx] >> PT_SHIFT;
    pde_t *pd = (pde_t *)&g_physical_mem[pd_pfn * PGSIZE];
    
    // lvl 2 -> 1
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        pthread_mutex_lock(&phys_bitmap_lock);
        int frame = get_free_phys_frame();
        if (frame == -1) { 
            pthread_mutex_unlock(&phys_bitmap_lock); 
            pthread_mutex_unlock(&pt_lock);
            return -1; 
        }
        bitmap_set(frame, g_phys_bitmap);
        pthread_mutex_unlock(&phys_bitmap_lock);
        
        memset(&g_physical_mem[frame * PGSIZE], 0, PGSIZE);
        pd[pd_idx] = ((uint64_t)frame << PT_SHIFT) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    
    uint64_t pt_pfn = pd[pd_idx] >> PT_SHIFT;
    pte_t *pt = (pte_t *)&g_physical_mem[pt_pfn * PGSIZE];
    
    // lvl 1
    uint64_t pfn = p >> PT_SHIFT;
    pt[pt_idx] = (pfn << PT_SHIFT) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    
    pthread_mutex_unlock(&pt_lock);
    
    tlb_misses++;
    TLB_add(va, pa);
    return 0;
}

// allocation
void *get_next_avail(int num_pages) {
    pthread_mutex_lock(&vm_lock);
    
    uint64_t start_va = PGSIZE; // start at 4KB
    struct vm_area *curr = g_vm_head;
    
    while (curr) {
        // check gap between start_va and curr->start
        if (curr->start > start_va) {
            uint64_t gap = (curr->start - start_va) / PGSIZE;
            if (gap >= num_pages) {
                pthread_mutex_unlock(&vm_lock);
                return (void *)start_va;
            }
        }
        start_va = curr->start + curr->size * PGSIZE;
        curr = curr->next;
    }
    
    // check after last element
    if ((MAX_MEMSIZE - start_va) / PGSIZE >= num_pages) {
        pthread_mutex_unlock(&vm_lock);
        return (void *)start_va;
    }
    
    pthread_mutex_unlock(&vm_lock);
    return NULL;
}

void *n_malloc(unsigned int num_bytes) {
    if (!vm_initialized) set_physical_mem();
    if (num_bytes == 0) return NULL;
    
    int num_pages = (num_bytes + PGSIZE - 1) / PGSIZE;
    
    void *va = get_next_avail(num_pages);
    if (!va) return NULL;
    
    // add to VMA list
    pthread_mutex_lock(&vm_lock);
    struct vm_area *new_node = malloc(sizeof(struct vm_area));
    new_node->start = (uint64_t)va;
    new_node->size = num_pages;
    new_node->next = NULL;
    
    // insert sorted
    if (!g_vm_head || g_vm_head->start > new_node->start) {
        new_node->next = g_vm_head;
        g_vm_head = new_node;
    } else {
        struct vm_area *curr = g_vm_head;
        while (curr->next && curr->next->start < new_node->start) {
            curr = curr->next;
        }
        new_node->next = curr->next;
        curr->next = new_node;
    }
    pthread_mutex_unlock(&vm_lock);
    
    // map pages
    for (int i = 0; i < num_pages; i++) {
        pthread_mutex_lock(&phys_bitmap_lock);
        int frame = get_free_phys_frame();
        if (frame == -1) {
            pthread_mutex_unlock(&phys_bitmap_lock);
            // rollback would go here, free previously allocated pages)
            return NULL;
        }
        bitmap_set(frame, g_phys_bitmap);
        pthread_mutex_unlock(&phys_bitmap_lock);
        
        uint64_t page_va = (uint64_t)va + i * PGSIZE;
        uint64_t page_pa = (uint64_t)frame * PGSIZE;
        
        if (map_page(g_pml4_root, (void *)page_va, (void *)page_pa) < 0) {
            return NULL;
        }
    }
    
    return va;
}

void n_free(void *va, int size) {
    if (!va || size <= 0) return;
    
    uint64_t v = (uint64_t)va;
    int num_pages = (size + PGSIZE - 1) / PGSIZE;
    
    // remove from VMA
    pthread_mutex_lock(&vm_lock);
    struct vm_area *curr = g_vm_head;
    struct vm_area *prev = NULL;
    int found = 0;
    while (curr) {
        if (curr->start == v) {
            if (prev) prev->next = curr->next;
            else g_vm_head = curr->next;
            free(curr);
            found = 1;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&vm_lock);
    
    if (!found) return;

    // free physical pages
    for (int i = 0; i < num_pages; i++) {
        uint64_t page_va = v + i * PGSIZE;
        pte_t *pte = translate(g_pml4_root, (void *)page_va);
        if (pte && (*pte & PTE_PRESENT)) {
            uint64_t pfn = *pte >> PT_SHIFT;
            pthread_mutex_lock(&phys_bitmap_lock);
            bitmap_clear(pfn, g_phys_bitmap);
            pthread_mutex_unlock(&phys_bitmap_lock);
            
            pthread_mutex_lock(&pt_lock);
            *pte = 0; // clear PTE
            pthread_mutex_unlock(&pt_lock);
        }
    }
}

// data movement
int put_data(void *va, void *val, int size) {
    if (!va || !val || size <= 0) return -1;
    
    uint64_t v = (uint64_t)va;
    char *src = (char *)val;
    int remaining = size;
    
    while (remaining > 0) {
        uint64_t offset = v & OFFSET_MASK;
        uint64_t len = PGSIZE - offset;
        if (len > remaining) len = remaining;
        
        pte_t *pte = translate(g_pml4_root, (void *)v);
        if (!pte || !(*pte & PTE_PRESENT)) return -1;
        
        uint64_t pfn = *pte >> PT_SHIFT;
        uint64_t pa = pfn * PGSIZE + offset;
        
        memcpy(&g_physical_mem[pa], src, len);
        
        v += len;
        src += len;
        remaining -= len;
    }
    return 0;
}

void get_data(void *va, void *val, int size) {
    if (!va || !val || size <= 0) return;
    
    uint64_t v = (uint64_t)va;
    char *dst = (char *)val;
    int remaining = size;
    
    while (remaining > 0) {
        uint64_t offset = v & OFFSET_MASK;
        uint64_t len = PGSIZE - offset;
        if (len > remaining) len = remaining;
        
        pte_t *pte = translate(g_pml4_root, (void *)v);
        if (!pte || !(*pte & PTE_PRESENT)) return;
        
        uint64_t pfn = *pte >> PT_SHIFT;
        uint64_t pa = pfn * PGSIZE + offset;
        
        memcpy(dst, &g_physical_mem[pa], len);
        
        v += len;
        dst += len;
        remaining -= len;
    }
}

void mat_mult(void *mat1, void *mat2, int size, void *answer) {
    int i, j, k;
    int val1, val2, res;
    
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            res = 0;
            for (k = 0; k < size; k++) {
                uint64_t addr1 = (uint64_t)mat1 + ((i * size + k) * sizeof(int));
                uint64_t addr2 = (uint64_t)mat2 + ((k * size + j) * sizeof(int));
                get_data((void *)addr1, &val1, sizeof(int));
                get_data((void *)addr2, &val2, sizeof(int));
                res += val1 * val2;
            }
            uint64_t addr_ans = (uint64_t)answer + ((i * size + j) * sizeof(int));
            put_data((void *)addr_ans, &res, sizeof(int));
        }
    }
}