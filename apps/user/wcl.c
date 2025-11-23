#include "app.h"
#include <string.h>

#define BLOCK_SIZE 512

int main(int argc, char **argv) {
    if (argc < 2) {
        INFO("usage: wcl [FILE1] [FILE2] ...");
        return -1;
    }

    int total_lines = 0;
    char buf[BLOCK_SIZE];

    for (int f = 1; f < argc; f++) {
        char *filename = argv[f];
        int file_ino = dir_lookup(workdir_ino, filename);
        if (file_ino < 0) {
            INFO("wcl: file %s not found", filename);
            continue;
        }

        // Read only the first block
        memset(buf, 0, BLOCK_SIZE);
        int ret = file_read(file_ino, 0, buf);
        
        // If read failed or file is empty, skip counting
        if (ret < 0) {
            continue;
        }

        int lines = 0;
        int has_content = 0;
        
        for (int i = 0; i < BLOCK_SIZE && buf[i] != '\0'; i++) {
            if (buf[i] == '\n') {
                lines++;
                has_content = 0;
            } else if (buf[i] != '\r' && buf[i] != '\0') {
                has_content = 1;
            }
        }
        
        // Count the last line if it has content
        if (has_content) {
            lines++;
        }

        total_lines += lines;
    }

    printf("%d\n\r", total_lines);
    return 0;
}

