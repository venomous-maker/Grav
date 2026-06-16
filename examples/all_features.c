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

typedef struct Point Point;
typedef struct Rect Rect;
typedef struct zoo__Animal zoo__Animal;
typedef struct zoo__Dog zoo__Dog;
typedef struct zoo__Cat zoo__Cat;
typedef struct BankAccount BankAccount;
typedef struct Robot Robot;

struct Point {
    int x;
    int y;
};

struct Rect {
    Point origin;
    int width;
    int height;
};

typedef struct GravArr_int_4 {
    int data[4];
} GravArr_int_4;

struct zoo__Animal {
    const void* __vt;
    const GravTypeInfo* __type;
    const char* name;
};

struct zoo__Dog {
    const void* __vt;
    const GravTypeInfo* __type;
    const char* name;
};

struct zoo__Cat {
    const void* __vt;
    const GravTypeInfo* __type;
    const char* name;
};

struct BankAccount {
    const void* __vt;
    const GravTypeInfo* __type;
    const char* owner;
    int balance;
};

struct Robot {
    const void* __vt;
    const GravTypeInfo* __type;
    int id;
};

typedef struct zoo__Animal_VT {
    const char* (*speak)(zoo__Animal*);
    const char* (*label)(zoo__Animal*);
} zoo__Animal_VT;

typedef struct zoo__Dog_VT {
    const char* (*speak)(zoo__Animal*);
    const char* (*label)(zoo__Animal*);
} zoo__Dog_VT;

typedef struct zoo__Cat_VT {
    const char* (*speak)(zoo__Animal*);
    const char* (*label)(zoo__Animal*);
} zoo__Cat_VT;

typedef struct BankAccount_VT {
    void (*deposit)(BankAccount*, int);
    int (*get_balance)(BankAccount*);
    void (*set_balance)(BankAccount*, int);
} BankAccount_VT;

typedef struct Robot_VT {
    const char* (*greet)(Robot*);
    int (*get_id)(Robot*);
    void (*set_id)(Robot*, int);
} Robot_VT;

typedef struct zoo__Named_ITAB {
    const char* (*label)(void*);
} zoo__Named_ITAB;

typedef struct Greeter_ITAB {
    const char* (*greet)(void*);
} Greeter_ITAB;

Direction vf_opposite(Direction d);
int vf_areaOf(Rect r);
int vf_sumScores(GravArr_int_4 s);
void vf_bumpBy(int* p, int n);
int vf_square(int x);
int vf_sumOfSquares(int n);
const char* vf_classify(int x);
zoo__Animal* zoo__Animal_new(const char* name);
const char* zoo__Animal_m_label(zoo__Animal* self);
zoo__Dog* zoo__Dog_new(const char* name);
const char* zoo__Dog_m_speak(zoo__Dog* self);
zoo__Cat* zoo__Cat_new(const char* name);
const char* zoo__Cat_m_speak(zoo__Cat* self);
BankAccount* BankAccount_new(const char* owner);
void BankAccount_m_deposit(BankAccount* self, int n);
BankAccount* BankAccount_m_opening(const char* owner);
int BankAccount_m_get_balance(BankAccount* self);
void BankAccount_m_set_balance(BankAccount* self, int value);
Robot* Robot_new(int id);
const char* Robot_m_greet(Robot* self);
int Robot_m_get_id(Robot* self);
void Robot_m_set_id(Robot* self, int value);
void vf_main(void);

static zoo__Animal_VT zoo__Animal_vtable = {
    0,
    (const char* (*)(zoo__Animal*))zoo__Animal_m_label,
};

static zoo__Dog_VT zoo__Dog_vtable = {
    (const char* (*)(zoo__Animal*))zoo__Dog_m_speak,
    (const char* (*)(zoo__Animal*))zoo__Animal_m_label,
};

static zoo__Cat_VT zoo__Cat_vtable = {
    (const char* (*)(zoo__Animal*))zoo__Cat_m_speak,
    (const char* (*)(zoo__Animal*))zoo__Animal_m_label,
};

static BankAccount_VT BankAccount_vtable = {
    (void (*)(BankAccount*, int))BankAccount_m_deposit,
    (int (*)(BankAccount*))BankAccount_m_get_balance,
    (void (*)(BankAccount*, int))BankAccount_m_set_balance,
};

static Robot_VT Robot_vtable = {
    (const char* (*)(Robot*))Robot_m_greet,
    (int (*)(Robot*))Robot_m_get_id,
    (void (*)(Robot*, int))Robot_m_set_id,
};

static const GravTypeInfo zoo__Animal_typeinfo = { "zoo.Animal", 0 };
static const GravTypeInfo zoo__Dog_typeinfo = { "zoo.Dog", &zoo__Animal_typeinfo };
static const GravTypeInfo zoo__Cat_typeinfo = { "zoo.Cat", &zoo__Animal_typeinfo };
static const GravTypeInfo BankAccount_typeinfo = { "BankAccount", 0 };
static const GravTypeInfo Robot_typeinfo = { "Robot", 0 };

static zoo__Named_ITAB zoo__Animal__itab__zoo__Named = {
    (const char* (*)(void*))zoo__Animal_m_label,
};

static zoo__Named_ITAB zoo__Dog__itab__zoo__Named = {
    (const char* (*)(void*))zoo__Animal_m_label,
};

static zoo__Named_ITAB zoo__Cat__itab__zoo__Named = {
    (const char* (*)(void*))zoo__Animal_m_label,
};

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

int vf_areaOf(Rect r) {
    return ((r).width * (r).height);
}

int vf_sumScores(GravArr_int_4 s) {
    int total = 0;
    for (int i = 0, __hi1 = 4; i < __hi1; i++) {
        total += (s).data[i];
    }
    return total;
}

void vf_bumpBy(int* p, int n) {
    (*p) = ((*p) + n);
}

int vf_square(int x) {
    return (x * x);
}

int vf_sumOfSquares(int n) {
    int total = 0;
    for (int i = 1, __hi2 = n; i <= __hi2; i++) {
        total += vf_square(i);
    }
    return total;
}

const char* vf_classify(int x) {
    {
        int __sw3 = x;
        if ((__sw3 == 0)) {
            return "zero";
        } else if ((__sw3 == 1) || (__sw3 == 2) || (__sw3 == 3)) {
            return "small";
        } else {
            return "big";
        }
    }
    return "?";
}

zoo__Animal* zoo__Animal_new(const char* name) {
    zoo__Animal* self = (zoo__Animal*)calloc(1, sizeof(zoo__Animal));
    self->__vt = &zoo__Animal_vtable;
    self->__type = &zoo__Animal_typeinfo;
    (self)->name = name;
    return self;
}

const char* zoo__Animal_m_label(zoo__Animal* self) {
    return (self)->name;
}

zoo__Dog* zoo__Dog_new(const char* name) {
    zoo__Dog* self = (zoo__Dog*)calloc(1, sizeof(zoo__Dog));
    self->__vt = &zoo__Dog_vtable;
    self->__type = &zoo__Dog_typeinfo;
    (self)->name = name;
    return self;
}

const char* zoo__Dog_m_speak(zoo__Dog* self) {
    return "woof";
}

zoo__Cat* zoo__Cat_new(const char* name) {
    zoo__Cat* self = (zoo__Cat*)calloc(1, sizeof(zoo__Cat));
    self->__vt = &zoo__Cat_vtable;
    self->__type = &zoo__Cat_typeinfo;
    (self)->name = name;
    return self;
}

const char* zoo__Cat_m_speak(zoo__Cat* self) {
    return "meow";
}

BankAccount* BankAccount_new(const char* owner) {
    BankAccount* self = (BankAccount*)calloc(1, sizeof(BankAccount));
    self->__vt = &BankAccount_vtable;
    self->__type = &BankAccount_typeinfo;
    (self)->owner = owner;
    (self)->balance = 0;
    return self;
}

void BankAccount_m_deposit(BankAccount* self, int n) {
    (self)->balance += n;
}

BankAccount* BankAccount_m_opening(const char* owner) {
    return BankAccount_new(owner);
}

int BankAccount_m_get_balance(BankAccount* self) {
    return self->balance;
}

void BankAccount_m_set_balance(BankAccount* self, int value) {
    self->balance = value;
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

void vf_main(void) {
    const char* who = "Grav";
    printf("%s\n", grav_str_concat("hello ", who));
    printf("%d\n", ((int)(3.9)));
    printf("%d\n", ((int)(3.9)));
    printf("%f\n", (((double)(7)) / ((double)(2))));
    printf("%d\n", 4);
    printf("%d\n", (5 * 5));
    Point p = (Point){ .x = 3, .y = 4 };
    printf("%d\n", (((p).x * (p).x) + ((p).y * (p).y)));
    Rect r = (Rect){ .origin = p, .width = 5, .height = 6 };
    printf("%d\n", vf_areaOf(r));
    printf("%d\n", ((r).origin).x);
    Point p2 = p;
    (p2).x = 100;
    printf("%d\n", (p).x);
    int temp = 21;
    printf("%d\n", temp);
    Point here = (Point){ .x = 2, .y = 5 };
    printf("%d\n", ((here).x + (here).y));
    GravArr_int_4 scores = (GravArr_int_4){ { 80, 90, 70, 100 } };
    printf("%d\n", 4);
    printf("%d\n", (scores).data[3]);
    (scores).data[0] = 85;
    printf("%d\n", vf_sumScores(scores));
    printf("%d\n", ((int)(vf_opposite(Direction_North))));
    int mask = 0;
    mask |= 1;
    mask |= 4;
    printf("%d\n", (mask & 6));
    printf("%d\n", (13 % 5));
    printf("%d\n", ((1 << 8) ^ 255));
    printf("%d\n", (~5));
    printf("%s\n", (((9 % 2) == 0) ? "even" : "odd"));
    int acc = 0;
    for (int i = 0, __hi4 = 5; i < __hi4; i++) {
        acc += i;
    }
    printf("%d\n", acc);
    int count = 40;
    int* cp = (&count);
    vf_bumpBy(cp, 2);
    (*cp) = ((*cp) + 1);
    printf("%d\n", count);
    Point v = (Point){ .x = 7, .y = 8 };
    Point* vp = (&v);
    ((*vp)).x = 70;
    printf("%d\n", ((v).x + ((*vp)).y));
    struct GravIface g = (struct GravIface){ 0, 0 };
    printf("%s\n", (((g).obj == 0)) ? "true" : "false");
    struct GravIface chosen = ((g).obj != 0 ? g : (struct GravIface){ (void*)(Robot_new(1)), &Robot__itab__Greeter });
    printf("%s\n", ((Greeter_ITAB*)(chosen).itab)->greet((chosen).obj));
    Robot* maybe = 0;
    printf("%s\n", (maybe != 0 ? ((Robot_VT*)((struct GravObject*)(maybe))->__vt)->greet((Robot*)(maybe)) : ""));
    printf("%s\n", (Robot_new(2) != 0 ? ((Robot_VT*)((struct GravObject*)(Robot_new(2)))->__vt)->greet((Robot*)(Robot_new(2))) : ""));
    Robot* rb = Robot_new(3);
    printf("%s\n", ((Greeter_ITAB*)((struct GravIface){ (void*)(rb), &Robot__itab__Greeter }).itab)->greet(((struct GravIface){ (void*)(rb), &Robot__itab__Greeter }).obj));
    printf("%s\n", (grav_is_instance(rb, &Robot_typeinfo)) ? "true" : "false");
    int step = 1;
    while ((step <= 3)) {
        if ((step == 1)) {
            printf("%s\n", "one");
        } else if ((step == 2)) {
            printf("%s\n", "two");
        } else {
            printf("%s\n", "three");
        }
        (step++);
    }
    for (int k = 0; (k < 5); (k++)) {
        if ((k == 1)) {
            continue;
        }
        if ((k == 3)) {
            break;
        }
        printf("%d\n", k);
    }
    int m = 2;
    do {
        printf("%d\n", m);
        (m--);
    } while ((m > 0));
    printf("%s\n", vf_classify(7));
    zoo__Animal* an = (zoo__Animal*)(zoo__Dog_new("Rex"));
    printf("%s\n", ((zoo__Animal_VT*)((struct GravObject*)(an))->__vt)->speak((zoo__Animal*)(an)));
    printf("%s\n", ((zoo__Animal_VT*)((struct GravObject*)(an))->__vt)->label((zoo__Animal*)(an)));
    struct GravIface nm = (struct GravIface){ (void*)(zoo__Cat_new("Mia")), &zoo__Cat__itab__zoo__Named };
    printf("%s\n", ((zoo__Named_ITAB*)(nm).itab)->label((nm).obj));
    printf("%s\n", grav_typename(an));
    printf("%s\n", (grav_is_instance(an, &zoo__Animal_typeinfo)) ? "true" : "false");
    BankAccount* acct = BankAccount_m_opening("Ada");
    ((BankAccount_VT*)((struct GravObject*)(acct))->__vt)->deposit((BankAccount*)(acct), 50);
    printf("%d\n", ((BankAccount_VT*)((struct GravObject*)(acct))->__vt)->get_balance((BankAccount*)(acct)));
    printf("%s\n", (acct)->owner);
    printf("%s\n", (((!(1 > 2)) && true)) ? "true" : "false");
    printf("%d\n", vf_sumOfSquares(4));
    printf("%s\n", ((sizeof(int) > 0)) ? "true" : "false");
    printf("%s\n", ((sizeof(Point) == sizeof(Point))) ? "true" : "false");
    printf("%s\n", ((sizeof(scores) == (sizeof(int) * 4))) ? "true" : "false");
}

int main(void) {
    vf_main();
    return 0;
}
