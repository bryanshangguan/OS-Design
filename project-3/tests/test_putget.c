#include "../my_vm.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== TEST: put_data + get_data across pages ===\n");

    // Allocate 6000 bytes so we cross a page boundary
    void *ptr = n_malloc(6000);

    char write_buf[6000];
    char read_buf[6000];

    // Fill write buffer with pattern
    for (int i = 0; i < 6000; i++)
        write_buf[i] = (char)(i % 256);

    // Write
    if (put_data(ptr, write_buf, 6000) != 0) {
        printf("FAIL: put_data returned error\n");
        return 0;
    }

    // Read back
    memset(read_buf, 0, sizeof(read_buf));
    get_data(ptr, read_buf, 6000);

    // Compare
    if (memcmp(write_buf, read_buf, 6000) == 0)
        printf("PASS: Data matched across page boundary\n");
    else
        printf("FAIL: Data mismatch\n");

    return 0;
}
