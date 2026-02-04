#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include "tileset.h"

// ----------------------
// Tile indices (your order)
// wall2, wall, door, floor
// ----------------------
#define TILE_WALL2 0
#define TILE_WALL  1
#define TILE_DOOR  2
#define TILE_FLOOR 3

#define MAP_W 20
#define MAP_H 18

static UINT8 room_map[MAP_W * MAP_H];

// GB sprite origin offset: screen->tile
static UINT8 to_tile_x(INT16 px) { return (UINT8)((px - 8) >> 3); }
static UINT8 to_tile_y(INT16 py) { return (UINT8)((py - 16) >> 3); }

static UINT8 tile_at(UINT8 tx, UINT8 ty) {
    if (tx >= MAP_W || ty >= MAP_H) return TILE_WALL2;
    return room_map[(UINT16)ty * MAP_W + tx];
}

static UINT8 is_solid_tile(UINT8 t) {
    return (t == TILE_WALL2) || (t == TILE_WALL);
}

static UINT8 is_solid_at_pxpy(INT16 px, INT16 py) {
    UINT8 tx = to_tile_x(px);
    UINT8 ty = to_tile_y(py);
    return is_solid_tile(tile_at(tx, ty));
}

static void draw_room_to_bkg(void) {
    set_bkg_tiles(0, 0, MAP_W, MAP_H, room_map);
}

// ----------------------
// Fixed room layout
// ----------------------
static void build_fixed_room(void) {
    UINT8 x, y;

    // Fill with floor
    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            room_map[(UINT16)y * MAP_W + x] = TILE_FLOOR;
        }
    }

    // Top strip (wall2)
    for (x = 0; x < MAP_W; x++) room_map[0 * MAP_W + x] = TILE_WALL2;

    // Back wall row under it (wall)
    for (x = 0; x < MAP_W; x++) room_map[1 * MAP_W + x] = TILE_WALL;

    // Door at top middle
    room_map[1 * MAP_W + (MAP_W / 2)] = TILE_DOOR;

    // Bottom strips: row 16 wall, row 17 wall2
    for (x = 0; x < MAP_W; x++) {
        room_map[(MAP_H - 2) * MAP_W + x] = TILE_WALL;
        room_map[(MAP_H - 1) * MAP_W + x] = TILE_WALL2;
    }

    // Side framing
    for (y = 2; y < MAP_H - 2; y++) {
        room_map[(UINT16)y * MAP_W + 0] = TILE_WALL2;
        room_map[(UINT16)y * MAP_W + 1] = TILE_WALL;
        room_map[(UINT16)y * MAP_W + (MAP_W - 2)] = TILE_WALL;
        room_map[(UINT16)y * MAP_W + (MAP_W - 1)] = TILE_WALL2;
    }

    draw_room_to_bkg();
}

static void ui_draw(UINT8 hp, UINT8 room_id, UINT8 focus) {
    gotoxy(0, 0);
    printf("HP:%u ROOM:%u F:%u ", hp, room_id, focus);
}

static void ui_show_death(void) {
    gotoxy(3, 8);
    printf("YOU DIED");
    gotoxy(1, 10);
    printf("PRESS START");
}

static void ui_clear_death_text(void) {
    gotoxy(0, 8);  printf("                ");
    gotoxy(0, 9);  printf("                ");
    gotoxy(0,10);  printf("                ");
}

// Slightly shrunk overlap for 8x8 sprites
static UINT8 overlaps8_i16(INT16 ax, INT16 ay, INT16 bx, INT16 by) {
    return (ax + 6 > bx) && (ax < bx + 6) && (ay + 6 > by) && (ay < by + 6);
}

static INT8 sign_i16(INT16 v) {
    if (v < 0) return -1;
    if (v > 0) return 1;
    return 0;
}

// radius check (use INT32 so SDCC stops whining)
static UINT8 within_radius_i16(INT16 ax, INT16 ay, INT16 bx, INT16 by, UINT8 r) {
    INT16 dx = ax - bx;
    INT16 dy = ay - by;
    INT32 ddx = (INT32)dx;
    INT32 ddy = (INT32)dy;
    INT32 rr  = (INT32)r * (INT32)r;
    return (ddx*ddx + ddy*ddy) <= rr;
}

// ----------------------
// Simple 8x8 sprite tiles
// ----------------------
static const unsigned char monk_sprite_tile[] = {
    0x00,0x00,
    0x3C,0x00,
    0x7E,0x00,
    0xFF,0x00,
    0xFF,0x00,
    0x7E,0x00,
    0x3C,0x00,
    0x00,0x00
};

static const unsigned char spirit_sprite_tile[] = {
    0x00,0x00,
    0x3C,0x3C,
    0x7E,0x7E,
    0xDB,0xDB,
    0xFF,0xFF,
    0x7E,0x7E,
    0x3C,0x3C,
    0x00,0x00
};

/*
  KI BLUE FIX:
  Put the shape in bitplane1 (high bytes) so it uses colour index 2.
  Each row is (low_byte, high_byte). We keep low_byte = 0x00, move data into high_byte.
*/
static const unsigned char ki_sprite_tile[] = {
    0x00,0x00,
    0x00,0x18,
    0x00,0x3C,
    0x00,0x7E,
    0x00,0x7E,
    0x00,0x3C,
    0x00,0x18,
    0x00,0x00
};

static const uint16_t spr_palette[] = {
    RGB8(0,0,0),
    RGB8(200,170,110),  // monk
    RGB8(40,120,255),   // ki BLUE
    RGB8(150,90,170)    // spirit
};

static const uint16_t spr_palette_pulse[] = {
    RGB8(0,0,0),
    RGB8(255, 60, 200), // monk flashes hot pink-ish
    RGB8(40,120,255),   // ki stays blue
    RGB8(150,90,170)
};

// Directions for shooting
#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

int main(void) {
    // --- core state ---
    INT16 px = 80, py = 88;
    UINT8 last_dir = DIR_DOWN;

    UINT8 hp = 3;
    UINT8 room_id = 1;

    UINT8 is_dead = 0;
    UINT8 inv_frames = 0;
    UINT8 kb_frames = 0;
    INT8  kbx = 0, kby = 0;

    // Enemy
    UINT8 spirit_active = 1;
    INT16 sx = 40, sy = 72;

    UINT8 spirit_step_timer = 0;
    UINT8 spirit_stun = 0;

    UINT8 spirit_kb_frames = 0;
    INT8 skx = 0, sky = 0;

    // Spirit respawn delay (so shots feel meaningful)
    UINT8 spirit_respawn_timer = 0;

    // Shooting (Ki)
    UINT8 ki_active = 0;
    INT16 kx = 0, ky = 0;
    INT8 kdx = 0, kdy = 0;

    // AoE pulse (B)
    UINT8 pulse_cooldown = 0;
    UINT8 pulse_active_frames = 0;
    UINT8 pulse_hit_latch = 0; // prevents multi-hit spam per pulse

    // SPECIAL MOVE (SELECT): big AoE
    UINT8 focus = 0;              // 0..3
    UINT8 special_cooldown = 0;   // separate cooldown so it feels "special"
    UINT8 special_active_frames = 0;

    // Input edge detection
    UINT8 prev_keys = 0;

    DISPLAY_OFF;

    SPRITES_8x8;
    SHOW_BKG;
    SHOW_SPRITES;

    printf("");

    // ---- Load BG tiles from png2asset ----
    set_bkg_data(tileset_TILE_ORIGIN, tileset_TILE_COUNT, tileset_tiles);

    // ---- CGB palette: make sure BG palette 0 isn't black ----
    if (_cpu == CGB_TYPE) {
        palette_color_t pal0[4] = {
            tileset_palettes[4], tileset_palettes[5], tileset_palettes[6], tileset_palettes[7]
        };
        set_bkg_palette(0, 1, pal0);
        set_sprite_palette(0, 1, spr_palette);
    }

    // ---- Load sprite graphics ----
    // sprite tile ids: 0 monk, 1 spirit, 2 ki
    set_sprite_data(0, 1, monk_sprite_tile);
    set_sprite_data(1, 1, spirit_sprite_tile);
    set_sprite_data(2, 1, ki_sprite_tile);

    set_sprite_tile(0, 0);
    set_sprite_tile(1, 1);
    set_sprite_tile(2, 2);

    // Start hidden ki
    move_sprite(2, 0, 0);

    build_fixed_room();
    ui_draw(hp, room_id, focus);

    move_sprite(0, (UINT8)px, (UINT8)py);
    move_sprite(1, (UINT8)sx, (UINT8)sy);

    DISPLAY_ON;

    while (1) {
        UINT8 keys = joypad();
        UINT8 pressed = (keys ^ prev_keys) & keys;
        prev_keys = keys;

        if (is_dead) {
            if (pressed & J_START) {
                is_dead = 0;
                hp = 3;
                inv_frames = 30;
                kb_frames = 0;

                px = 80; py = 88;
                sx = 40; sy = 72;
                spirit_active = 1;

                ki_active = 0;
                move_sprite(2, 0, 0);

                spirit_stun = 20;
                spirit_kb_frames = 0;
                spirit_respawn_timer = 0;

                pulse_cooldown = 0;
                pulse_active_frames = 0;
                pulse_hit_latch = 0;

                focus = 0;
                special_cooldown = 0;
                special_active_frames = 0;

                ui_clear_death_text();

                build_fixed_room();
                ui_draw(hp, room_id, focus);
            }

            wait_vbl_done();
            continue;
        }

        // Timers
        if (inv_frames) inv_frames--;
        if (spirit_step_timer) spirit_step_timer--;
        if (spirit_stun) spirit_stun--;
        if (pulse_cooldown) pulse_cooldown--;
        if (pulse_active_frames) pulse_active_frames--;
        if (kb_frames) kb_frames--;

        if (spirit_kb_frames) spirit_kb_frames--;
        if (spirit_respawn_timer) spirit_respawn_timer--;

        if (special_cooldown) special_cooldown--;
        if (special_active_frames) special_active_frames--;

        // reset pulse latch when pulse window ends
        if (pulse_active_frames == 0) pulse_hit_latch = 0;

        // ----------------------
        // Palette feedback (pink flash) during pulse OR special
        // ----------------------
        if (_cpu == CGB_TYPE) {
            if (pulse_active_frames || special_active_frames) set_sprite_palette(0, 1, spr_palette_pulse);
            else set_sprite_palette(0, 1, spr_palette);
        }

        // ----------------------
        // Player movement + last_dir
        // ----------------------
        if (kb_frames) {
            INT16 npx = px + kbx;
            INT16 npy = py + kby;

            if (!is_solid_at_pxpy(npx, py)) px = npx;
            if (!is_solid_at_pxpy(px, npy)) py = npy;
        } else {
            INT8 dx = 0, dy = 0;

            if (keys & J_LEFT)  { dx = -1; last_dir = DIR_LEFT; }
            if (keys & J_RIGHT) { dx =  1; last_dir = DIR_RIGHT; }
            if (keys & J_UP)    { dy = -1; last_dir = DIR_UP; }
            if (keys & J_DOWN)  { dy =  1; last_dir = DIR_DOWN; }

            if (dx) {
                INT16 npx = px + dx;
                if (!is_solid_at_pxpy(npx, py)) px = npx;
            }
            if (dy) {
                INT16 npy = py + dy;
                if (!is_solid_at_pxpy(px, npy)) py = npy;
            }
        }

        // ----------------------
        // SPECIAL MOVE on SELECT: big AoE (spend focus)
        // ----------------------
        if ((pressed & J_SELECT) && (focus > 0) && (special_cooldown == 0)) {
            special_cooldown = 120;           // longer cooldown
            special_active_frames = 12;       // flash window
            // Spend all focus
            focus = 0;
            ui_draw(hp, room_id, focus);

            // Apply immediately (big radius + stronger knock)
            if (spirit_active && within_radius_i16(px, py, sx, sy, 32)) {
                INT16 dxp = sx - px;
                INT16 dyp = sy - py;

                skx = (INT8)(sign_i16(dxp) * 4);
                sky = (INT8)(sign_i16(dyp) * 4);
                spirit_kb_frames = 12;
                spirit_stun = 40;
            }
        }

        // ----------------------
        // AoE pulse on B: knockback + stun
        // ----------------------
        if ((pressed & J_B) && (pulse_cooldown == 0)) {
            pulse_cooldown = 90;
            pulse_active_frames = 10;
            pulse_hit_latch = 0;
        }

        // apply pulse ONCE per activation
        if (spirit_active && pulse_active_frames && !pulse_hit_latch) {
            if (within_radius_i16(px, py, sx, sy, 18)) {
                INT16 dxp = sx - px;
                INT16 dyp = sy - py;

                skx = (INT8)(sign_i16(dxp) * 3);
                sky = (INT8)(sign_i16(dyp) * 3);
                spirit_kb_frames = 8;
                spirit_stun = 25;

                pulse_hit_latch = 1;
            }
        }

        // ----------------------
        // Fire ki on A
        // ----------------------
        if (!ki_active && (pressed & J_A)) {
            ki_active = 1;
            kx = px;
            ky = py;

            kdx = 0; kdy = 0;
            if (last_dir == DIR_LEFT)  kdx = -2;
            if (last_dir == DIR_RIGHT) kdx =  2;
            if (last_dir == DIR_UP)    kdy = -2;
            if (last_dir == DIR_DOWN)  kdy =  2;

            move_sprite(2, (UINT8)kx, (UINT8)ky);
        }

        // Update ki
        if (ki_active) {
            INT16 nkx = kx + kdx;
            INT16 nky = ky + kdy;

            if (nkx < 1 || nkx > 167 || nky < 1 || nky > 151 || is_solid_at_pxpy(nkx, nky)) {
                ki_active = 0;
                move_sprite(2, 0, 0);
            } else {
                kx = nkx; ky = nky;
                move_sprite(2, (UINT8)kx, (UINT8)ky);
            }
        }

        // ----------------------
        // Spirit movement + respawn
        // ----------------------
        if (!spirit_active) {
            if (spirit_respawn_timer == 0) {
                move_sprite(1, 0, 0);
            } else if (spirit_respawn_timer == 1) {
                sx = 40; sy = 72;
                spirit_active = 1;
                spirit_stun = 20;
                spirit_step_timer = 10;
                move_sprite(1, (UINT8)sx, (UINT8)sy);
            }
        } else {
            if (spirit_kb_frames) {
                INT16 nsx = sx + skx;
                INT16 nsy = sy + sky;

                if (!is_solid_at_pxpy(nsx, sy)) sx = nsx;
                if (!is_solid_at_pxpy(sx, nsy)) sy = nsy;
            } else if (!spirit_stun) {
                if (spirit_step_timer == 0) {
                    spirit_step_timer = 4;

                    if (sx < px && !is_solid_at_pxpy(sx + 1, sy)) sx++;
                    else if (sx > px && !is_solid_at_pxpy(sx - 1, sy)) sx--;

                    if (sy < py && !is_solid_at_pxpy(sx, sy + 1)) sy++;
                    else if (sy > py && !is_solid_at_pxpy(sx, sy - 1)) sy--;
                }
            }
        }

        // ----------------------
        // Ki hits spirit: "seal"
        // ----------------------
        if (spirit_active && ki_active) {
            if (overlaps8_i16(kx, ky, sx, sy)) {
                spirit_active = 0;
                ki_active = 0;
                move_sprite(2, 0, 0);

                // Build focus up to 3
                if (focus < 3) focus++;
                ui_draw(hp, room_id, focus);

                spirit_respawn_timer = 40;
                move_sprite(1, 0, 0);
            }
        }

        // ----------------------
        // Spirit hits player
        // ----------------------
        if (spirit_active && inv_frames == 0 && overlaps8_i16(px, py, sx, sy)) {
            if (hp > 0) hp--;
            ui_draw(hp, room_id, focus);

            if (hp == 0) {
                is_dead = 1;
                ui_show_death();
            } else {
                inv_frames = 60;
                kb_frames = 12;
                kbx = sign_i16(px - sx) * 2;
                kby = sign_i16(py - sy) * 2;
            }
        }

        // ----------------------
        // Render sprites
        // ----------------------
        if (inv_frames && (inv_frames & 1)) move_sprite(0, 0, 0);
        else move_sprite(0, (UINT8)px, (UINT8)py);

        if (spirit_active) move_sprite(1, (UINT8)sx, (UINT8)sy);
        else move_sprite(1, 0, 0);

        wait_vbl_done();
    }
}
