#include "app.h"
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        INFO("usage: grep [PATTERN] [FILE]");
        INFO("       grep 'multi word pattern' file");
        return -1;
    }

    // Build pattern from all arguments except the last one
    static char pattern[256];
    pattern[0] = '\0';
    
    // Combine all arguments except the last one (filename) into pattern
    for (int i = 1; i < argc - 1; i++) {
        // Add space between words (but not before the first word)
        if (i > 1) {
            strcat(pattern, " ");
        }
        strcat(pattern, argv[i]);
    }

    char *filename = argv[argc - 1];

    // Look up inode of file
    int file_ino = dir_lookup(workdir_ino, filename);
    if (file_ino < 0) {
        INFO("grep: %s: file not found", filename);
        return -1;
    }

    // Read only the first block (like cat does)
    char buf[BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    file_read(file_ino, 0, buf);

    // Split into lines and search
    char *line = strtok(buf, "\n");
    int found_any = 0;
    
    while (line != NULL) {
        if (strstr(line, pattern) != NULL) {
            printf("%s\n\r", line);
            found_any = 1;
        }
        line = strtok(NULL, "\n");
    }

    return found_any ? 0 : 1;
}