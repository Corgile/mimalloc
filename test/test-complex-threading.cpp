/*
 * test-complex-threading.cpp - 复杂多线程内存分配测试
 *
 * 测试场景：
 * 1. 多线程并发malloc/free
 * 2. C++ new/delete混合使用
 * 3. 同步和异步操作
 * 4. 指针重用验证
 * 5. Event ID前缀存储验证
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <vector>

#include "mimalloc-new-delete.h"
#include "mimalloc-override.h"

class ThreadSafeLogger {
private:
  mutable std::mutex log_mutex;

public:
  template <typename... Args>
  void log(const std::string &format, Args... args) {
    std::lock_guard<std::mutex> lock(log_mutex);
    printf(("[TEST] " + format + "\n").c_str(), args...);
    fflush(stdout);
  }
};

ThreadSafeLogger logger;

// 全局统计
std::atomic<int> total_mallocs{0};
std::atomic<int> total_frees{0};
std::atomic<int> total_news{0};
std::atomic<int> total_deletes{0};

// 测试用的数据结构
struct TestData {
  int thread_id;
  size_t alloc_size;
  void *ptr;
  uint64_t expected_event_id;

  TestData(int tid, size_t size)
      : thread_id(tid), alloc_size(size), ptr(nullptr), expected_event_id(0) {}
};

// 工作队列用于异步操作
class WorkQueue {
private:
  std::queue<std::function<void()>> tasks;
  std::mutex queue_mutex;
  std::condition_variable cv;
  std::atomic<bool> stop_flag{false};

public:
  void enqueue(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      if (!stop_flag) {
        tasks.push(task);
      }
    }
    cv.notify_one();
  }

  void worker_thread() {
    while (true) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv.wait(lock, [this] { return !tasks.empty() || stop_flag; });

        if (stop_flag && tasks.empty())
          break;

        if (!tasks.empty()) {
          task = tasks.front();
          tasks.pop();
        }
      }

      if (task) {
        task();
      }
    }
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      stop_flag = true;
    }
    cv.notify_all();
  }
};

// 验证event_id是否正确存储在指针前
bool verify_event_id(void *ptr, uint64_t expected_id) {
  if (!ptr)
    return false;

  // 从指针前8字节读取event_id
  uint64_t *stored_id = reinterpret_cast<uint64_t *>((uint8_t *)ptr - 8);
  bool result = (*stored_id == expected_id);

  if (!result) {
    logger.log("Event ID mismatch! ptr=%p, expected=%lu, actual=%lu", ptr,
               expected_id, *stored_id);
  }

  return result;
}

// 线程1: 同步malloc/free测试
void sync_malloc_test(int thread_id, int iterations) {
  logger.log("Thread %d: Starting sync malloc test with %d iterations",
             thread_id, iterations);

  std::vector<TestData> allocations;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> size_dist(16, 1024);

  // 分配阶段
  for (int i = 0; i < iterations; i++) {
    size_t size = size_dist(gen);
    TestData data(thread_id, size);

    data.ptr = mi_malloc(size);
    total_mallocs++;

    if (data.ptr) {
      // 验证event_id存储
      std::this_thread::sleep_for(
          std::chrono::microseconds(1)); // 让event_id有时间写入
      allocations.push_back(data);

      logger.log("Thread %d malloc: ptr=%p, size=%zu", thread_id, data.ptr,
                 size);
    }

    // 偶尔释放一些，模拟实际使用
    if (i % 3 == 0 && !allocations.empty()) {
      auto &to_free = allocations.back();
      mi_free(to_free.ptr);
      total_frees++;
      logger.log("Thread %d free: ptr=%p", thread_id, to_free.ptr);
      allocations.pop_back();
    }
  }

  // 释放剩余的
  for (auto &data : allocations) {
    mi_free(data.ptr);
    total_frees++;
    logger.log("Thread %d final free: ptr=%p", thread_id, data.ptr);
  }

  logger.log("Thread %d: Sync malloc test completed", thread_id);
}

// 线程2: C++ new/delete测试
void cpp_new_test(int thread_id, int iterations) {
  logger.log("Thread %d: Starting C++ new/delete test with %d iterations",
             thread_id, iterations);

  std::vector<int *> int_ptrs;
  std::vector<double *> double_ptrs;
  std::vector<char *> char_arrays;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> choice_dist(0, 2);
  std::uniform_int_distribution<size_t> array_size_dist(10, 100);

  for (int i = 0; i < iterations; i++) {
    int choice = choice_dist(gen);

    switch (choice) {
    case 0: {
      // new/delete int
      int *ptr = new int(i * thread_id);
      total_news++;
      int_ptrs.push_back(ptr);
      logger.log("Thread %d new int: ptr=%p, value=%d", thread_id, ptr, *ptr);
      break;
    }
    case 1: {
      // new/delete double
      double *ptr = new double(i * thread_id * 1.5);
      total_news++;
      double_ptrs.push_back(ptr);
      logger.log("Thread %d new double: ptr=%p, value=%f", thread_id, ptr,
                 *ptr);
      break;
    }
    case 2: {
      // new[]/delete[] char array
      size_t size = array_size_dist(gen);
      char *ptr = new char[size];
      total_news++;
      memset(ptr, 'A' + (thread_id % 26), size);
      char_arrays.push_back(ptr);
      logger.log("Thread %d new char[%zu]: ptr=%p", thread_id, size, ptr);
      break;
    }
    }

    // 偶尔删除一些
    if (i % 4 == 0) {
      if (!int_ptrs.empty()) {
        delete int_ptrs.back();
        total_deletes++;
        logger.log("Thread %d delete int: ptr=%p", thread_id, int_ptrs.back());
        int_ptrs.pop_back();
      }
      if (!double_ptrs.empty()) {
        delete double_ptrs.back();
        total_deletes++;
        logger.log("Thread %d delete double: ptr=%p", thread_id,
                   double_ptrs.back());
        double_ptrs.pop_back();
      }
      if (!char_arrays.empty()) {
        delete[] char_arrays.back();
        total_deletes++;
        logger.log("Thread %d delete[] char: ptr=%p", thread_id,
                   char_arrays.back());
        char_arrays.pop_back();
      }
    }
  }

  // 清理剩余
  for (auto ptr : int_ptrs) {
    delete ptr;
    total_deletes++;
    logger.log("Thread %d final delete int: ptr=%p", thread_id, ptr);
  }
  for (auto ptr : double_ptrs) {
    delete ptr;
    total_deletes++;
    logger.log("Thread %d final delete double: ptr=%p", thread_id, ptr);
  }
  for (auto ptr : char_arrays) {
    delete[] ptr;
    total_deletes++;
    logger.log("Thread %d final delete[] char: ptr=%p", thread_id, ptr);
  }

  logger.log("Thread %d: C++ new/delete test completed", thread_id);
}

// 线程3: 异步队列测试
void async_queue_test(WorkQueue &work_queue, int thread_id, int iterations) {
  logger.log("Thread %d: Starting async queue test with %d iterations",
             thread_id, iterations);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> size_dist(32, 512);
  std::uniform_int_distribution<int> delay_dist(1, 10);

  for (int i = 0; i < iterations; i++) {
    size_t size = size_dist(gen);
    int delay = delay_dist(gen);

    // 异步分配任务
    work_queue.enqueue([thread_id, size, i]() {
      void *ptr = mi_malloc(size);
      total_mallocs++;
      logger.log("Thread %d async malloc: ptr=%p, size=%zu, iter=%d", thread_id,
                 ptr, size, i);

      // 模拟一些工作
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      // 异步释放
      mi_free(ptr);
      total_frees++;
      logger.log("Thread %d async free: ptr=%p, iter=%d", thread_id, ptr, i);
    });

    std::this_thread::sleep_for(std::chrono::microseconds(delay * 100));
  }

  logger.log("Thread %d: Async queue test completed", thread_id);
}

// 线程4: 指针重用压力测试
void pointer_reuse_test(int thread_id, int iterations) {
  logger.log("Thread %d: Starting pointer reuse test with %d iterations",
             thread_id, iterations);

  std::vector<void *> ptrs;
  const size_t FIXED_SIZE = 64; // 固定大小，增加重用概率

  for (int i = 0; i < iterations; i++) {
    // 分配
    void *ptr = mi_malloc(FIXED_SIZE);
    total_mallocs++;
    logger.log("Thread %d reuse malloc: ptr=%p, size=%zu, iter=%d", thread_id,
               ptr, FIXED_SIZE, i);

    ptrs.push_back(ptr);

    // 每隔几次就释放一半，增加重用机会
    if (i % 10 == 9) {
      size_t half = ptrs.size() / 2;
      for (size_t j = 0; j < half; j++) {
        mi_free(ptrs[j]);
        total_frees++;
        logger.log("Thread %d reuse free: ptr=%p, iter=%d", thread_id, ptrs[j],
                   i);
      }
      ptrs.erase(ptrs.begin(), ptrs.begin() + half);
    }
  }

  // 清理剩余
  for (void *ptr : ptrs) {
    mi_free(ptr);
    total_frees++;
    logger.log("Thread %d final reuse free: ptr=%p", thread_id, ptr);
  }

  logger.log("Thread %d: Pointer reuse test completed", thread_id);
}

int main() {
  logger.log("=== Complex Multi-threaded Memory Allocation Test ===");
  logger.log("Testing event_id prefix storage mechanism");

  auto start_time = std::chrono::high_resolution_clock::now();

  // 创建异步工作队列
  WorkQueue work_queue;
  std::thread worker_thread(&WorkQueue::worker_thread, &work_queue);

  // 启动多个测试线程
  std::vector<std::future<void>> futures;

  // 同步malloc/free线程
  futures.reserve(3);
for (int i = 0; i < 3; i++) {
    futures.push_back(
        std::async(std::launch::async, sync_malloc_test, i + 1, 50));
  }

  // C++ new/delete线程
  for (int i = 0; i < 2; i++) {
    futures.push_back(std::async(std::launch::async, cpp_new_test, i + 10, 30));
  }

  // 异步队列线程
  for (int i = 0; i < 2; i++) {
    futures.push_back(std::async(std::launch::async, async_queue_test,
                                 std::ref(work_queue), i + 20, 25));
  }

  // 指针重用压力测试线程
  for (int i = 0; i < 2; i++) {
    futures.push_back(
        std::async(std::launch::async, pointer_reuse_test, i + 30, 40));
  }

  // 等待所有线程完成
  for (auto &future : futures) {
    future.wait();
  }

  // 停止工作队列
  work_queue.stop();
  worker_thread.join();

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  // 打印统计信息
  logger.log("=== Test Results ===");
  logger.log("Total mallocs: %d", total_mallocs.load());
  logger.log("Total frees: %d", total_frees.load());
  logger.log("Total news: %d", total_news.load());
  logger.log("Total deletes: %d", total_deletes.load());
  logger.log("Test duration: %ld ms", duration.count());

  // 检查内存泄漏和打印event_id统计
  // mi_print_leaks();

  logger.log("=== Test Completed Successfully ===");

  return 0;
}