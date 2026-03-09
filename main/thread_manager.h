#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t thread_manager_init(void);
void thread_manager_start(void);

#ifdef __cplusplus
}
#endif

#endif // THREAD_MANAGER_H
