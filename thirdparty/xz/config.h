/* SPDX-License-Identifier: 0BSD */

#pragma once

#define ASSUME_RAM 32

#define HAVE_CHECK_CRC32 1

#define HAVE_DECODERS 1
#define HAVE_ENCODERS 1

#define HAVE_DECODER_LZMA1 1
#define HAVE_ENCODER_LZMA1 1

#define HAVE_MF_BT2 1
#define HAVE_MF_BT3 1
#define HAVE_MF_BT4 1
#define HAVE_MF_HC3 1
#define HAVE_MF_HC4 1

#define HAVE_INTTYPES_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDINT_H 1
#define HAVE__BOOL 1

#define HAVE_VISIBILITY 0

#define NDEBUG 1

#define PACKAGE_BUGREPORT "xz@tukaani.org"
#define PACKAGE_NAME "XZ Utils"
#define PACKAGE_URL "https://tukaani.org/xz/"

#if defined(_WIN64) || defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
#define SIZEOF_SIZE_T 8
#else
#define SIZEOF_SIZE_T 4
#endif

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
#define TUKLIB_FAST_UNALIGNED_ACCESS 1
#endif