/* Generated from Grav source by gravc. Do not edit. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct GravTypeInfo {
    const char* name;
    const struct GravTypeInfo* base;
} GravTypeInfo;

/* Common object header: vtable pointer + runtime type descriptor. */
struct GravObject { const void* __vt; const GravTypeInfo* __type; };

/* An interface value: the object plus its per-class method table. */
struct GravIface { void* obj; const void* itab; };

static const char* grav_str_concat(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    char* r = (char*)malloc(la + lb + 1);
    memcpy(r, a, la);
    memcpy(r + la, b, lb + 1);
    return r;
}

static const char* grav_typename(const void* obj) {
    return ((const struct GravObject*)obj)->__type->name;
}

static bool grav_is_instance(const void* obj, const GravTypeInfo* want) {
    const GravTypeInfo* t = ((const struct GravObject*)obj)->__type;
    while (t) { if (t == want) return true; t = t->base; }
    return false;
}


const char* vf_classify(int x);
void vf_main(void);


const char* vf_classify(int x) {
    {
        int __sw0 = x;
        if ((__sw0 == 0)) {
            return "zero";
        } else if ((__sw0 == 1) || (__sw0 == 2) || (__sw0 == 3)) {
            return "small";
        } else {
            return "big";
        }
    }
    return "?";
}

void vf_main(void) {
    int i = 1;
    while ((i <= 5)) {
        if (((i == 3) || (i == 5))) {
            printf("%s\n", "odd-ish");
        } else {
            printf("%d\n", i);
        }
        (i++);
    }
    for (int k = 0; (k < 3); (k++)) {
        printf("%d\n", k);
    }
    int n = 3;
    do {
        printf("%d\n", n);
        (n--);
    } while ((n > 0));
    int j = 0;
    while (true) {
        (j++);
        if ((j == 2)) {
            continue;
        }
        if ((j > 4)) {
            break;
        }
        printf("%d\n", j);
    }
    printf("%s\n", vf_classify(2));
    printf("%s\n", (((!(1 > 2)) && true)) ? "true" : "false");
}

int main(void) {
    vf_main();
    return 0;
}
