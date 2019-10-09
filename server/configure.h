#ifndef SMS_SERVER_CONFIGURE_H
#define SMS_SERVER_CONFIGURE_H

namespace flinter {
class Tree;
} // namespace flinter

extern const flinter::Tree *g_configure;

extern int configure_load(const char *filename);
extern void configure_destroy(void);

#endif // SMS_SERVER_CONFIGURE_H
