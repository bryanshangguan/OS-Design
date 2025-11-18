#include "my_vm.h"
#include <stdio.h>

/* Stub bitmap implementations to satisfy linker. Real logic can move into my_vm.c later. */
int bitmap_get_next_free(int start_bit, int total_bits) {
    printf("[bitmap_get_next_free] start=%d total=%d (stub)\n", start_bit, total_bits);
    return -1; // no free found (stub)
}
int bitmap_test(int index, unsigned char *bitmap) {
    (void)bitmap; // unused
    return 0; // always free (stub)
}
void bitmap_set(int index, unsigned char *bitmap) {
    (void)index; (void)bitmap; // stub
}
void bitmap_clear(int index, unsigned char *bitmap) {
    (void)index; (void)bitmap; // stub
}
