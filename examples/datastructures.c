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

typedef struct Point Point;

struct Point {
    int x;
    int y;
};

typedef struct GravArr_int_3 {
    int data[3];
} GravArr_int_3;

typedef struct GravArr_int_4 {
    int data[4];
} GravArr_int_4;

int vf_sumRow(GravArr_int_3 r);
GravArr_int_4 vf_makeSquares(int n);
void vf_main(void);


int vf_sumRow(GravArr_int_3 r) {
    int total = 0;
    for (int i = 0, __hi0 = 3; i < __hi0; i++) {
        total += (r).data[i];
    }
    return total;
}

GravArr_int_4 vf_makeSquares(int n) {
    GravArr_int_4 out = (GravArr_int_4){ { 0, 0, 0, 0 } };
    for (int i = 0, __hi1 = n; i < __hi1; i++) {
        (out).data[i] = (i * i);
    }
    return out;
}

void vf_main(void) {
    printf("%d\n", 4);
    printf("%d\n", (4 * 2));
    printf("%d\n", (3 * 4));
    int temp = 21;
    printf("%d\n", temp);
    Point here = (Point){ .x = 2, .y = 5 };
    printf("%d\n", ((here).x + (here).y));
    GravArr_int_4 primes = (GravArr_int_4){ { 2, 3, 5, 7 } };
    printf("%d\n", 4);
    printf("%d\n", (primes).data[2]);
    (primes).data[0] = 11;
    printf("%d\n", (primes).data[0]);
    GravArr_int_3 r = (GravArr_int_3){ { 10, 20, 30 } };
    printf("%d\n", vf_sumRow(r));
    GravArr_int_4 sq = vf_makeSquares(4);
    printf("%d\n", (sq).data[3]);
    printf("%d\n", 4);
    printf("%s\n", ((sizeof(int) > 0)) ? "true" : "false");
    printf("%s\n", ((sizeof(Point) == sizeof(Point))) ? "true" : "false");
    printf("%s\n", ((sizeof(primes) == (sizeof(int) * 4))) ? "true" : "false");
}

int main(void) {
    vf_main();
    return 0;
}
