/*
* Add NetID and names of all project partners
* Course: CS 416/518
* NetID: ki120, bys8
* Name: Kelvin Ihezue, Bryan Shangguan
*/


#include "my_vm.h"
#include <string.h>   // optional for memcpy if you later implement put/get

// -----------------------------------------------------------------------------
// Global Declarations (optional)
// -----------------------------------------------------------------------------

struct tlb tlb_store; // Placeholder for your TLB structure

// Optional counters for TLB statistics
static unsigned long long tlb_lookups = 0;
static unsigned long long tlb_misses  = 0;

static int vm_initialized = 0;
static unsigned char *g_physical_mem = NULL;   // 1 GB simulated physical memory
static unsigned char *g_phys_bitmap  = NULL;   // 1 bit per physical frame
static unsigned char *g_virt_bitmap  = NULL;   // 1 bit per virtual page
static pde_t         *g_pgdir_root   = NULL;   // top-level page directory

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
/*
 * set_physical_mem()
 * ------------------
 * Allocates and initializes simulated physical memory and any required
 * data structures (e.g., bitmaps for tracking page use).
 *
 * Return value: None.
 * Errors should be handled internally (e.g., failed allocation).
 */
void set_physical_mem(void) {
    // TODO: Implement memory allocation for simulated physical memory.
    // Use 32-bit values for sizes, page counts, and offsets.

    if (vm_initialized) return;

    // allocate the simulated 1 GB physical memory
    g_physical_mem = (unsigned char *)malloc((size_t)MEMSIZE);
    if (!g_physical_mem) {
        fprintf(stderr, "set_physical_mem: failed to allocate physical memory\n");
        return;
    }
    memset(g_physical_mem, 0, (size_t)MEMSIZE);

    // allocate bitmaps 1 bit per page/frame
    const uint32_t phys_frames   = (uint32_t)(NUM_PHYS_FRAMES); // 1GB / 4KB = 262,144
    const uint32_t virt_pages    = (uint32_t)(NUM_VIRT_PAGES);  // 4GB / 4KB = 1,048,576
    const size_t   phys_bm_bytes = (size_t)((phys_frames + 7u) / 8u);
    const size_t   virt_bm_bytes = (size_t)((virt_pages  + 7u) / 8u);

    g_phys_bitmap = (unsigned char *)calloc(phys_bm_bytes, 1);
    g_virt_bitmap = (unsigned char *)calloc(virt_bm_bytes, 1);
    if (!g_phys_bitmap || !g_virt_bitmap) {
        fprintf(stderr, "set_physical_mem: failed to allocate bitmaps\n");
        free(g_phys_bitmap); g_phys_bitmap = NULL;
        free(g_virt_bitmap); g_virt_bitmap = NULL;
        free(g_physical_mem); g_physical_mem = NULL;
        return;
    }

    // allocate + clear the top-level page directory (1024 entries)
    g_pgdir_root = (pde_t *)calloc(PD_ENTRIES, sizeof(pde_t));
    if (!g_pgdir_root) {
        fprintf(stderr, "set_physical_mem: failed to allocate page directory\n");
        free(g_phys_bitmap); g_phys_bitmap = NULL;
        free(g_virt_bitmap); g_virt_bitmap = NULL;
        free(g_physical_mem); g_physical_mem = NULL;
        return;
    }

    // initialize TLB
    for (int i = 0; i < TLB_ENTRIES; i++) {
        tlb_store.valid[i] = false;
        tlb_store.vpn[i] = 0;
        tlb_store.pfn[i] = 0;
        tlb_store.last_used[i] = 0;
    }

    // reset TLB stats
    tlb_lookups = 0;
    tlb_misses  = 0;
    vm_initialized = 1;
}

// -----------------------------------------------------------------------------
// TLB
// -----------------------------------------------------------------------------

/*
 * TLB_add()
 * ---------
 * Adds a new virtual-to-physical translation to the TLB.
 * Ensure thread safety when updating shared TLB data.
 *
 * Return:
 *   0  -> Success (translation successfully added)
 *  -1  -> Failure (e.g., TLB full or invalid input)
 */
int TLB_add(void *va, void *pa)
{
    // TODO: Implement TLB insertion logic.
    return -1; // Currently returns failure placeholder.
}

/*
 * TLB_check()
 * -----------
 * Looks up a virtual address in the TLB.
 *
 * Return:
 *   Pointer to the corresponding page table entry (PTE) if found.
 *   NULL if the translation is not found (TLB miss).
 */
pte_t *TLB_check(void *va)
{
    // TODO: Implement TLB lookup.
    return NULL; // Currently returns TLB miss.
}

/*
 * print_TLB_missrate()
 * --------------------
 * Calculates and prints the TLB miss rate.
 *
 * Return value: None.
 */
void print_TLB_missrate(void)
{
    double miss_rate = 0.0;
    // TODO: Calculate miss rate as (tlb_misses / tlb_lookups).
    fprintf(stderr, "TLB miss rate %lf \n", miss_rate);
}

// -----------------------------------------------------------------------------
// Page Table
// -----------------------------------------------------------------------------

/*
 * translate()
 * -----------
 * Translates a virtual address to a physical address.
 * Perform a TLB lookup first; if not found, walk the page directory
 * and page tables using a two-level lookup.
 *
 * Return:
 *   Pointer to the PTE structure if translation succeeds.
 *   NULL if translation fails (e.g., page not mapped).
 */
pte_t *translate(pde_t *pgdir, void *va)
{
    // TODO: Extract the 32-bit virtual address and compute indices
    // for the page directory, page table, and offset.
    // Return the corresponding PTE if found.
    return NULL; // Translation unsuccessful placeholder.
}

/*
 * map_page()
 * -----------
 * Establishes a mapping between a virtual and a physical page.
 * Creates intermediate page tables if necessary.
 *
 * Return:
 *   0  -> Success (mapping created)
 *  -1  -> Failure (e.g., no space or invalid address)
 */
int map_page(pde_t *pgdir, void *va, void *pa)
{
    // TODO: Map virtual address to physical address in the page tables.
    return -1; // Failure placeholder.
}

// -----------------------------------------------------------------------------
// Allocation
// -----------------------------------------------------------------------------

/*
 * get_next_avail()
 * ----------------
 * Finds and returns the base virtual address of the next available
 * block of contiguous free pages.
 *
 * Return:
 *   Pointer to the base virtual address if available.
 *   NULL if there are no sufficient free pages.
 */
void *get_next_avail(int num_pages)
{
    // TODO: Implement virtual bitmap search for free pages.
    return NULL; // No available block placeholder.
}

/*
 * n_malloc()
 * -----------
 * Allocates a given number of bytes in virtual memory.
 * Initializes physical memory and page directories if not already done.
 *
 * Return:
 *   Pointer to the starting virtual address of allocated memory (success).
 *   NULL if allocation fails.
 */
void *n_malloc(unsigned int num_bytes)
{
    // TODO: Determine required pages, allocate them, and map them.
    return NULL; // Allocation failure placeholder.
}

/*
 * n_free()
 * ---------
 * Frees one or more pages of memory starting at the given virtual address.
 * Marks the corresponding virtual and physical pages as free.
 * Removes the translation from the TLB.
 *
 * Return value: None.
 */
void n_free(void *va, int size)
{
    // TODO: Clear page table entries, update bitmaps, and invalidate TLB.
    


}

// -----------------------------------------------------------------------------
// Data Movement
// -----------------------------------------------------------------------------

/*
 * put_data()
 * ----------
 * Copies data from a user buffer into simulated physical memory using
 * the virtual address. Handle page boundaries properly.
 *
 * Return:
 *   0  -> Success (data written successfully)
 *  -1  -> Failure (e.g., translation failure)
 */
int put_data(void *va, void *val, int size)
{
    // TODO: Walk virtual pages, translate to physical addresses,
    // and copy data into simulated memory.
    



    return -1; // Failure placeholder.
}

/*
 * get_data()
 * -----------
 * Copies data from simulated physical memory (accessed via virtual address)
 * into a user buffer.
 *
 * Return value: None.
 */
void get_data(void *va, void *val, int size)
{
    // TODO: Perform reverse operation of put_data().
    //
}

// -----------------------------------------------------------------------------
// Matrix Multiplication
// -----------------------------------------------------------------------------

/*
 * mat_mult()
 * ----------
 * Performs matrix multiplication of two matrices stored in virtual memory.
 * Each element is accessed and stored using get_data() and put_data().
 *
 * Return value: None.
 */
void mat_mult(void *mat1, void *mat2, int size, void *answer)
{
    int i, j, k;
    uint32_t a, b, c;

    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            c = 0;
            for (k = 0; k < size; k++) {
                // TODO: Compute addresses for mat1[i][k] and mat2[k][j].
                // Retrieve values using get_data() and perform multiplication.
                get_data(NULL, &a, sizeof(int));  // placeholder
                get_data(NULL, &b, sizeof(int));  // placeholder
                c += (a * b);
            }
            // TODO: Store the result in answer[i][j] using put_data().
            put_data(NULL, (void *)&c, sizeof(int)); // placeholder
        }
    }
}

