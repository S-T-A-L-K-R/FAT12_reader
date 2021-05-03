#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "file_reader.h"


int main(void)
{
    struct disk_t* my_disk = disk_open_from_file("fat12_c1.bin");

    disk_close(my_disk);
    return 0;
}


