//
// libmimalloc / hiktrack.hh
// Created by corgi on 2025-09-12.
//

#pragma once
#include <atomic>

// 所有翻译单元都能看到这份声明
extern std::atomic<bool> g_init_done;
