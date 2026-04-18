/*
 * map_format.h — Origin Engine Map format (.oem)
 *
 * Binary map format: "OEMp" header + segments.
 * Segments: 00=MapData, 01=Brush, 02=Entity, 03=I/O, 04=BrushEntBrush
 */
#ifndef MAP_FORMAT_H
#define MAP_FORMAT_H

#include "brush.h"
#include "entity.h"

#define OEM_MAGIC "OEMp"

#define SEG_MAP_DATA       0x00
#define SEG_BRUSH          0x01
#define SEG_ENTITY         0x02
#define SEG_IO_CONNECTION  0x03
#define SEG_BRUSH_ENT      0x04

typedef struct {
    char title[256];
    char description[512];
} MapInfo;

int map_load(const char* path, MapInfo* out_info);
int map_save(const char* path, const MapInfo* info);

#endif /* MAP_FORMAT_H */
