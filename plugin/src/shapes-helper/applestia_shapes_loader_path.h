#ifndef APPLESTIA_SHAPES_LOADER_PATH_H
#define APPLESTIA_SHAPES_LOADER_PATH_H
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

// dlopen caches literal paths even after their memfds are closed. ItemHints
// uses /proc/self/fd/N; using the same spelling with a reused N loads the item
// helper AGAIN, not the new shapes memfd. Use a distinct loader pathname.
static inline void applestia_shapes_loader_path(char* path, size_t size, int fd) {
    snprintf(path, size, "/proc/%ld/fd/%d", (long)getpid(), fd);
}
#endif
