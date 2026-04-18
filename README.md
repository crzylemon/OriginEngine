# Origin Engine

A Source Engine-inspired game engine written in pure C with Vulkan rendering.

## Features

- Vulkan renderer with textured brushes, wireframe overlay, transparency, and face lighting
- Arbitrary convex brush geometry with per-face textures, UV scale/offset
- Source-style entity system with classname/targetname, Think/Touch/Use callbacks
- Entity I/O system (output→input connections with delays)
- Brush entities: `func_door`, `trigger_once`, `trigger_multiple`, `trigger_hurt`
- Player movement: WASD + mouse, gravity, crouching, slope walking, fall damage
- ODE physics integration for rigid body props
- Binary map format (`.oem`) with brush, entity, I/O, and brush entity segments
- Game .so loading (`dlopen`) — engine and game are separate
- Dev console with command history
- Source-style menu system (title/pause)
- Sound system (miniaudio + stb_vorbis, WAV/OGG)
- Mesh system (`.oemesh` binary format) with prop rendering
- Equirectangular panorama skybox
- Bitmap font rendering with per-vertex color
- HUD (health, crosshair, death screen)
- Hammer-style 4-view map editor (GTK3 + Cairo)

## Building

Requires: GCC, Vulkan SDK, GLFW3, ODE, GTK3

```bash
make engine    # builds origin_engine
make game      # builds game/bin/client.so
make editor    # builds origin_editor (GTK3)
make shaders   # compiles GLSL → SPIR-V
```

## Running

```bash
./origin_engine --game game
./origin_editor --game game
```

## Project Structure

```
engine/         Engine source (renderer, physics, entities, brushes, etc.)
game/           Demo game (client.so, maps, textures, sounds, meshes)
realGame/       Real game i'm working on
tools/          Map compiler, mesh generator, texture generator
editor.c        Hammer-style map editor
main.c          Engine entry point
```

## Map Format (.oem)

Binary format with segments: MapData (0x00), Brush (0x01), Entity (0x02), I/O (0x03), BrushEntBrush (0x04). See `map-example.txt` for the full spec.

## License \[kinda\]

Do whatever you want with it, as long as you credit Origin Engine. (This applies to the code in here and making games with it)
