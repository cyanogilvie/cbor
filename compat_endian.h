#ifndef COMPAT_ENDIAN_H
#define COMPAT_ENDIAN_H

/*
 * Portable big-endian byte swap macros.
 * On Linux/BSD, <endian.h> provides these directly.
 * On Windows (mingw), we define them using GCC/Clang builtins.
 */

#if defined(__linux__) || defined(__CYGWIN__)
#  include <endian.h>
#elif defined(__APPLE__)
#  include <libkern/OSByteOrder.h>
#  define htobe16(x) OSSwapHostToBigInt16(x)
#  define htobe32(x) OSSwapHostToBigInt32(x)
#  define htobe64(x) OSSwapHostToBigInt64(x)
#  define be16toh(x) OSSwapBigToHostInt16(x)
#  define be32toh(x) OSSwapBigToHostInt32(x)
#  define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(_WIN32) || defined(__MINGW32__)
#  include <stdint.h>
#  if defined(__GNUC__) || defined(__clang__)
#    define htobe16(x) __builtin_bswap16(x)
#    define htobe32(x) __builtin_bswap32(x)
#    define htobe64(x) __builtin_bswap64(x)
#    define be16toh(x) __builtin_bswap16(x)
#    define be32toh(x) __builtin_bswap32(x)
#    define be64toh(x) __builtin_bswap64(x)
#  else
#    include <stdlib.h>
#    define htobe16(x) _byteswap_ushort(x)
#    define htobe32(x) _byteswap_ulong(x)
#    define htobe64(x) _byteswap_uint64(x)
#    define be16toh(x) _byteswap_ushort(x)
#    define be32toh(x) _byteswap_ulong(x)
#    define be64toh(x) _byteswap_uint64(x)
#  endif
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#  include <sys/endian.h>
#else
#  error "Platform not supported for byte swap macros"
#endif

#endif /* COMPAT_ENDIAN_H */
