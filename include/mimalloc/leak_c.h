//
// libmimalloc / leak_c.h
// Created by corgi on 2025-09-13.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

  /* Windows：直接导出，Linux：空 */
#ifdef _WIN32
#  define MI_LEAK_API  __declspec(dllexport)
#else
#  define MI_LEAK_API
#endif

  MI_LEAK_API void mi_print_leaks(void);

#ifdef __cplusplus
}
#endif
