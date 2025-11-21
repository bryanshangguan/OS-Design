#include "../my_vm.h"
#include <stdio.h>

int main() {
    printf("=== TEST: n_malloc + n_free ===\n");

    // allocate 8000 bytes (~2 pages)
    void *ptr = n_malloc(8000);
    if (!ptr) { printf("FAIL: n_malloc returned NULL\n"); return 0; }

    printf("Allocated virtual address: %p\n", ptr);

    // allocate a second block
    void *ptr2 = n_malloc(12000);
    if (!ptr2) { printf("FAIL: second n_malloc returned NULL\n"); return 0; }

    printf("Allocated second block: %p\n", ptr2);

    // free both
    n_free(ptr, 8000);
    n_free(ptr2, 12000);

    printf("Freed both successfully.\n");
    printf("If no crash → PASS\n");

    return 0;
}
