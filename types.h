#ifndef UYA_TYPES_H
#define UYA_TYPES_H

#if defined(_EE) || defined(_IOP)
#include <tamtypes.h>
#else
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

#endif
