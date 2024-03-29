#ifndef PANIC_H
#define PANIC_H

#include <stdio.h>
#include <stdlib.h>

static void panic(const char *reason) {
    fprintf(stderr, "%s\n", reason);
    __builtin_debugtrap();	// WAHE edit
    abort();
}

#endif /* PANIC_H */
