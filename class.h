/*
 * class.h — OOP in C, just #include it.
 *
 * Gives you: CLASS, EXTENDS, CONSTRUCTOR, DESTRUCTOR,
 *            METHOD, THIS, NEW, DELETE, SUPER, UPCAST
 */

#ifndef CLASS_H
#define CLASS_H

#include <stdlib.h>
#include <string.h>

/* ── CLASS ───────────────────────────────────────────────────── */

/*
 * CLASS(Name, body...)
 * Defines a typedef'd struct with a built-in _destroy pointer.
 *
 *   CLASS(Animal,
 *       char* name;
 *       int age;
 *       METHOD(void, speak, THIS(Animal));
 *   );
 */
#define CLASS(Name, ...)                \
    typedef struct Name Name;           \
    void Name##_destroy_impl(Name*);    \
    void Name##_init(Name*);            \
    struct Name {                       \
        void (*_destroy)(Name* self);   \
        __VA_ARGS__                     \
    }

/* ── EXTENDS ─────────────────────────────────────────────────── */

/*
 * EXTENDS(Base)
 * Embed as FIRST member for inheritance. Allows UPCAST.
 *
 *   CLASS(Cat,
 *       EXTENDS(Animal);
 *       int indoor;
 *   );
 */
#define EXTENDS(Base) Base base

/* ── METHOD / THIS ───────────────────────────────────────────── */

/*
 * METHOD(RetType, name, args...)
 * Declares a function pointer member.
 *
 *   METHOD(void, speak, THIS(Animal));
 *   => void (*speak)(Animal* self);
 */
#define METHOD(RetType, name, ...) RetType (*name)(__VA_ARGS__)

/*
 * THIS(ClassName)
 * Shorthand for the self pointer type.
 */
#define THIS(Name) Name* self

/* ── CONSTRUCTOR ─────────────────────────────────────────────── */

/*
 * CONSTRUCTOR(Name)
 * Defines Name_init(Name* self) — you fill in the body.
 * Called automatically by NEW after allocation.
 *
 *   CONSTRUCTOR(Animal) {
 *       self->name = NULL;
 *       self->age = 0;
 *   }
 */
#define CONSTRUCTOR(Name) \
    void Name##_init(Name* self)

/* ── DESTRUCTOR ──────────────────────────────────────────────── */

/*
 * DESTRUCTOR(Name)
 * Defines Name_destroy_impl(Name* self) — you fill in the body.
 * Called automatically by DELETE before free.
 *
 *   DESTRUCTOR(Animal) {
 *       free(self->name);
 *   }
 */
#define DESTRUCTOR(Name) \
    void Name##_destroy_impl(Name* self)

/* ── NEW / DELETE ────────────────────────────────────────────── */

/*
 * NEW(Name)
 * Allocates, zeros, wires _destroy, calls Name_init.
 * Returns Name* (NULL on alloc failure).
 *
 * Set fields after NEW or inside the constructor:
 *   Animal* a = NEW(Animal);
 *   a->name = strdup("Rex");
 *   a->age = 5;
 */
#define NEW(Name)                                       \
    ({                                                  \
        Name* _obj = (Name*)calloc(1, sizeof(Name));    \
        if (_obj) {                                     \
            _obj->_destroy = Name##_destroy_impl;       \
            Name##_init(_obj);                          \
        }                                               \
        _obj;                                           \
    })

/*
 * DELETE(obj)
 * Calls _destroy then frees.
 */
#define DELETE(obj)                      \
    do {                                 \
        if (obj) {                       \
            if ((obj)->_destroy)          \
                (obj)->_destroy(obj);     \
            free(obj);                   \
        }                                \
    } while (0)

/* ── SUPER ───────────────────────────────────────────────────── */

/*
 * SUPER(Base, self)
 * Calls the base class constructor on the embedded base.
 *
 *   CONSTRUCTOR(Cat) {
 *       SUPER(Animal, self);
 *       ...
 *   }
 */
#define SUPER(Base, self) \
    Base##_init((Base*)(&(self)->base))

/* ── UPCAST ──────────────────────────────────────────────────── */

/*
 * UPCAST(Base, ptr)
 * Cast derived pointer to base pointer.
 *
 *   Animal* a = UPCAST(Animal, cat_ptr);
 */
#define UPCAST(Base, ptr) ((Base*)(&(ptr)->base))

#endif /* CLASS_H */
