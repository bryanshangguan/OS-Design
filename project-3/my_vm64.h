/*
* Course: CS 416/518
* NetID: ki120, bys8
* Name: Kelvin Ihezue, Bryan Shangguan
*/

#ifndef MY_VM64_H_INCLUDED
#define MY_VM64_H_INCLUDED

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

// memory and paging configuration (64-bit)
#define PGSIZE         4096ULL
#define MEMSIZE        (1ULL << 30) // 1GB physical memory
#define MAX_MEMSIZE    (1ULL << 48) // 48-bit virtual address space

// page table constants
#define PT_ENTRIES     512
#define PD_ENTRIES     512
#define PDPT_ENTRIES   512
#define PML4_ENTRIES   512

// shifts
#define PT_SHIFT       12
#define PD_SHIFT       21
#define PDPT_SHIFT     30
#define PML4_SHIFT     39

// masks
#define OFFSET_MASK    0xFFFULL
#define PT_MASK        0x1FFULL
#define PD_MASK        0x1FFULL
#define PDPT_MASK      0x1FFULL
#define PML4_MASK      0x1FFULL

// macros
#define PML4_INDEX(va) (((uint64_t)(va) >> PML4_SHIFT) & PML4_MASK)
#define PDPT_INDEX(va) (((uint64_t)(va) >> PDPT_SHIFT) & PDPT_MASK)
#define PD_INDEX(va)   (((uint64_t)(va) >> PD_SHIFT)   & PD_MASK)
#define PT_INDEX(va)   (((uint64_t)(va) >> PT_SHIFT)   & PT_MASK)
#define OFFSET(va)     ((uint64_t)(va) & OFFSET_MASK)

#define PAGE_ROUND_UP(x)   (((x) + PGSIZE - 1) & ~(PGSIZE - 1))

// types
typedef uint64_t pte_t;
typedef uint64_t pde_t;

// flags
#define PTE_PRESENT    0x1
#define PTE_WRITABLE   0x2
#define PTE_USER       0x4

// TLB
#define TLB_ENTRIES    512

struct tlb {
    uint64_t vpn[TLB_ENTRIES];
    uint64_t pfn[TLB_ENTRIES];
    bool     valid[TLB_ENTRIES];
    uint64_t last_used[TLB_ENTRIES];
};

extern struct tlb tlb_store;

//  func prototypes
void set_physical_mem(void);
int TLB_add(void *va, void *pa);
pte_t *TLB_check(void *va);
void print_TLB_missrate(void);
pte_t *translate(pde_t *pgdir, void *va);
int map_page(pde_t *pgdir, void *va, void *pa);
void *get_next_avail(int num_pages);
void *n_malloc(unsigned int num_bytes);
void n_free(void *va, int size);
int put_data(void *va, void *val, int size);
void get_data(void *va, void *val, int size);
void mat_mult(void *mat1, void *mat2, int size, void *answer);

#endif // MY_VM64_H_INCLUDED
