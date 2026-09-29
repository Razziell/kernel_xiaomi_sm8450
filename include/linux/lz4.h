/* SPDX-License-Identifier: BSD-2-Clause */
/* Kernel compatibility interface for LZ4 1.10.0. */
#ifndef __LINUX_LZ4_H__
#define __LINUX_LZ4_H__

#include "../../lib/lz4/lz4.h"
#include "../../lib/lz4/lz4hc.h"

/* Allocate these sizes, not an older upstream stream layout. */
#define LZ4_MEM_COMPRESS sizeof(LZ4_stream_t)
#define LZ4HC_MEM_COMPRESS sizeof(LZ4_streamHC_t)
/* Preserve Linux's accepted level range; upstream clamps 13..16 to 12. */
#define LZ4HC_MIN_CLEVEL 3
#define LZ4HC_DEFAULT_CLEVEL LZ4HC_CLEVEL_DEFAULT
#define LZ4HC_MAX_CLEVEL 16

#define LZ4_STREAMSIZE sizeof(LZ4_stream_t)
#define LZ4_STREAMSIZE_U64 (LZ4_STREAMSIZE / sizeof(unsigned long long))
#define LZ4_STREAMHCSIZE sizeof(LZ4_streamHC_t)
#define LZ4_STREAMHCSIZE_SIZET (LZ4_STREAMHCSIZE / sizeof(size_t))
#define LZ4_STREAMDECODESIZE sizeof(LZ4_streamDecode_t)
#define LZ4_STREAMDECODESIZE_U64 (LZ4_STREAMDECODESIZE / sizeof(unsigned long long))

#endif /* __LINUX_LZ4_H__ */
