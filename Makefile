CC = gcc
CFLAGS = -Wall -Wextra -lm
VULKAN_LIBS = -lvulkan -lglfw -ldl -lpthread -lm -lode

ENGINE_SRC = engine/entity.c engine/brush.c engine/trigger.c \
             engine/console.c engine/world.c

RENDER_SRC = engine/render.c engine/camera.c engine/brush.c engine/player.c \
             engine/console.c engine/texture.c engine/entity.c engine/trigger.c \
             engine/game_dll.c engine/brush_entity.c engine/map_format.c \
             engine/entity_io.c engine/font.c engine/dev_console.c \
             engine/sound.c engine/mesh.c engine/prop.c engine/physics.c

EDITOR_SRC = engine/brush.c engine/entity.c engine/console.c engine/trigger.c \
             engine/brush_entity.c engine/map_format.c engine/entity_io.c \
             engine/texture.c

GTK_FLAGS = $(shell pkg-config --cflags --libs gtk+-3.0)

SHADER_DIR = engine/shaders

.PHONY: all clean shaders engine game

all: shaders engine editor game

shaders:
	glslangValidator -V $(SHADER_DIR)/triangle.vert -o $(SHADER_DIR)/vert.spv
	glslangValidator -V $(SHADER_DIR)/triangle.frag -o $(SHADER_DIR)/frag.spv
	glslangValidator -V $(SHADER_DIR)/wireframe.vert -o $(SHADER_DIR)/wire_vert.spv
	glslangValidator -V $(SHADER_DIR)/wireframe.frag -o $(SHADER_DIR)/wire_frag.spv
	glslangValidator -V $(SHADER_DIR)/brush.vert -o $(SHADER_DIR)/brush_vert.spv
	glslangValidator -V $(SHADER_DIR)/brush.frag -o $(SHADER_DIR)/brush_frag.spv
	glslangValidator -V $(SHADER_DIR)/overlay.vert -o $(SHADER_DIR)/overlay_vert.spv
	glslangValidator -V $(SHADER_DIR)/overlay.frag -o $(SHADER_DIR)/overlay_frag.spv
	glslangValidator -V $(SHADER_DIR)/text.vert -o $(SHADER_DIR)/text_vert.spv
	glslangValidator -V $(SHADER_DIR)/text.frag -o $(SHADER_DIR)/text_frag.spv

# The engine binary
engine: main.c $(RENDER_SRC)
	$(CC) $^ -o origin_engine $(CFLAGS) $(VULKAN_LIBS)

# The game shared library
game: game/game.c
	$(CC) -shared -fPIC $^ -o game/bin/client.so $(CFLAGS)
game2: realGame/game.c
	$(CC) -shared -fPIC $^ -o realGame/bin/client.so $(CFLAGS)

# The map editor (GTK3 + Cairo, Hammer-style 4-view)
editor: editor.c $(EDITOR_SRC)
	$(CC) $^ -o origin_editor $(CFLAGS) $(GTK_FLAGS)

# Old logic-only demo
game_logic: main.c $(ENGINE_SRC)
	$(CC) $^ -o game_logic_demo $(CFLAGS)

clean:
	rm -f origin_engine origin_editor game/bin/client.so game_logic_demo tools/map_compiler $(SHADER_DIR)/*.spv

map_compiler: tools/map_compiler.c
	$(CC) $^ -o tools/map_compiler $(CFLAGS)

map_parser: parse_map.c
	$(CC) $^ -o parse_map $(CFLAGS)