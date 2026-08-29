#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

void idt_init(void);
void pic_remap(void);
void pit_init(void);

#pragma GCC visibility pop
