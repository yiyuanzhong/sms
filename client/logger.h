#ifndef SMS_LOGGER_H
#define SMS_LOGGER_H

extern int logger_initialize(const char *filename);
extern int logger_shutdown(void);

extern int logger_fatal(const char *file, int line, const char *fmt, ...);
extern int logger_error(const char *file, int line, const char *fmt, ...);
extern int logger_warn (const char *file, int line, const char *fmt, ...);
extern int logger_info (const char *file, int line, const char *fmt, ...);
extern int logger_trace(const char *file, int line, const char *fmt, ...);
extern int logger_debug(const char *file, int line, const char *fmt, ...);

#define LOGF(fmt, ...) logger_fatal(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define LOGE(fmt, ...) logger_error(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define LOGW(fmt, ...) logger_warn (__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define LOGI(fmt, ...) logger_info (__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define LOGT(fmt, ...) logger_trace(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define LOGD(fmt, ...) logger_debug(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)

#endif /* SMS_LOGGER_H */
