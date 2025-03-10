//===-- Compile time cpu feature detection ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file lists target cpu features by introspecting compiler enabled
// preprocessor definitions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SUPPORT_MACROS_CPU_FEATURES_H
#define LLVM_LIBC_SRC_SUPPORT_MACROS_CPU_FEATURES_H

#if defined(__SSE2__)
#define LIBC_TARGET_HAS_SSE2
#endif

#if defined(__SSE4_2__)
#define LIBC_TARGET_HAS_SSE4_2
#endif

#if defined(__AVX__)
#define LIBC_TARGET_HAS_AVX
#endif

#if defined(__AVX2__)
#define LIBC_TARGET_HAS_AVX2
#endif

#if defined(__AVX512F__)
#define LIBC_TARGET_HAS_AVX512F
#endif

#if defined(__AVX512BW__)
#define LIBC_TARGET_HAS_AVX512BW
#endif

#if defined(__ARM_FEATURE_FMA) || defined(__AVX2__) || defined(__FMA__)
#define LIBC_TARGET_HAS_FMA
#endif

#endif // LLVM_LIBC_SRC_SUPPORT_MACROS_CPU_FEATURES_H
