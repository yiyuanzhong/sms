#ifndef SMS_CONFIGURE_H
#define SMS_CONFIGURE_H

struct configure {
    char *handshake;
    char *device;
    int baudrate;

    char *hostname;
    char *url;
    char *cainfo;
    char *token;
};

extern struct configure *configure_create(const char *path);
extern void configure_free(struct configure *c);

#endif /* SMS_CONFIGURE_H */
