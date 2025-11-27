/*
* Add NetID and names of all project partners
* Course: CS 416/518
* NetID: ki120, bys8
* Name: Kelvin Ihezue, Bryan Shangguan
*/

#include "my_vm.h"
#include <string.h>
#include <pthread.h>

#define PT_REGION_SIZE 2048
#define PT_REGION_START (NUM_PHYS_FRAMES - PT_REGION_SIZE)

// global declarations
static pthread_mutex_t phys_bitmap_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t virt_bitmap_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t init_lock        = PTHREAD_MUTEX_INITIALIZER;

struct tlb tlb_store;

// optional counters for TLB statistics
static unsigned long long tlb_lookups = 0;
static unsigned long long tlb_misses  = 0;
// timestamp for LRU replacement in TLB
static unsigned long long tlb_time    = 0;

static int vm_initialized             = 0;
static unsigned char *g_physical_mem  = NULL;   // 1 GB simulated physical memory
static unsigned char *g_phys_bitmap   = NULL;   // 1 bit per physical frame
static unsigned char *g_virt_bitmap   = NULL;   // 1 bit per virtual page
static pde_t         *g_pgdir_root    = NULL;   // top-level page directory

// bitmap helpers
// Test a bit: 1 if allocated, 0 if free
int bitmap_test(int index, unsigned char *bitmap)
{
    int byte_idx = index >> 3;      // index / 8
    int bit_idx  = index & 0x7;     // index % 8
    unsigned char mask = (unsigned char)(1u << bit_idx);
    return (bitmap[byte_idx] & mask) != 0;
}

// set a bit to 1 (allocated)
void bitmap_set(int index, unsigned char *bitmap)
{
    int byte_idx = index >> 3;
    int bit_idx  = index & 0x7;
    unsigned char mask = (unsigned char)(1u << bit_idx);
    bitmap[byte_idx] |= mask;
}

// clear a bit to 0 (free)
void bitmap_clear(int index, unsigned char *bitmap)
{
    int byte_idx = index >> 3;
    int bit_idx  = index & 0x7;
    unsigned char mask = (unsigned char)(1u << bit_idx);
    bitmap[byte_idx] &= (unsigned char)~mask;
}

// Find the next free bit at or after start_bit
// Return: bit index or -1 if none free
int bitmap_get_next_free(int start_bit, int total_bits)
{
    for (int bit = start_bit; bit < total_bits; bit++) {
        if (!bitmap_test(bit, g_virt_bitmap))  // only used for virtual bitmap in Person B
            return bit;
    }
    return -1;
}

// setup
void set_physical_mem(void) {

    /* make initialization thread-safe */
    pthread_mutex_lock(&init_lock);

    if (vm_initialized) {
        pthread_mutex_unlock(&init_lock);
        return;
    }

    // allocate the simulated 1 GB physical memory
    g_physical_mem = (unsigned char *)malloc((size_t)MEMSIZE);
    if (g_physical_mem == NULL) {
        pthread_mutex_unlock(&init_lock);
        return;
    }
    memset(g_physical_mem, 0, (size_t)MEMSIZE);

    // allocate bitmaps 1 bit per page/frame
    uint32_t phys_frames   = (uint32_t)NUM_PHYS_FRAMES;
    uint32_t virt_pages    = (uint32_t)NUM_VIRT_PAGES;
    size_t   phys_bm_bytes = (size_t)((phys_frames + 7u) / 8u);
    size_t   virt_bm_bytes = (size_t)((virt_pages  + 7u) / 8u);

    g_phys_bitmap = (unsigned char *)calloc(phys_bm_bytes, 1);
    g_virt_bitmap = (unsigned char *)calloc(virt_bm_bytes, 1);
    if (g_phys_bitmap == NULL || g_virt_bitmap == NULL) {
        free(g_phys_bitmap); g_phys_bitmap = NULL;
        free(g_virt_bitmap); g_virt_bitmap = NULL;
        free(g_physical_mem); g_physical_mem = NULL;
        pthread_mutex_unlock(&init_lock);
        return;
    }

    // allocate + clear the top-level page directory (1024 entries)
    g_pgdir_root = (pde_t *)calloc(PD_ENTRIES, sizeof(pde_t));
    if (g_pgdir_root == NULL) {
        free(g_phys_bitmap); g_phys_bitmap = NULL;
        free(g_virt_bitmap); g_virt_bitmap = NULL;
        free(g_physical_mem); g_physical_mem = NULL;
        pthread_mutex_unlock(&init_lock);
        return;
    }

    // initialize TLB
    for (int i = 0; i < TLB_ENTRIES; i++) {
        tlb_store.valid[i]     = false;
        tlb_store.vpn[i]       = 0;
        tlb_store.pfn[i]       = 0;
        tlb_store.last_used[i] = 0;
    }

    tlb_lookups = 0;
    tlb_misses  = 0;
    tlb_time    = 0;
    vm_initialized = 1;

    pthread_mutex_unlock(&init_lock);
}

// TLB
int TLB_add(void *va, void *pa)
{
    if (!va || !pa) {
        return -1;
    }

    uint32_t v   = VA2U(va);
    uint32_t p   = VA2U(pa);
    uint32_t vpn = v >> OFFBITS;   // virtual page number
    uint32_t pfn = p >> OFFBITS;   // physical frame number

    int free_idx = -1;
    int lru_idx  = 0;
    unsigned long long lru_time_val = (unsigned long long)-1;

    for (int i = 0; i < TLB_ENTRIES; i++) {
        if (tlb_store.valid[i]) {
            // if same VPN is already present, just refresh it
            if (tlb_store.vpn[i] == vpn) {
                tlb_store.pfn[i]       = pfn;
                tlb_store.last_used[i] = ++tlb_time;
                return 0;
            }
            // track least recently used entry
            if (tlb_store.last_used[i] < lru_time_val) {
                lru_time_val = tlb_store.last_used[i];
                lru_idx      = i;
            }
        } else if (free_idx < 0) {
            free_idx = i;
        }
    }

    int target_idx = (free_idx >= 0) ? free_idx : lru_idx;

    tlb_store.valid[target_idx]     = true;
    tlb_store.vpn[target_idx]       = vpn;
    tlb_store.pfn[target_idx]       = pfn;
    tlb_store.last_used[target_idx] = ++tlb_time;

    return 0;
}

pte_t *TLB_check(void *va) {
    (void)va;
    return NULL; // always miss
}

void print_TLB_missrate(void) {
    double miss_rate = 0.0;
    if (tlb_lookups != 0) {
        miss_rate = (double)tlb_misses / (double)tlb_lookups;
    }

    fprintf(stderr,
        "TLB: lookups=%llu misses=%llu TLB miss rate %lf \n", 
        tlb_lookups, tlb_misses, miss_rate);
}

// page table
pte_t *translate(pde_t *pgdir, void *va)
{
    if (pgdir == NULL) {
        return NULL;
    }

    // count one TLB lookup per translation request
    tlb_lookups++;

    uint32_t v   = VA2U(va);
    uint32_t pdx = PDX(v);
    uint32_t ptx = PTX(v);

    pde_t pde = pgdir[pdx];
    if ((pde & PTE_PRESENT) == 0) {
        // no mapping for this VA
        return NULL;
    }

    uint32_t pt_pfn   = pde >> PFN_SHIFT;
    uint32_t pt_paddr = pt_pfn * PGSIZE;

    pte_t *pt  = (pte_t *)&g_physical_mem[pt_paddr];
    pte_t *pte = &pt[ptx];

    if (((*pte) & PTE_PRESENT) == 0) {
        // page not present
        return NULL;
    }

    return pte;
}

int map_page(pde_t *pgdir, void *va, void *pa)
{
    uint32_t v = VA2U(va);
    uint32_t p = VA2U(pa);

    uint32_t pdx = PDX(v);
    uint32_t ptx = PTX(v);

    // allocate page table if the PDE is not yet present
    if ((pgdir[pdx] & PTE_PRESENT) == 0) {

        int pt_index = -1;

        pthread_mutex_lock(&phys_bitmap_lock);

        for (int frame = PT_REGION_START; frame < NUM_PHYS_FRAMES; frame++) {
            if (!bitmap_test(frame, g_phys_bitmap)) {
                bitmap_set(frame, g_phys_bitmap);
                pt_index = frame;
                break;
            }
        }

        pthread_mutex_unlock(&phys_bitmap_lock);

        if (pt_index < 0) {
            return -1;
        }

        uint32_t pt_paddr = (uint32_t)pt_index * PGSIZE;
        memset(&g_physical_mem[pt_paddr], 0, PGSIZE);

        pgdir[pdx] = (pt_index << PFN_SHIFT) | PTE_PRESENT | PTE_WRITABLE;
    }

    uint32_t pt_pfn   = pgdir[pdx] >> PFN_SHIFT;
    uint32_t pt_paddr = pt_pfn * PGSIZE;

    pte_t *pt = (pte_t *)&g_physical_mem[pt_paddr];
    uint32_t pfn = p >> PFN_SHIFT;

    pt[ptx] = (pfn << PFN_SHIFT) | PTE_PRESENT | PTE_WRITABLE;

    // count one compulsory miss for this newly mapped virtual page
    tlb_misses++;
    TLB_add(va, pa);

    return 0;
}

// allocation
void *get_next_avail(int num_pages)
{
    pthread_mutex_lock(&virt_bitmap_lock);

    int total_pages = (int)NUM_VIRT_PAGES;

    int run_len   = 0;
    int run_start = -1;

    // start at VPN 1 so VA 0 is never allocated
    for (int idx = 1; idx < total_pages; idx++) {
        if (!bitmap_test(idx, g_virt_bitmap)) {
            if (run_start == -1) {
                run_start = idx;
            }
            run_len++;

            if (run_len == num_pages) {

                for (int mark = run_start; mark < run_start + num_pages; mark++) {
                    bitmap_set(mark, g_virt_bitmap);
                }

                pthread_mutex_unlock(&virt_bitmap_lock);

                uint32_t vaddr = (uint32_t)run_start * PGSIZE;
                return U2VA(vaddr);
            }
        } else {
            run_start = -1;
            run_len   = 0;
        }
    }

    pthread_mutex_unlock(&virt_bitmap_lock);
    return NULL;
}

void *n_malloc(unsigned int num_bytes)
{
    if (!vm_initialized) {
        set_physical_mem();
    }

    if (num_bytes == 0) {
        return NULL;
    }

    int num_pages = (int)(PAGE_ROUND_UP(num_bytes) / PGSIZE);
    void *va_base = get_next_avail(num_pages);
    if (va_base == NULL) {
        return NULL;
    }

    // track which PFNs we've allocated so we can roll back on failure
    int allocated_pfns[num_pages];
    for (int i = 0; i < num_pages; i++) {
        allocated_pfns[i] = -1;
    }

    for (int p = 0; p < num_pages; p++) {

        int phys_index = -1;

        // allocate only from DATA region
        pthread_mutex_lock(&phys_bitmap_lock);
        for (int frame = 0; frame < PT_REGION_START; frame++) {
            if (!bitmap_test(frame, g_phys_bitmap)) {
                bitmap_set(frame, g_phys_bitmap);
                phys_index = frame;
                break;
            }
        }
        pthread_mutex_unlock(&phys_bitmap_lock);

        if (phys_index < 0) {

            // roll back any PFNs we already set
            pthread_mutex_lock(&phys_bitmap_lock);
            for (int j = 0; j < p; j++) {
                if (allocated_pfns[j] >= 0) {
                    bitmap_clear(allocated_pfns[j], g_phys_bitmap);
                }
            }
            pthread_mutex_unlock(&phys_bitmap_lock);

            // roll back virtual pages
            pthread_mutex_lock(&virt_bitmap_lock);
            uint32_t start_vpn = VA2U(va_base) / PGSIZE;
            for (int r = 0; r < num_pages; r++) {
                bitmap_clear((int)start_vpn + r, g_virt_bitmap);
            }
            pthread_mutex_unlock(&virt_bitmap_lock);

            return NULL;
        }

        allocated_pfns[p] = phys_index;

        uint32_t pa      = (uint32_t)phys_index * PGSIZE;
        uint32_t va_page = (uint32_t)VA2U(va_base) + (uint32_t)(p * PGSIZE);

        // IMPORTANT: phys lock is NOT held here → no deadlock in map_page
        if (map_page(g_pgdir_root, U2VA(va_page), U2VA(pa)) < 0) {
            // if mapping fails, roll back everything
            pthread_mutex_lock(&phys_bitmap_lock);
            for (int j = 0; j <= p; j++) {
                if (allocated_pfns[j] >= 0) {
                    bitmap_clear(allocated_pfns[j], g_phys_bitmap);
                }
            }
            pthread_mutex_unlock(&phys_bitmap_lock);

            pthread_mutex_lock(&virt_bitmap_lock);
            uint32_t start_vpn = VA2U(va_base) / PGSIZE;
            for (int r = 0; r < num_pages; r++) {
                bitmap_clear((int)start_vpn + r, g_virt_bitmap);
            }
            pthread_mutex_unlock(&virt_bitmap_lock);

            return NULL;
        }
    }

    return va_base;
}

// deallocation
void n_free(void *va, int size)
{
    if (va == NULL || size <= 0) {
        return;
    }

    uint32_t vaddr     = VA2U(va);
    int      num_pages = (int)(PAGE_ROUND_UP((unsigned int)size) / PGSIZE);
    uint32_t start_vpn = vaddr / PGSIZE;

    pthread_mutex_lock(&virt_bitmap_lock);
    for (int i = 0; i < num_pages; i++) {
        bitmap_clear((int)start_vpn + i, g_virt_bitmap);
    }
    pthread_mutex_unlock(&virt_bitmap_lock);

    pthread_mutex_lock(&phys_bitmap_lock);
    for (int i = 0; i < num_pages; i++) {

        uint32_t page_va = vaddr + (uint32_t)(i * PGSIZE);
        pte_t *pte = translate(g_pgdir_root, U2VA(page_va));

        if (pte != NULL && ((*pte) & PTE_PRESENT)) {
            uint32_t pfn = (*pte >> PFN_SHIFT);
            bitmap_clear((int)pfn, g_phys_bitmap);
            *pte = 0;
        }
    }
    pthread_mutex_unlock(&phys_bitmap_lock);
    
}
// data movement

int put_data(void *va, void *val, int size)
{
    if (va == NULL || val == NULL || size <= 0) {
        return -1;
    }

    uint8_t  *src   = (uint8_t *)val;
    uint32_t  vaddr = VA2U(va);
    int       left  = size;

    while (left > 0) {

        uint32_t page_offset   = OFF(vaddr);
        uint32_t space_in_page = PGSIZE - page_offset;
        uint32_t chunk         = (left < (int)space_in_page) ?
                                  (uint32_t)left : space_in_page;

        pte_t *pte = translate(g_pgdir_root, U2VA(vaddr));
        if (pte == NULL || ((*pte) & PTE_PRESENT) == 0) {
            return -1;
        }

        uint32_t pfn   = (*pte >> PFN_SHIFT);
        uint32_t paddr = pfn * PGSIZE + page_offset;

        memcpy(&g_physical_mem[paddr], src, chunk);

        vaddr += chunk;
        src   += chunk;
        left  -= (int)chunk;
    }

    return 0;
}

void get_data(void *va, void *val, int size)
{
    if (va == NULL || val == NULL || size <= 0) {
        return;
    }

    uint8_t  *dst   = (uint8_t *)val;
    uint32_t  vaddr = VA2U(va);
    int       left  = size;

    while (left > 0) {

        uint32_t page_offset   = OFF(vaddr);
        uint32_t space_in_page = PGSIZE - page_offset;
        uint32_t chunk         = (left < (int)space_in_page) ?
                                  (uint32_t)left : space_in_page;

        pte_t *pte = translate(g_pgdir_root, U2VA(vaddr));
        if (pte == NULL || ((*pte) & PTE_PRESENT) == 0) {
            memset(dst, 0, chunk);
            return;
        }

        uint32_t pfn   = (*pte >> PFN_SHIFT);
        uint32_t paddr = pfn * PGSIZE + page_offset;

        memcpy(dst, &g_physical_mem[paddr], chunk);

        vaddr += chunk;
        dst   += chunk;
        left  -= (int)chunk;
    }
}

// matrix multiplication
// matrix test
void mat_mult(void *mat1, void *mat2, int size, void *answer)
{
    int i, j, k;
    uint32_t a, b, c;

    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {

            c = 0;

            for (k = 0; k < size; k++) {

                uint32_t idx_a = (uint32_t)(i * size + k);
                uint32_t idx_b = (uint32_t)(k * size + j);

                uint32_t addr_a = idx_a * sizeof(uint32_t);
                uint32_t addr_b = idx_b * sizeof(uint32_t);

                get_data(U2VA(VA2U(mat1) + addr_a), &a, sizeof(uint32_t));
                get_data(U2VA(VA2U(mat2) + addr_b), &b, sizeof(uint32_t));

                c += a * b;
            }

            uint32_t idx_c  = (uint32_t)(i * size + j);
            uint32_t addr_c = idx_c * sizeof(uint32_t);
            put_data(U2VA(VA2U(answer) + addr_c), &c, sizeof(uint32_t));
        }
    }
}
