#ifndef LIB_H
#define LIB_H
// DO NOT EDIT - file will not be copied in submission.

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>

typedef unsigned int uint;

#define DATA_BLOCK_SIZE (1<<12)
#define META_BLOCK_SIZE (1<<10)

void ls_print_file(char * fileName, uint size, mode_t permissions, time_t mtime, uint depth);

#endif
