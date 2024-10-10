#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

struct ExactObjectListBase {
  bool used;
};

/*
 * Taken mostly from http://graphics.stanford.edu/~seander/bithacks.html
 * Returns the Count of Trailing Zeroes in v (the index of the least significant
 * set bit). Returns -1 when v == 0.
 */
inline int ctz(uint32_t v) {
  if (!v)
    return -1;

  static const int MultiplyDeBruijnBitPosition[32] = {
      0,  1,  28, 2,  29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4,  8,
      31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6,  11, 5,  10, 9};

  return MultiplyDeBruijnBitPosition
      [static_cast<uint32_t>(((v & -v) * 0x077CB531UL)) >> 27];
}

template <typename T, int Limit>
struct ExactObjectList {
  struct range {
    range(T* cur, T* end) : cur(cur), end(end) {}

    T* next() {
      while (!cur->used)
        ++cur;

      T* ret = cur;
      ++cur;

      return ret == end ? 0 : ret;
    }

    T* cur;
    T* end;
  };

  ExactObjectList() { clear(); }

  T* getFreeObject() {
    assert(count < Limit);
    ++count;

    T* ptr = 0;
    for (uint32_t i = 0; i < FreeListSize; ++i) {
      if (freeList[i] != 0) {
        int bit = ctz(freeList[i]);
        uint32_t index = (i << 5) + bit;
        ptr = arr + index;
        freeList[i] &= ~(static_cast<uint32_t>(1) << bit);
        break;
      }
    }

    assert(ptr && !ptr->used && ptr >= arr && ptr < arr + Limit);
    ptr->used = true;

    return ptr;
  }

  T* newObjectReuse() {
    T* ret;
    if (count >= Limit)
      ret = &arr[Limit - 1];
    else
      ret = getFreeObject();

    assert(ret->used && ret >= arr && ret < arr + Limit);
    return ret;
  }

  T* newObject() {
    if (count >= Limit)
      return 0;

    T* ret = getFreeObject();
    assert(ret->used && ret >= arr && ret < arr + Limit);
    return ret;
  }

  range all() { return range(&arr[0], &arr[Limit]); }

  void free(T* ptr) {
    assert(ptr->used);
    if (ptr->used) {
      uint32_t index = static_cast<uint32_t>(ptr - arr);
      freeList[index >> 5] |= (static_cast<uint32_t>(1) << (index & 31));

      ptr->used = false;

      assert(count > 0);
      --count;
    }
  }

  void free(range& r) { free(r.cur - 1); }

  void clear() {
    std::memset(freeList, 0xff, FreeListSize * sizeof(uint32_t));
    count = 0;

    for (std::size_t i = 0; i < Limit; ++i)
      arr[i].used = false;

    arr[Limit].used = true;

    // Mark padding as used
    for (uint32_t index = Limit; index < FreeListSize * 32; ++index)
      freeList[index >> 5] &= ~(static_cast<uint32_t>(1) << (index & 31));
  }

  std::size_t size() const { return count; }

  T arr[Limit + 1];  // Sentinel

  static uint32_t const FreeListSize = (Limit + 31) / 32;
  uint32_t freeList[FreeListSize];

  std::size_t count;
};
