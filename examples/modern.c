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

typedef enum Direction {
    Direction_North,
    Direction_East,
    Direction_South,
    Direction_West
} Direction;

typedef struct Vec Vec;
typedef struct Robot Robot;

struct Vec {
    int x;
    int y;
};

struct Robot {
    const void* __vt;
    const GravTypeInfo* __type;
    int id;
};

typedef struct Robot_VT {
    const char* (*greet)(Robot*);
    int (*get_id)(Robot*);
    void (*set_id)(Robot*, int);
} Robot_VT;

typedef struct Greeter_ITAB {
    const char* (*greet)(void*);
} Greeter_ITAB;

Direction vf_opposite(Direction d);
Robot* Robot_new(int id);
const char* Robot_m_greet(Robot* self);
int Robot_m_get_id(Robot* self);
void Robot_m_set_id(Robot* self, int value);
int vf_square(int x);
int vf_sumOfSquares(int n);
void vf_bumpBy(int* p, int n);
void vf_main(void);

static Robot_VT Robot_vtable = {
    (const char* (*)(Robot*))Robot_m_greet,
    (int (*)(Robot*))Robot_m_get_id,
    (void (*)(Robot*, int))Robot_m_set_id,
};

static const GravTypeInfo Robot_typeinfo = { "Robot", 0 };

static Greeter_ITAB Robot__itab__Greeter = {
    (const char* (*)(void*))Robot_m_greet,
};

Direction vf_opposite(Direction d) {
    {
        Direction __sw0 = d;
        if ((__sw0 == Direction_North)) {
            return Direction_South;
        } else if ((__sw0 == Direction_South)) {
            return Direction_North;
        } else if ((__sw0 == Direction_East)) {
            return Direction_West;
        } else {
            return Direction_East;
        }
    }
}

Robot* Robot_new(int id) {
    Robot* self = (Robot*)calloc(1, sizeof(Robot));
    self->__vt = &Robot_vtable;
    self->__type = &Robot_typeinfo;
    (self)->id = id;
    return self;
}

const char* Robot_m_greet(Robot* self) {
    return "beep";
}

int Robot_m_get_id(Robot* self) {
    return self->id;
}

void Robot_m_set_id(Robot* self, int value) {
    self->id = value;
}

int vf_square(int x) {
    return (x * x);
}

int vf_sumOfSquares(int n) {
    int total = 0;
    for (int i = 1, __hi1 = n; i <= __hi1; i++) {
        total += vf_square(i);
    }
    return total;
}

void vf_bumpBy(int* p, int n) {
    (*p) = ((*p) + n);
}

void vf_main(void) {
    printf("%d\n", ((int)(vf_opposite(Direction_North))));
    int mask = 0;
    mask |= 1;
    mask |= 4;
    printf("%d\n", (mask & 6));
    printf("%d\n", (13 % 5));
    printf("%d\n", ((1 << 8) ^ 255));
    printf("%d\n", (~5));
    int n = 9;
    printf("%s\n", (((n % 2) == 0) ? "even" : "odd"));
    int acc = 0;
    for (int i = 0, __hi2 = 5; i < __hi2; i++) {
        acc += i;
    }
    printf("%d\n", acc);
    printf("%d\n", ((int)(3.9)));
    int count = 40;
    int* p = (&count);
    vf_bumpBy(p, 2);
    (*p) = ((*p) + 1);
    printf("%d\n", count);
    Vec v = (Vec){ .x = 7, .y = 8 };
    Vec* vp = (&v);
    ((*vp)).x = 70;
    printf("%d\n", ((v).x + ((*vp)).y));
    struct GravIface g = (struct GravIface){ 0, 0 };
    printf("%s\n", (((g).obj == 0)) ? "true" : "false");
    struct GravIface chosen = ((g).obj != 0 ? g : (struct GravIface){ (void*)(Robot_new(1)), &Robot__itab__Greeter });
    printf("%s\n", ((Greeter_ITAB*)(chosen).itab)->greet((chosen).obj));
    Robot* maybe = 0;
    printf("%s\n", (maybe != 0 ? ((Robot_VT*)((struct GravObject*)(maybe))->__vt)->greet((Robot*)(maybe)) : ""));
    printf("%s\n", (Robot_new(2) != 0 ? ((Robot_VT*)((struct GravObject*)(Robot_new(2)))->__vt)->greet((Robot*)(Robot_new(2))) : ""));
    Robot* r = Robot_new(3);
    printf("%s\n", ((Greeter_ITAB*)((struct GravIface){ (void*)(r), &Robot__itab__Greeter }).itab)->greet(((struct GravIface){ (void*)(r), &Robot__itab__Greeter }).obj));
    printf("%s\n", (grav_is_instance(r, &Robot_typeinfo)) ? "true" : "false");
    printf("%d\n", vf_sumOfSquares(4));
}

int main(void) {
    vf_main();
    return 0;
}
