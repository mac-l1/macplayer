#ifndef __LOG_H
#define __LOG_H

#define DBG(fmt, ...) do { printf("D(%p): %s:%d " fmt "\n", pthread_self(),  __func__, __LINE__, ##__VA_ARGS__); } while (0)
#define ERROR(fmt, ...) do { printf("E(%p): %s:%d " fmt "\n", pthread_self(),  __func__, __LINE__, ##__VA_ARGS__); } while (0)
#define INFO(fmt, ...) do { printf("I(%p): %s:%d " fmt "\n", pthread_self(), __func__, __LINE__, ##__VA_ARGS__); } while (0)

#endif

