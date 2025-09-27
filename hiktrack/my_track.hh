//
// libmimalloc / my_track.hh
// Created by corgi on 2025-09-13.
//

#pragma once
/*  ----------  真实函数  ----------  */
extern "C" {
void my_track_init();
void my_track_malloc(const void *p, size_t req, size_t size, bool zero);
void my_track_free(const void *p, size_t size);
/* 在文件末尾追加：让测试程序能拿到泄漏表 */
void mi_print_leaks(void);
// 打印剩余分配
}
