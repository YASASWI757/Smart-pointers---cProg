#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "smartptr.h"

int main(void) {
    const sp_status statuses[] = {
        SP_OK,
        SP_ERR_NULL_ARG,
        SP_ERR_NO_MEMORY,
        SP_ERR_EMPTY,
        SP_ERR_INVALID_HANDLE,
        SP_ERR_EXPIRED,
        SP_ERR_OVERFLOW,
        SP_ERR_INVALID_SIZE,
        SP_ERR_NOT_EMPTY
    };

    for (size_t i = 0;
         i < sizeof statuses / sizeof statuses[0];
         ++i) {
        const char *message = sp_strerror(statuses[i]);

        assert(message != NULL);
        assert(message[0] != '\0');
        assert(strcmp(message, "Unknown error") != 0);
    }

    assert(strcmp(sp_strerror(SP_OK), "Success") == 0);

    assert(strcmp(
        sp_strerror((sp_status)999),
        "Unknown error"
    ) == 0);

    puts("All common-layer tests passed!");
    return 0;
}