//
// libmimalloc / test_leak.cc
// Created by corgi on 2025-09-13.
//
#include <mimalloc-override.h>

#include <stdio.h>

/* 纯 C 接口 */
// #include "mimalloc/leak_c.h"

/* 为了使用 mi_xxx 函数，再 include 纯 C 的 mimalloc.h */

int main(void) {
  printf("----- start leak test -----\n");

  int *p1 = malloc(100 * sizeof(int));
  free(p1); /* 正常释放 */

  int *p2 = malloc(200 * sizeof(int)); /* 800 字节泄漏 */
  (void)p2;

  double *p3 = malloc(64 * sizeof(double)); /* 512 字节泄漏 */
  (void)p3;

  printf("track init should appear below:\n");
  mi_print_leaks(); /* 打印存活块 */
  return 0;
}