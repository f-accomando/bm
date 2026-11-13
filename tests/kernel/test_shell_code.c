/*
 * Host unit test for Shell Code (core/src/kernel/shell_code.c)
 */
#include <stdio.h>
#include <stdlib.h>

#include "kernel/shell_code.h"

int main(void)
{
    int rc = shell_code_test_run();
    if (rc != 0) {
        printf("test_shell_code: FAILED with code %d\n", rc);
        return 1;
    }
    printf("test_shell_code: OK\n");
    return 0;
}
