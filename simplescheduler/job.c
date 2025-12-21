#include <stdio.h>
#include "dummy_main.h"

int dummy_main(int argc, char **argv) {
    printf("Job started: PID %d\n", getpid());

    for (int i = 0; i < 5; i++) {
        printf("Job running iteration %d\n", i + 1);
        sleep(1);
    }

    printf("Job finished: PID %d\n", getpid());
    return 0;
}
