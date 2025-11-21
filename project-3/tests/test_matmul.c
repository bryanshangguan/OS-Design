#include "../my_vm.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== TEST: mat_mult ===\n");

    int n = 2;
    int bytes = n * n * sizeof(int);

    // allocate space for matrices
    void *A = n_malloc(bytes);
    void *B = n_malloc(bytes);
    void *C = n_malloc(bytes);

    int matA[] = { 1, 2,
                   3, 4 };

    int matB[] = { 2, 0,
                   1, 2 };

    int expected[] = {
        1*2 + 2*1,    1*0 + 2*2,
        3*2 + 4*1,    3*0 + 4*2
    };

    // put data
    put_data(A, matA, bytes);
    put_data(B, matB, bytes);

    // perform multiplication
    mat_mult(A, B, n, C);

    // read out C
    int result[4];
    get_data(C, result, bytes);

    printf("Result matrix:\n");
    printf("%d %d\n%d %d\n",
           result[0], result[1],
           result[2], result[3]);

    // check correctness
    if (memcmp(result, expected, bytes) == 0)
        printf("PASS: mat_mult produced correct result\n");
    else
        printf("FAIL: result does not match expected\n");

    return 0;
}
