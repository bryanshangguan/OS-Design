/*
* Course: CS 416/518
* NetID: ki120, bys8
* Name: Kelvin Ihezue, Bryan Shangguan
*/

#ifndef MY_VM_H_INCLUDED
#define MY_VM_H_INCLUDED

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 *  Virtual Memory Simulation Header
 * ============================================================================
 *  This header defines constants, data types, and function prototypes
 *  for implementing a simulated 32-bit virtual memory system.
 *
 *  Students will:
 *   - Fill in missing constants and macros for address translation.
 *   - Define TLB structure and page table entry fields.
 *   - Implement all declared functions in my_vm.c.
 *
 *  Return conventions (used across functions):
 *    0   → Success
 *   -1   → Failure
 *   NULL → Translation or lookup not found
 * ============================================================================
 */

// memory and paging configuration

#define VA_BITS        32u           // simulated virtual address width
#define PGSIZE         4096u         // page size = 4 KB

#define MAX_MEMSIZE    (1ULL << 32)  // max virtual memory = 4 GB
#define MEMSIZE        (1ULL << 30)  // simulated physical memory = 1 GB

// constants for bit shifts and masks
#define PTXSHIFT      12u              // bits to shift for page table index
#define PDXSHIFT      22u              // bits to shift for page directory index

#define OFFBITS       12u              // offset bits for 4KB pages
#define INDEXBITS     10u              // bits per level (1024 entries)

#define PXMASK        0x3FFu           // mask for a 10-bit page index
#define OFFMASK       0xFFFu           // mask for 12-bit offset

#define PT_ENTRIES    (PGSIZE/sizeof(pte_t))  // 1024 entries per page table
#define PD_ENTRIES    (PGSIZE/sizeof(pde_t))  // 1024 entries per directory

// macros to extract address components
#define PDX(va)       (((uint32_t)(va) >> PDXSHIFT) & PXMASK)
#define PTX(va)       (((uint32_t)(va) >> PTXSHIFT) & PXMASK)
#define OFF(va)       ((uint32_t)(va) & OFFMASK)

#define PAGE_ROUND_UP(x)   (((x) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PAGE_INDEX_FROM_PA(pa) ((uint32_t)(pa) >> OFFBITS)

// type fefinitions
typedef uint32_t vaddr32_t;   // simulated 32-bit virtual address
typedef uint32_t paddr32_t;   // simulated 32-bit physical address
typedef uint32_t pte_t;       // page table entry
typedef uint32_t pde_t;       // page directory entry

// page table flags
#define PFN_SHIFT     OFFBITS          // PFN stored above offset bits
#define PTE_PRESENT   0x001u
#define PTE_WRITABLE  0x002u
#define PTE_USER      0x004u

// address conversion helpers
static inline vaddr32_t VA2U(void *va)     { return (vaddr32_t)(uintptr_t)va; }
static inline void*     U2VA(vaddr32_t u)  { return (void*)(uintptr_t)u; }

// TLB configuration
#define TLB_ENTRIES   512   // default number of TLB entries

struct tlb {
    uint32_t vpn[TLB_ENTRIES];
    uint32_t pfn[TLB_ENTRIES];
    bool     valid[TLB_ENTRIES];
    uint64_t last_used[TLB_ENTRIES];
};

extern struct tlb tlb_store;

// function prototypes
/*
 * Initializes physical memory and supporting data structures.
 * Return: None.
 */
void set_physical_mem(void);

/*
 * Adds a new virtual-to-physical translation to the TLB.
 * Return: 0 on success, -1 on failure.
 */
int TLB_add(void *va, void *pa);

/*
 * Checks if a virtual address translation exists in the TLB.
 * Return: pointer to PTE on hit; NULL on miss.
 */
pte_t *TLB_check(void *va);

/*
 * Calculates and prints the TLB miss rate.
 * Return: None.
 */
void print_TLB_missrate(void);

/*
 * Translates a virtual address to a physical address.
 * Return: pointer to PTE if successful; NULL otherwise.
 */
pte_t *translate(pde_t *pgdir, void *va);

/*
 * Creates a mapping between a virtual and a physical page.
 * Return: 0 on success, -1 on failure.
 */
int map_page(pde_t *pgdir, void *va, void *pa);

/*
 * Finds the next available block of contiguous virtual pages.
 * Return: pointer to base virtual address on success; NULL if unavailable.
 */
void *get_next_avail(int num_pages);

/*
 * Allocates memory in the simulated virtual address space.
 * Return: pointer to base virtual address on success; NULL on failure.
 */
void *n_malloc(unsigned int num_bytes);

/*
 * Frees one or more pages of memory starting from the given virtual address.
 * Return: None.
 */
void n_free(void *va, int size);

/*
 * Copies data from a user buffer into simulated physical memory
 * through a virtual address.
 * Return: 0 on success, -1 on failure.
 */
int put_data(void *va, void *val, int size);

/*
 * Copies data from simulated physical memory into a user buffer.
 * Return: None.
 */
void get_data(void *va, void *val, int size);

/*
 * Performs matrix multiplication using data stored in simulated memory.
 * Each element should be accessed via get_data() and stored via put_data().
 * Return: None.
 */
void mat_mult(void *mat1, void *mat2, int size, void *answer);
int bitmap_get_next_free(int start_bit, int total_bits);  // returns index or -1
int bitmap_test(int index, unsigned char *bitmap);        // returns 0/1
void bitmap_set(int index, unsigned char *bitmap);        // marks allocated
void bitmap_clear(int index, unsigned char *bitmap);      // marks free

// convenience: number of physical frames and virtual pages
#define NUM_PHYS_FRAMES   (MEMSIZE / PGSIZE)
#define NUM_VIRT_PAGES    (MAX_MEMSIZE / PGSIZE)


#endif