#!/usr/bin/env python3
"""
gen_textures.py — Procedural texture generator for Origin Engine
Usage: python3 tools/gen_textures.py [output_dir] [size]

All textures are pure noise/math — no manual pixel placement.
"""
import sys, os, random, math
from PIL import Image, ImageDraw, ImageFilter

SIZE = 256
OUT_DIR = "textures"

def noise_grid(w, h, freq, seed):
    """Smooth tileable value noise at given frequency."""
    r = random.Random(seed)
    gw, gh = max(2, int(freq)), max(2, int(freq))
    # Grid wraps: grid[gy % gh][gx % gw]
    grid = [[r.random() for _ in range(gw)] for _ in range(gh)]
    img = Image.new("F", (w, h))
    for y in range(h):
        for x in range(w):
            gx = (x / w) * gw
            gy = (y / h) * gh
            ix, iy = int(gx), int(gy)
            fx = gx - ix; fy = gy - iy
            fx = fx*fx*(3-2*fx); fy = fy*fy*(3-2*fy)
            # Wrap indices for seamless tiling
            ix0 = ix % gw; ix1 = (ix + 1) % gw
            iy0 = iy % gh; iy1 = (iy + 1) % gh
            top = grid[iy0][ix0]*(1-fx) + grid[iy0][ix1]*fx
            bot = grid[iy1][ix0]*(1-fx) + grid[iy1][ix1]*fx
            img.putpixel((x, y), top*(1-fy) + bot*fy)
    return img

def fbm(w, h, octaves=5, seed=0, base_freq=4, lac=2.0, gain=0.5):
    """Fractal noise."""
    out = Image.new("F", (w, h), 0.0)
    amp, freq, total = 1.0, base_freq, 0.0
    for o in range(octaves):
        layer = noise_grid(w, h, freq, seed + o*137)
        for y in range(h):
            for x in range(w):
                out.putpixel((x, y), out.getpixel((x, y)) + layer.getpixel((x, y)) * amp)
        total += amp; amp *= gain; freq *= lac
    for y in range(h):
        for x in range(w):
            out.putpixel((x, y), out.getpixel((x, y)) / total)
    return out

def apply_color(noise_img, dark, light):
    """Map noise 0..1 to color gradient."""
    w, h = noise_img.size
    img = Image.new("RGB", (w, h))
    for y in range(h):
        for x in range(w):
            t = max(0.0, min(1.0, noise_img.getpixel((x, y))))
            img.putpixel((x, y), tuple(int(dark[i] + (light[i]-dark[i])*t) for i in range(3)))
    return img

def blend_rgb(a, b, factor):
    """Blend two RGB images."""
    return Image.blend(a, b, factor)

def multiply_noise(img, noise_img, strength=0.3):
    """Multiply RGB image by noise for variation."""
    w, h = img.size
    out = img.copy()
    for y in range(h):
        for x in range(w):
            v = noise_img.getpixel((x, y))
            f = 1.0 - strength + v * strength * 2
            c = img.getpixel((x, y))
            out.putpixel((x, y), tuple(max(0, min(255, int(c[i]*f))) for i in range(3)))
    return out

def add_border(img, width=0, color=(40, 40, 40)):
    """Border disabled by default for seamless tiling."""
    if width <= 0:
        return img
    draw = ImageDraw.Draw(img)
    w, h = img.size
    for i in range(width):
        draw.line([(w-1-i,0),(w-1-i,h-1)], fill=color)
        draw.line([(0,h-1-i),(w-1,h-1-i)], fill=color)
    return img


# ── Textures (all noise/math based, no manual pixels) ────────

def gen_concrete(size):
    n1 = fbm(size, size, 5, seed=42, base_freq=8)
    n2 = noise_grid(size, size, 64, 43)  # fine detail
    img = Image.new("RGB", (size, size))
    for y in range(size):
        for x in range(size):
            coarse = n1.getpixel((x, y))
            fine = n2.getpixel((x, y))
            v = 130 + coarse * 25 + fine * 15
            img.putpixel((x, y), (int(v), int(v-1), int(v-2)))
    # Darken with low-freq noise for stain patches
    stain = noise_grid(size, size, 6, 44)
    img = multiply_noise(img, stain, 0.12)
    img = img.filter(ImageFilter.SMOOTH)
    return add_border(img)

# MARK: gen wall
def gen_wall(size, base_color=(40, 45, 120)):
    n1 = fbm(size, size, 5, seed=42, base_freq=128)
    n2 = noise_grid(size, size, 64, 128)  # fine detail
    img = Image.new("RGB", (size, size))
    br, bg, bb = base_color
    for y in range(size):
        for x in range(size):
            coarse = n1.getpixel((x, y))
            fine = n2.getpixel((x, y))
            v = 130 + coarse * 25 + fine * 15
            vv = v / 128
            r = int(max(0, min(255, br * vv)))
            g = int(max(0, min(255, bg * vv)))
            b = int(max(0, min(255, bb * vv)))
            img.putpixel((x, y), (r, g, b))
    # Darken with low-freq noise for stain patches
    #stain = noise_grid(size, size, 6, 44)
    #img = multiply_noise(img, stain, 0.12)
    img = img.filter(ImageFilter.SMOOTH)
    return add_border(img)


def gen_grass(size):
    # Multiple octaves at different greens, blended
    n1 = fbm(size, size, 6, seed=10, base_freq=6)
    n2 = fbm(size, size, 6, seed=11, base_freq=16)  # high freq detail
    n3 = noise_grid(size, size, 4, 12)  # color patches
    img = Image.new("RGB", (size, size))
    for y in range(size):
        for x in range(size):
            v1 = n1.getpixel((x, y))
            v2 = n2.getpixel((x, y))
            v3 = n3.getpixel((x, y))
            # Mix dark and bright green based on noise
            bright = v1 * 0.5 + v2 * 0.5
            r = int(30 + bright * 55 + v3 * 15)
            g = int(65 + bright * 110 + v3 * 20)
            b = int(15 + bright * 30 + v3 * 10)
            img.putpixel((x, y), (min(255,r), min(255,g), min(255,b)))
    img = img.filter(ImageFilter.SHARPEN)
    return add_border(img)


def gen_dirt(size):
    n1 = fbm(size, size, 6, seed=55, base_freq=8)
    n2 = fbm(size, size, 5, seed=56, base_freq=24)
    n3 = noise_grid(size, size, 64, 57)
    n4 = noise_grid(size, size, 128, 58)  # extra fine grain
    img = Image.new("RGB", (size, size))
    for y in range(size):
        for x in range(size):
            v1 = n1.getpixel((x, y))
            v2 = n2.getpixel((x, y))
            v3 = n3.getpixel((x, y))
            v4 = n4.getpixel((x, y))
            v = v1 * 0.3 + v2 * 0.3 + v3 * 0.2 + v4 * 0.2
            r = int(80 + v * 90)
            g = int(58 + v * 65)
            b = int(30 + v * 38)
            img.putpixel((x, y), (min(255,r), min(255,g), min(255,b)))
    return add_border(img)


def gen_bricks(size):
    bh = size // 8
    bw = size // 4
    brick_noise = fbm(size, size, 5, seed=78, base_freq=16)
    fine_noise = noise_grid(size, size, 64, 79)
    mortar_noise = noise_grid(size, size, 32, 80)
    img = Image.new("RGB", (size, size))

    for y in range(size):
        for x in range(size):
            row = y // bh
            offset = (bw // 2) if row % 2 else 0
            col = (x - offset) // bw

            local_y = y % bh
            local_x = (x - offset) % bw
            if local_x < 0: local_x += bw
            dy = min(local_y, bh - 1 - local_y)
            dx = min(local_x, bw - 1 - local_x)
            mortar_dist = min(dx, dy)

            if mortar_dist <= 2:
                # Darker mortar with noise
                mv = mortar_noise.getpixel((x, y))
                v = int(60 + mv * 30)
                img.putpixel((x, y), (v, int(v*0.93), int(v*0.85)))
            else:
                bn = brick_noise.getpixel((x, y))
                fn = fine_noise.getpixel((x, y))
                rng = random.Random(col * 31 + row * 17)
                base_r = rng.randint(140, 200)
                base_g = rng.randint(55, 90)
                base_b = rng.randint(40, 75)
                # Stronger edge darkening
                edge_f = min(1.0, (mortar_dist - 2) / 4.0)
                v = bn * 0.5 + fn * 0.5
                r = int(base_r * (0.75 + v * 0.3) * (0.78 + edge_f * 0.22))
                g = int(base_g * (0.75 + v * 0.3) * (0.78 + edge_f * 0.22))
                b = int(base_b * (0.75 + v * 0.3) * (0.78 + edge_f * 0.22))
                img.putpixel((x, y), (min(255,r), min(255,g), min(255,b)))
    return add_border(img)


def gen_metal(size):
    """Brushed metal: anisotropic noise (stretched in X) + subtle variation."""
    # Create horizontally-stretched noise by using different X/Y frequencies
    n1 = noise_grid(size, size, 64, 88)   # medium detail
    n2 = noise_grid(size, size, 16, 89)   # broad shading
    n3 = fbm(size, size, 4, seed=90, base_freq=8)  # color variation
    # Anisotropic: average neighboring pixels horizontally for brush effect
    img = Image.new("RGB", (size, size))
    for y in range(size):
        for x in range(size):
            # Sample noise at multiple X offsets and average for horizontal smear
            total = 0
            for dx in range(-3, 4):
                sx = (x + dx) % size
                total += n1.getpixel((sx, y))
            h = total / 7.0
            v = n2.getpixel((x, y))
            c = n3.getpixel((x, y))
            base = 120 + v * 25 + h * 20 + c * 10
            val = int(max(0, min(255, base)))
            # Slight blue-grey tint
            img.putpixel((x, y), (max(0,val-3), val-1, min(255,val+2)))
    return add_border(img)


def gen_wood(size):
    """Wood planks: subtle grain, less wavy, more natural color variation."""
    plank_count = 3
    pw = size // plank_count
    gap = 2
    img = Image.new("RGB", (size, size), (50, 35, 20))

    for p in range(plank_count):
        px0 = p * pw + gap
        px1 = (p + 1) * pw - gap
        plank_w = px1 - px0
        if plank_w <= 0: continue
        seed = 330 + p * 50
        n1 = noise_grid(plank_w, size, 8, seed)
        n2 = noise_grid(plank_w, size, 24, seed+1)
        n3 = noise_grid(plank_w, size, 48, seed+2)
        rng = random.Random(seed)
        base_r = rng.randint(130, 175)
        base_g = rng.randint(88, 120)
        base_b = rng.randint(48, 72)

        for y in range(size):
            for x in range(px0, min(px1, size)):
                lx = x - px0
                nv1 = n1.getpixel((lx, y))
                nv2 = n2.getpixel((lx, y))
                nv3 = n3.getpixel((lx, y))
                # Gentle grain: low amplitude sine, less distortion
                grain = math.sin((y + nv1 * 10) * 0.06) * 0.25 + 0.5
                v = grain * 0.4 + nv1 * 0.2 + nv2 * 0.2 + nv3 * 0.2
                r = int(base_r * (0.6 + v * 0.5))
                g = int(base_g * (0.6 + v * 0.5))
                b = int(base_b * (0.6 + v * 0.5))
                img.putpixel((x, y), (min(255,r), min(255,g), min(255,b)))
    return add_border(img)


def gen_sand(size):
    # Three frequencies blended for fine grain look
    n1 = fbm(size, size, 6, seed=111, base_freq=8)
    n2 = noise_grid(size, size, 64, 112)
    n3 = noise_grid(size, size, 128, 113)
    img = Image.new("RGB", (size, size))
    for y in range(size):
        for x in range(size):
            v1 = n1.getpixel((x, y))
            v2 = n2.getpixel((x, y))
            v3 = n3.getpixel((x, y))
            v = v1 * 0.4 + v2 * 0.35 + v3 * 0.25
            r = int(180 + v * 55)
            g = int(160 + v * 45)
            b = int(110 + v * 28)
            img.putpixel((x, y), (min(255,r), min(255,g), min(255,b)))
    return add_border(img)


def gen_stone(size):
    """Stone: layered noise with more contrast and color variation."""
    n1 = fbm(size, size, 6, seed=44, base_freq=32)
    n2 = noise_grid(size, size, 48, 45)
    n3 = noise_grid(size, size, 96, 46)
    n4 = noise_grid(size, size, 8, 47)  # large patches
    img = Image.new("RGB", (size, size))
    for y in range(size):
        for x in range(size):
            v1 = n1.getpixel((x, y))
            v2 = n2.getpixel((x, y))
            v3 = n3.getpixel((x, y))
            v4 = n4.getpixel((x, y))
            # Blend with more weight on high-freq
            v = v1 * 0.3 + v2 * 0.3 + v3 * 0.2 + v4 * 0.2
            # Stronger contrast
            v = (v - 0.5) * 2.0 + 0.5
            v = max(0, min(1, v))
            # Warm/cool patches from large-scale noise
            warm = (v4 - 0.5) * 0.12
            base = 85 + v * 100
            r = int(min(255, base + warm * 50 + 5))
            g = int(min(255, base + warm * 25))
            b = int(min(255, base - warm * 15 - 4))
            img.putpixel((x, y), (r, g, b))
    return add_border(img)


def gen_tile(size):
    ts = size // 4
    grout = 3
    n = noise_grid(size, size, 48, 55)
    n2 = noise_grid(size, size, 8, 56)  # per-tile color variation
    img = Image.new("RGB", (size, size))

    for y in range(size):
        for x in range(size):
            ty = y // ts
            tx = x // ts
            local_y = y % ts
            local_x = x % ts
            dy = min(local_y, ts - 1 - local_y)
            dx = min(local_x, ts - 1 - local_x)
            mortar_dist = min(dx, dy)

            if mortar_dist <= grout:
                # Grout: dark, noisy
                gv = n.getpixel((x, y))
                v = int(110 + gv * 25)
                img.putpixel((x, y), (v, int(v*0.97), int(v*0.92)))
            else:
                # Tile surface with bevel
                bevel = min(1.0, (mortar_dist - grout) / 4.0)
                tile_var = n2.getpixel((x, y))
                surface = n.getpixel((x, y))
                rng = random.Random(tx * 7 + ty * 13)
                base = rng.randint(195, 225)
                v = int(base * (0.88 + bevel * 0.12) + surface * 10 - 5 + tile_var * 8)
                v = max(0, min(255, v))
                img.putpixel((x, y), (v, v, v - 2))
    return add_border(img)


def gen_water(size):
    """Water: smooth dark blue with soft lighter ripple patterns."""
    n1 = fbm(size, size, 5, seed=200, base_freq=10)
    n2 = fbm(size, size, 5, seed=201, base_freq=14)
    n3 = fbm(size, size, 3, seed=202, base_freq=6)
    img = Image.new("RGB", (size, size))
    for y in range(size):
        for x in range(size):
            v1 = n1.getpixel((x, y))
            v2 = n2.getpixel((x, y))
            v3 = n3.getpixel((x, y))
            # Soft ripple: smooth combination, not harsh threshold
            ripple = (v1 * 0.5 + v2 * 0.5)
            depth = v3 * 0.2 + 0.8
            r = int(min(255, 10 + ripple * 20 * depth))
            g = int(min(255, 30 + ripple * 50 * depth))
            b = int(min(255, 80 + ripple * 100 * depth))
            img.putpixel((x, y), (r, g, b))
    img = img.filter(ImageFilter.SMOOTH)
    return add_border(img)


def gen_lava(size):
    n1 = fbm(size, size, 5, seed=99, base_freq=4)
    n2 = fbm(size, size, 3, seed=100, base_freq=8)
    img = Image.new("RGB", (size, size))
    for y in range(size):
        for x in range(size):
            v1 = n1.getpixel((x, y))
            v2 = n2.getpixel((x, y))
            heat = max(0, v1 * 2 - 0.7) ** 0.5
            rock = v2 * 0.3
            r = int(min(255, 40 + heat * 250 + rock * 30))
            g = int(min(255, 15 + heat * 150 * max(0, heat - 0.3)))
            b = int(min(255, 10 + heat * 20))
            img.putpixel((x, y), (r, g, b))
    img = img.filter(ImageFilter.SMOOTH)
    return add_border(img)

# MARK: gen carpet

def gen_carpet(size, base_color=(40, 45, 120)):
    """Carpet: smooth color base (like water) with fine rough noise for fibers."""
    n1 = fbm(size, size, 5, seed=300, base_freq=10)
    n2 = fbm(size, size, 5, seed=301, base_freq=14)
    # Fine fiber noise
    n3 = noise_grid(size, size, 96, 302)
    n4 = noise_grid(size, size, 128, 303)
    img = Image.new("RGB", (size, size))
    br, bg, bb = base_color
    for y in range(size):
        for x in range(size):
            v1 = n1.getpixel((x, y))
            v2 = n2.getpixel((x, y))
            v3 = n3.getpixel((x, y))
            v4 = n4.getpixel((x, y))
            # Smooth base variation
            smooth = v1 * 0.5 + v2 * 0.5
            # Rough fiber texture
            fiber = (v3 * 0.5 + v4 * 0.5 - 0.5) * 0.35
            v = 0.7 + smooth * 0.3 + fiber
            r = int(max(0, min(255, br * v)))
            g = int(max(0, min(255, bg * v)))
            b = int(max(0, min(255, bb * v)))
            img.putpixel((x, y), (r, g, b))
    return add_border(img)


# ── Tool textures ─────────────────────────────────────────────

def gen_nodraw(size):
    img = Image.new("RGBA", (size, size), (180, 0, 0, 255))
    draw = ImageDraw.Draw(img)
    draw.line([(0,0),(size,size)], fill=(220,30,30,255), width=3)
    draw.line([(size,0),(0,size)], fill=(220,30,30,255), width=3)
    for i in range(0, size, size//4):
        draw.line([(i,0),(i,size)], fill=(150,0,0,255), width=1)
        draw.line([(0,i),(size,i)], fill=(150,0,0,255), width=1)
    return img

def gen_trigger(size):
    img = Image.new("RGBA", (size, size), (180, 80, 240, 80))
    draw = ImageDraw.Draw(img)
    for i in range(0, size, size//8):
        draw.line([(i,0),(i,size)], fill=(160,60,220,100), width=1)
        draw.line([(0,i),(size,i)], fill=(160,60,220,100), width=1)
    return img

def gen_clip(size, color):
    img = Image.new("RGBA", (size, size), color)
    draw = ImageDraw.Draw(img)
    step = size // 8
    for i in range(-size, size*2, step):
        draw.line([(i,0),(i+size,size)], fill=(*color[:3], min(255,color[3]+40)), width=1)
    return img


#TEXTURES = {
#    "concrete":    gen_concrete,
#    "grass_gen":   gen_grass,
#    "dirt_gen":    gen_dirt,
#    "bricks_gen":  gen_bricks,
#    "metal":       gen_metal,
#    "wood":        gen_wood,
#    "sand":        gen_sand,
#    "stone":       gen_stone,
#    "tile":        gen_tile,
#    "water":       gen_water,
#    "carpet":      lambda s: gen_carpet(s, (40, 45, 120)),
#    "carpet_red":  lambda s: gen_carpet(s, (120, 35, 35)),
#    "carpet_green":lambda s: gen_carpet(s, (35, 90, 45)),
#    "lava":        gen_lava,
#    "nodraw_gen":  gen_nodraw,
#    "trigger_gen": gen_trigger,
#    "plrclip_gen": lambda s: gen_clip(s, (255, 200, 0, 80)),
#    "entclip_gen": lambda s: gen_clip(s, (0, 200, 255, 80)),
#}
TEXTURES = {
    "wall_blue":  lambda s: gen_wall(s, (40, 45, 120)),
    "wall_red":   lambda s: gen_wall(s, (120, 35, 35)),
    "wall_green": lambda s: gen_wall(s, (35, 90, 45)),
    "wall_office": lambda s: gen_wall(s, (76, 100, 49)),
}

def main():
    global SIZE, OUT_DIR
    if len(sys.argv) > 1: OUT_DIR = sys.argv[1]
    if len(sys.argv) > 2: SIZE = int(sys.argv[2])
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Generating {len(TEXTURES)} textures ({SIZE}x{SIZE}) to {OUT_DIR}/")
    for name, gen_func in TEXTURES.items():
        img = gen_func(SIZE)
        path = os.path.join(OUT_DIR, f"{name}.png")
        img.save(path)
        print(f"  {path}")
    print("Done!")

if __name__ == "__main__":
    main()
