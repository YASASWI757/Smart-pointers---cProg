#ifndef SP_INTERNAL_H
#define SP_INTERNAL_H

#include "sp_common.h"

/* Private allocation helpers, used by library modules. */
void *sp__alloc(size_t size);
void sp__free(void *ptr);

#endif