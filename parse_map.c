#include "./engine/entity.h"
#include "./engine/map_format.h"
#include "./engine/brush.h"
#include <stdio.h>

int main() {
    MapInfo map;
    if (map_load("game/maps/editor.oem", &map)) {
        
    }
    printf("ENTITIES: %d | BRUSHES: %d\n", brush_count(), entity_count());
    return 1;
}