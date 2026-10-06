#include "applestia_shapes_helper_api.h"
#include "applestia_shapes_loader_path.h"
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>

static void* load(const char* source, const char* name, int distinct_path) {
    int file = open(source, O_RDONLY);
    assert(file >= 0);
    int fd = memfd_create(name, MFD_CLOEXEC);
    assert(fd >= 0);
    char data[8192];
    ssize_t n;
    while ((n = read(file, data, sizeof(data))) > 0)
        assert(write(fd, data, n) == n);
    assert(n == 0);
    close(file);
    char path[64];
    if (distinct_path)
        applestia_shapes_loader_path(path, sizeof(path), fd);
    else
        snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    void* handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
    assert(handle);
    close(fd);
    return handle;
}
int main(void) {
    void* item = load("src/item-helper/hyprglass-item-helper.so", "item", 0);
    void* wrong = load("src/shapes-helper/applestia-shapes-helper.so", "shapes-collision", 0);
    assert(item == wrong);
    assert(!dlsym(wrong, "applestia_shapes_helper_v1_api"));
    void* shapes = load("src/shapes-helper/applestia-shapes-helper.so", "shapes-distinct", 1);
    assert(shapes != item);
    const struct applestia_shapes_helper_v1_api* api = dlsym(shapes, "applestia_shapes_helper_v1_api");
    assert(api && api->abi_version == APPLESTIA_SHAPES_HELPER_ABI_VERSION);
    assert(dlclose(shapes) == 0);
    assert(dlsym(RTLD_DEFAULT, "applestia_shapes_helper_v1_api") == api);
    api->stop(); // callback/function code survives close, not just the data symbol
    puts("shapes-loader: reproduced fd-path collision; distinct path and persistent ABI adoption passed");
}
