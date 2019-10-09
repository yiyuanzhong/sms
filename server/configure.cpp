#include "sms/server/configure.h"

#include <flinter/types/tree.h>
#include <flinter/cmdline.h>

const flinter::Tree *g_configure;

int configure_load(const char *filename)
{
    char *path = cmdline_get_absolute_path(filename, 0);
    if (!path) {
        return -1;
    }

    flinter::Tree *const t = new flinter::Tree;
    if (!t->ParseFromHdfFile(path)) {
        free(path);
        return -1;
    }

    free(path);
    g_configure = t;
    return 0;
}

void configure_destroy(void)
{
    delete g_configure;
    g_configure = nullptr;
}
