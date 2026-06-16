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
typedef struct Square Square;

struct Point {
    int x;
    int y;
};

struct Square {
    const void* __vt;
    const GravTypeInfo* __type;
    int side;
};

typedef struct Square_VT {
    int (*area)(Square*);
    int (*get_side)(Square*);
    void (*set_side)(Square*, int);
} Square_VT;

typedef struct Shape_ITAB {
    int (*area)(void*);
} Shape_ITAB;

int vf_mathx__square(int n);
int vf_mathx__max(int a, int b);
int vf_distSq(Point p);
Square* Square_new(int side);
int Square_m_area(Square* self);
int Square_m_get_side(Square* self);
void Square_m_set_side(Square* self, int value);
void vf_main(void);

static Square_VT Square_vtable = {
    (int (*)(Square*))Square_m_area,
    (int (*)(Square*))Square_m_get_side,
    (void (*)(Square*, int))Square_m_set_side,
};

static const GravTypeInfo Square_typeinfo = { "Square", 0 };

static Shape_ITAB Square__itab__Shape = {
    (int (*)(void*))Square_m_area,
};

int vf_mathx__square(int n) {
    return (n * n);
}

int vf_mathx__max(int a, int b) {
    return ((a > b) ? a : b);
}

int vf_distSq(Point p) {
    return (vf_mathx__square((p).x) + vf_mathx__square((p).y));
}

Square* Square_new(int side) {
    Square* self = (Square*)calloc(1, sizeof(Square));
    self->__vt = &Square_vtable;
    self->__type = &Square_typeinfo;
    (self)->side = side;
    return self;
}

int Square_m_area(Square* self) {
    return vf_mathx__square((self)->side);
}

int Square_m_get_side(Square* self) {
    return self->side;
}

void Square_m_set_side(Square* self, int value) {
    self->side = value;
}

void vf_main(void) {
    Point p = (Point){ .x = 3, .y = 4 };
    printf("%d\n", vf_distSq(p));
    struct GravIface s = (struct GravIface){ (void*)(Square_new(6)), &Square__itab__Shape };
    printf("%d\n", ((Shape_ITAB*)(s).itab)->area((s).obj));
    printf("%d\n", vf_mathx__max(vf_distSq(p), ((Shape_ITAB*)(s).itab)->area((s).obj)));
}

int main(void) {
    vf_main();
    return 0;
}
