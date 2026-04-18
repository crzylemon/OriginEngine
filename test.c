/*
 * test.c — Demo of class.h: OOP in C
 *
 * Shows: classes, methods, constructors, destructors,
 *        inheritance, and polymorphism.
 *
 * Compile: gcc test.c -o test
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "class.h"

/* ═══════════════════════════════════════════════════════════════
 *  Base class: Animal
 * ═══════════════════════════════════════════════════════════════ */

CLASS(Animal,
    char* name;
    int   age;
    METHOD(void, speak, THIS(Animal));
);

void Animal_speak_default(Animal* self) {
    printf("%s says: ...\n", self->name);
}

CONSTRUCTOR(Animal) {
    self->name  = NULL;
    self->age   = 0;
    self->speak = Animal_speak_default;
}

DESTRUCTOR(Animal) {
    printf("  [Animal] freeing '%s'\n", self->name);
    free(self->name);
}

/* ═══════════════════════════════════════════════════════════════
 *  Derived class: Cat (extends Animal)
 * ═══════════════════════════════════════════════════════════════ */

CLASS(Cat,
    EXTENDS(Animal);
    int indoor;
);

void Cat_speak(Animal* self) {
    printf("%s says: Meow!\n", self->name);
}

CONSTRUCTOR(Cat) {
    SUPER(Animal, self);                    /* init base */
    self->indoor = 0;
    UPCAST(Animal, self)->speak = Cat_speak; /* override speak */
}

DESTRUCTOR(Cat) {
    printf("  [Cat] cleaning up cat stuff\n");
    Animal_destroy_impl(UPCAST(Animal, self)); /* call base destructor */
}

/* ═══════════════════════════════════════════════════════════════
 *  Derived class: Dog (extends Animal)
 * ═══════════════════════════════════════════════════════════════ */

CLASS(Dog,
    EXTENDS(Animal);
    int tricks;
);

void Dog_speak(Animal* self) {
    /* self points to the embedded Animal (i.e. Dog.base).
       Container_of-style: back up to the Dog that contains it. */
    Dog* d = (Dog*)((char*)self - offsetof(Dog, base));
    printf("%s says: Woof! (knows %d tricks)\n", self->name, d->tricks);
}

CONSTRUCTOR(Dog) {
    SUPER(Animal, self);
    self->tricks = 0;
    UPCAST(Animal, self)->speak = Dog_speak;
}

DESTRUCTOR(Dog) {
    printf("  [Dog] cleaning up dog stuff\n");
    Animal_destroy_impl(UPCAST(Animal, self));
}

/* ═══════════════════════════════════════════════════════════════
 *  Main
 * ═══════════════════════════════════════════════════════════════ */

int main(void) {
    printf("=== Creating objects ===\n\n");

    /* Create and configure objects */
    Animal* a = NEW(Animal);
    a->name = strdup("Generic Beast");
    a->age  = 5;

    Cat* c = NEW(Cat);
    UPCAST(Animal, c)->name = strdup("Whiskers");
    UPCAST(Animal, c)->age  = 3;
    c->indoor = 1;

    Dog* d = NEW(Dog);
    UPCAST(Animal, d)->name = strdup("Rex");
    UPCAST(Animal, d)->age  = 7;
    d->tricks = 12;

    /* Polymorphism: call speak through base pointer */
    printf("--- Polymorphic speak ---\n");
    Animal* zoo[] = { a, UPCAST(Animal, c), UPCAST(Animal, d) };
    for (int i = 0; i < 3; i++) {
        zoo[i]->speak(zoo[i]);
    }

    printf("\n--- Info ---\n");
    printf("%s is %d years old\n", a->name, a->age);
    printf("%s is %d, indoor: %s\n",
           UPCAST(Animal, c)->name,
           UPCAST(Animal, c)->age,
           c->indoor ? "yes" : "no");
    printf("%s is %d, tricks: %d\n",
           UPCAST(Animal, d)->name,
           UPCAST(Animal, d)->age,
           d->tricks);

    printf("\n=== Deleting objects ===\n");
    DELETE(a);
    DELETE(c);
    DELETE(d);

    printf("\nDone!\n");
    return 0;
}
