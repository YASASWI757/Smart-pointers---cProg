#ifndef SP_INTERNAL_H
#define SP_INTERNAL_H

#include "sp_common.h"

/* Private allocation helpers, used by library modules. */
void *sp__alloc(size_t size);
void sp__free(void *ptr);
/* Register an external allocation when taking ownership. */
sp_status sp__track_external(void);

/* Stop tracking an allocation without freeing it. */
void sp__untrack(void);

#endif