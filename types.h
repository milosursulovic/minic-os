#pragma once

// Short, kernel-style aliases for the freestanding-guaranteed fixed-width
// integer types (<stdint.h> is a compiler-provided header, not a linked
// library - same "toolchain itself is exempt" carve-out as the assembler
// and linker). Kept short to match this codebase's existing style.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
