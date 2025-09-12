//
// libmimalloc / hiktrack.cc
// Created by corgi on 2025-09-12.
//
// --------------  必须在任何 mimalloc 头之前  --------------
#include "hiktrack.hh"
#include "mimalloc/leak_c.h" // 瀵煎嚭 C 鎺ュ彛
#include "mimalloc/my_track.hh"

#include <mutex>
#include <unordered_map>

/* ---------- 泄漏检测表 ---------- */
static std::mutex g_leak_mx;
static std::unordered_map<const void *, size_t> g_leak_map; // addr -> size
alignas(64) std::atomic<bool> g_init_done{false};           // 唯一定义

/* ---------- 外部 TBB 扩展（不再使用，留空即可） ---------- */
void track_extra_init() {}
void track_extra_malloc(const void *, size_t) {}
void track_extra_free(const void *, size_t) {}
void track_extra_realloc(const void *, size_t, const void *, size_t) {}

/* ---------- 三个 C 接口 ---------- */
// extern "C" {

void my_track_init() { printf("[LEAK] track init\n"); }

void my_track_malloc(const void *p, size_t req, size_t size, bool zero) {
  if (!p)
    return;
  std::lock_guard<std::mutex> lk(g_leak_mx);
  g_leak_map[p] = size;
  (void)req;
  (void)zero;
}

void my_track_free(const void *p, size_t size) {
  if (!p)
    return;
  std::lock_guard lk(g_leak_mx);
  g_leak_map.erase(p);
  (void)size;
}

/* 测试程序退出前调用：打印所有未被 free 的分配 */
void mi_print_leaks() {
  std::lock_guard lk(g_leak_mx);
  if (g_leak_map.empty()) {
    printf("[LEAK] ******  no leak detected  ******\n");
    return;
  }
  printf("[LEAK] ******  detected %zu leaks  ******\n", g_leak_map.size());
  for (const auto &[ptr, sz] : g_leak_map)
    printf("  leak @ %p  size=%zu\n", ptr, sz);
}

// } // extern "C"
