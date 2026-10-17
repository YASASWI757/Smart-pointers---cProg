
#include <stdio.h>

#include "sp_unique.h"

int main(void) {
    sp_unique owner = SP_UNIQUE_EMPTY;
    sp_unique new_owner = SP_UNIQUE_EMPTY;

    if (sp_unique_make(&owner, sizeof(int), NULL)
            != SP_OK) {
        fprintf(stderr, "Allocation failed\n");
        return 1;
    }

    int *value = sp_unique_get(&owner);

    *value = 42;

    printf("Original owner value: %d\n",
           *(int *)sp_unique_get(&owner));

    if (sp_unique_move(&new_owner, &owner)
            != SP_OK) {
        sp_unique_free(&owner);
        fprintf(stderr, "Ownership transfer failed\n");
        return 1;
    }

    printf("New owner value: %d\n",
           *(int *)sp_unique_get(&new_owner));

    printf("Original owner is %s\n",
           sp_unique_get(&owner) == NULL
               ? "empty"
               : "not empty");

    sp_unique_free(&owner);
    sp_unique_free(&new_owner);

    return 0;
}
