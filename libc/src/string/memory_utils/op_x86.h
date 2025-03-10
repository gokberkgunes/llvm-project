//===-- x86 implementation of memory function building blocks -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides x86 specific building blocks to compose memory functions.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_STRING_MEMORY_UTILS_OP_X86_H
#define LLVM_LIBC_SRC_STRING_MEMORY_UTILS_OP_X86_H

#include "src/__support/macros/architectures.h"

#if defined(LIBC_TARGET_IS_X86_64)

#include "src/__support/common.h"
#include "src/string/memory_utils/op_builtin.h"
#include "src/string/memory_utils/op_generic.h"

#if defined(__AVX512BW__) || defined(__AVX512F__) || defined(__AVX2__) ||      \
    defined(__SSE2__)
#include <immintrin.h>
#endif

// Define fake functions to prevent the compiler from failing on undefined
// functions in case the CPU extension is not present.
#if !defined(__AVX512BW__) && (defined(_MSC_VER) || defined(__SCE__))
#define _mm512_cmpneq_epi8_mask(A, B) 0
#endif
#if !defined(__AVX2__) && (defined(_MSC_VER) || defined(__SCE__))
#define _mm256_movemask_epi8(A) 0
#endif
#if !defined(__SSE2__) && (defined(_MSC_VER) || defined(__SCE__))
#define _mm_movemask_epi8(A) 0
#endif

namespace __llvm_libc::x86 {

// A set of constants to check compile time features.
static inline constexpr bool kSse2 = LLVM_LIBC_IS_DEFINED(__SSE2__);
static inline constexpr bool kAvx = LLVM_LIBC_IS_DEFINED(__AVX__);
static inline constexpr bool kAvx2 = LLVM_LIBC_IS_DEFINED(__AVX2__);
static inline constexpr bool kAvx512F = LLVM_LIBC_IS_DEFINED(__AVX512F__);
static inline constexpr bool kAvx512BW = LLVM_LIBC_IS_DEFINED(__AVX512BW__);

///////////////////////////////////////////////////////////////////////////////
// Memcpy repmovsb implementation
struct Memcpy {
  static void repmovsb(void *dst, const void *src, size_t count) {
    asm volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(count) : : "memory");
  }
};

///////////////////////////////////////////////////////////////////////////////
// Bcmp

// Base implementation for the Bcmp specializations.
//  - BlockSize is either 16, 32 or 64 depending on the available compile time
// features, it is used to switch between "single native operation" or a
// "sequence of native operations".
//  - BlockBcmp is the function that implements the bcmp logic.
template <size_t Size, size_t BlockSize, auto BlockBcmp> struct BcmpImpl {
  static constexpr size_t SIZE = Size;
  LIBC_INLINE static BcmpReturnType block(CPtr p1, CPtr p2) {
    if constexpr (Size == BlockSize) {
      return BlockBcmp(p1, p2);
    } else if constexpr (Size % BlockSize == 0) {
      for (size_t offset = 0; offset < Size; offset += BlockSize)
        if (auto value = BlockBcmp(p1 + offset, p2 + offset))
          return value;
    } else {
      deferred_static_assert("SIZE not implemented");
    }
    return BcmpReturnType::ZERO();
  }

  LIBC_INLINE static BcmpReturnType tail(CPtr p1, CPtr p2, size_t count) {
    return block(p1 + count - Size, p2 + count - Size);
  }

  LIBC_INLINE static BcmpReturnType head_tail(CPtr p1, CPtr p2, size_t count) {
    return block(p1, p2) | tail(p1, p2, count);
  }

  LIBC_INLINE static BcmpReturnType loop_and_tail(CPtr p1, CPtr p2,
                                                  size_t count) {
    static_assert(Size > 1, "a loop of size 1 does not need tail");
    size_t offset = 0;
    do {
      if (auto value = block(p1 + offset, p2 + offset))
        return value;
      offset += Size;
    } while (offset < count - Size);
    return tail(p1, p2, count);
  }
};

namespace sse2 {
LIBC_INLINE BcmpReturnType bcmp16(CPtr p1, CPtr p2) {
#if defined(__SSE2__)
  using T = char __attribute__((__vector_size__(16)));
  // A mask indicating which bytes differ after loading 16 bytes from p1 and p2.
  const int mask =
      _mm_movemask_epi8(cpp::bit_cast<__m128i>(load<T>(p1) != load<T>(p2)));
  return static_cast<uint32_t>(mask);
#else
  (void)p1;
  (void)p2;
  return BcmpReturnType::ZERO();
#endif // defined(__SSE2__)
}
template <size_t Size> using Bcmp = BcmpImpl<Size, 16, bcmp16>;
} // namespace sse2

namespace avx2 {
LIBC_INLINE BcmpReturnType bcmp32(CPtr p1, CPtr p2) {
#if defined(__AVX2__)
  using T = char __attribute__((__vector_size__(32)));
  // A mask indicating which bytes differ after loading 32 bytes from p1 and p2.
  const int mask =
      _mm256_movemask_epi8(cpp::bit_cast<__m256i>(load<T>(p1) != load<T>(p2)));
  // _mm256_movemask_epi8 returns an int but it is to be interpreted as a 32-bit
  // mask.
  return static_cast<uint32_t>(mask);
#else
  (void)p1;
  (void)p2;
  return BcmpReturnType::ZERO();
#endif // defined(__AVX2__)
}
template <size_t Size> using Bcmp = BcmpImpl<Size, 32, bcmp32>;
} // namespace avx2

namespace avx512bw {
LIBC_INLINE BcmpReturnType bcmp64(CPtr p1, CPtr p2) {
#if defined(__AVX512BW__)
  using T = char __attribute__((__vector_size__(64)));
  // A mask indicating which bytes differ after loading 64 bytes from p1 and p2.
  const uint64_t mask = _mm512_cmpneq_epi8_mask(
      cpp::bit_cast<__m512i>(load<T>(p1)), cpp::bit_cast<__m512i>(load<T>(p2)));
  const bool mask_is_set = mask != 0;
  return static_cast<uint32_t>(mask_is_set);
#else
  (void)p1;
  (void)p2;
  return BcmpReturnType::ZERO();
#endif // defined(__AVX512BW__)
}
template <size_t Size> using Bcmp = BcmpImpl<Size, 64, bcmp64>;
} // namespace avx512bw

// Assuming that the mask is non zero, the index of the first mismatching byte
// is the number of trailing zeros in the mask. Trailing zeros and not leading
// zeros because the x86 architecture is little endian.
LIBC_INLINE MemcmpReturnType char_diff_no_zero(CPtr p1, CPtr p2,
                                               uint64_t mask) {
  const size_t diff_index = __builtin_ctzll(mask);
  const int16_t ca = cpp::to_integer<uint8_t>(p1[diff_index]);
  const int16_t cb = cpp::to_integer<uint8_t>(p2[diff_index]);
  return ca - cb;
}

///////////////////////////////////////////////////////////////////////////////
// Memcmp

// Base implementation for the Memcmp specializations.
//  - BlockSize is either 16, 32 or 64 depending on the available compile time
// features, it is used to switch between "single native operation" or a
// "sequence of native operations".
//  - BlockMemcmp is the function that implements the memcmp logic.
//  - BlockBcmp is the function that implements the bcmp logic.
template <size_t Size, size_t BlockSize, auto BlockMemcmp, auto BlockBcmp>
struct MemcmpImpl {
  static constexpr size_t SIZE = Size;
  LIBC_INLINE static MemcmpReturnType block(CPtr p1, CPtr p2) {
    if constexpr (Size == BlockSize) {
      return BlockMemcmp(p1, p2);
    } else if constexpr (Size % BlockSize == 0) {
      for (size_t offset = 0; offset < Size; offset += BlockSize)
        if (auto value = BlockBcmp(p1 + offset, p2 + offset))
          return BlockMemcmp(p1 + offset, p2 + offset);
    } else {
      deferred_static_assert("SIZE not implemented");
    }
    return MemcmpReturnType::ZERO();
  }

  LIBC_INLINE static MemcmpReturnType tail(CPtr p1, CPtr p2, size_t count) {
    return block(p1 + count - Size, p2 + count - Size);
  }

  LIBC_INLINE static MemcmpReturnType head_tail(CPtr p1, CPtr p2,
                                                size_t count) {
    if (auto value = block(p1, p2))
      return value;
    return tail(p1, p2, count);
  }

  LIBC_INLINE static MemcmpReturnType loop_and_tail(CPtr p1, CPtr p2,
                                                    size_t count) {
    static_assert(Size > 1, "a loop of size 1 does not need tail");
    size_t offset = 0;
    do {
      if (auto value = block(p1 + offset, p2 + offset))
        return value;
      offset += Size;
    } while (offset < count - Size);
    return tail(p1, p2, count);
  }
};

namespace sse2 {
LIBC_INLINE MemcmpReturnType memcmp16(CPtr p1, CPtr p2) {
#if defined(__SSE2__)
  using T = char __attribute__((__vector_size__(16)));
  // A mask indicating which bytes differ after loading 16 bytes from p1 and p2.
  if (int mask =
          _mm_movemask_epi8(cpp::bit_cast<__m128i>(load<T>(p1) != load<T>(p2))))
    return char_diff_no_zero(p1, p2, mask);
  return MemcmpReturnType::ZERO();
#else
  (void)p1;
  (void)p2;
  return MemcmpReturnType::ZERO();
#endif // defined(__SSE2__)
}
template <size_t Size> using Memcmp = MemcmpImpl<Size, 16, memcmp16, bcmp16>;
} // namespace sse2

namespace avx2 {
LIBC_INLINE MemcmpReturnType memcmp32(CPtr p1, CPtr p2) {
#if defined(__AVX2__)
  using T = char __attribute__((__vector_size__(32)));
  // A mask indicating which bytes differ after loading 32 bytes from p1 and p2.
  if (int mask = _mm256_movemask_epi8(
          cpp::bit_cast<__m256i>(load<T>(p1) != load<T>(p2))))
    return char_diff_no_zero(p1, p2, mask);
  return MemcmpReturnType::ZERO();
#else
  (void)p1;
  (void)p2;
  return MemcmpReturnType::ZERO();
#endif // defined(__AVX2__)
}
template <size_t Size> using Memcmp = MemcmpImpl<Size, 32, memcmp32, bcmp32>;
} // namespace avx2

namespace avx512bw {
LIBC_INLINE MemcmpReturnType memcmp64(CPtr p1, CPtr p2) {
#if defined(__AVX512BW__)
  using T = char __attribute__((__vector_size__(64)));
  // A mask indicating which bytes differ after loading 64 bytes from p1 and p2.
  if (uint64_t mask =
          _mm512_cmpneq_epi8_mask(cpp::bit_cast<__m512i>(load<T>(p1)),
                                  cpp::bit_cast<__m512i>(load<T>(p2))))
    return char_diff_no_zero(p1, p2, mask);
  return MemcmpReturnType::ZERO();
#else
  (void)p1;
  (void)p2;
  return MemcmpReturnType::ZERO();
#endif // defined(__AVX512BW__)
}
template <size_t Size> using Memcmp = MemcmpImpl<Size, 64, memcmp64, bcmp64>;
} // namespace avx512bw

} // namespace __llvm_libc::x86

#endif // LIBC_TARGET_IS_X86_64

#endif // LLVM_LIBC_SRC_STRING_MEMORY_UTILS_OP_X86_H
