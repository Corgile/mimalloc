//
// libmimalloc / hiktrack.cc
// Created by corgi on 2025-09-12.
//
// --------------  必须在任何 mimalloc 头之前  --------------
#include "hiktrack.hh"
#include "../../hiktrack/my_track.hh"

#include <mutex>
#include <unordered_map>

/* ---------- Event ID metadata (存储在指针前8字节) ---------- */
struct EventMetadata { uint64_t event_id; };

/* ---------- 全局状态 ---------- */
static std::mutex g_track_mutex;
static std::atomic<uint64_t> g_next_event_id{1};
static std::unordered_map<uint64_t, uint64_t> g_active_records;

static EventMetadata *get_event_metadata(const void *p) {
  return reinterpret_cast<EventMetadata *>((uint8_t *)(p) -
                                           sizeof(EventMetadata));
}

/* ---------- 外部 TBB 扩展（不再使用，留空即可） ---------- */
void track_extra_init() {}
void track_extra_malloc(const void *, size_t) {}
void track_extra_free(const void *, size_t) {}
void track_extra_realloc(const void *, size_t, const void *, size_t) {}

/* ---------- 三个 C 接口 ---------- */
extern "C" {

void my_track_init() { printf("[TRACK] Enhanced tracking with event_id init\n"); }

void my_track_malloc(const void *p, size_t req, size_t size, bool zero) {
  if (!p)
    return;
  // 生成唯一event_id
  uint64_t event_id = g_next_event_id.fetch_add(1);
  // 在指针前8字节写入event_id
  EventMetadata *meta = get_event_metadata(p);
  meta->event_id = event_id;
  printf("[TRACK] malloc: p=%p, event_id=%llu, req=%zu, size=%zu\n", p, event_id, req, size);
  std::lock_guard lock(g_track_mutex);
  g_active_records[meta->event_id]++;
  (void)zero;
}

void my_track_free(const void *p, size_t size) {
  if (!p) return;
  // 从指针前8字节读取event_id
  EventMetadata *meta = get_event_metadata(p);
  uint64_t event_id = meta->event_id;

  printf("[TRACK] free: p=%p, event_id=%llu, size=%zu\n", p, event_id, size);
  meta->event_id = 0;
  std::lock_guard lock(g_track_mutex);
  if (g_active_records.contains(meta->event_id)) {
    g_active_records[meta->event_id]--;
    if (g_active_records[meta->event_id] == 0) {
      g_active_records.erase(meta->event_id);
    }
  } else { printf("[TRACK] INVALID ID: %llu, size=%zu\n", event_id, size); }
  (void)size;
}

void mi_print_leaks() {
  printf("[TRACK] ******  Event ID tracking test completed  ******\n");
  printf("[TRACK] Total allocated event IDs: %llu\n", g_next_event_id.load() - 1);
}
} // extern "C"
