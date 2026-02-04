#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/console.h>
#include <stdio.h>

#include "tileset.h"

// =====================
// Constants / Layout
// =====================

#define MAP_W 20
#define MAP_H 18
#define UI_ROWS 1   // top row reserved for console UI text

// IMPORTANT: set these to match the order of tiles in bg_tiles.png
// Example order assumption: [door, walls, walls_2, floor]
#define TILE_DOOR    0
#define TILE_WALL    1
#define TILE_WALL2   2
#define TILE_FLOOR   3

// Darker, moodier palette (only matters on CGB)
const uint16_t bg_palette[] = {
    RGB8(5, 8, 10),
    RGB8(30, 60, 70),
    RGB8(60, 40, 70),
    RGB8(160, 170, 180)
};

// =====================
// Sprites
// =====================

const unsigned char monk_sprite_tile[] = {
    0x00,0x00,
    0x3C,0x00,
    0x7E,0x00,
    0xFF,0x00,
    0xFF,0x00,
    0x7E,0x00,
    0x3C,0x00,
    0x00,0x00
};

const unsigned char ki_sprite_tile[] = {
    0x00,0x00,
    0x00,0x18,
    0x00,0x3C,
    0x00,0x7E,
    0x00,0x7E,
    0x00,0x3C,
    0x00,0x18,
    0x00,0x00
};

const unsigned char spirit_sprite_tile[] = {
    0x00,0x00,
    0x3C,0x3C,
    0x7E,0x7E,
    0xDB,0xDB,
    0xFF,0xFF,
    0x7E,0x7E,
    0x3C,0x3C,
    0x00,0x00
};

const uint16_t spr_palette_normal[] = {
    RGB8(0, 0, 0),
    RGB8(200, 170, 110),
    RGB8(120, 190, 210),
    RGB8(150, 90, 170)
};

const uint16_t spr_palette_pulse[] = {
    RGB8(0, 0, 0),
    RGB8(190, 110, 190),
    RGB8(120, 190, 210),
    RGB8(150, 90, 170)
};

#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

// =====================
// Room / Map
// =====================

UINT8 room_map[MAP_W * MAP_H];

// Simple LFSR RNG
UINT16 rng_state = 0xACE1u;
UINT8 rng8(void) {
    UINT16 lsb = rng_state & 1u;
    rng_state >>= 1;
    if (lsb) rng_state ^= 0xB400u;
    return (UINT8)(rng_state & 0xFF);
}

UINT8 tile_at(UINT8 tx, UINT8 ty) {
    if (tx >= MAP_W || ty >= MAP_H) return TILE_WALL;
    return room_map[(UINT16)ty * MAP_W + tx];
}

UINT8 is_solid_tile(UINT8 t) {
    // only the back wall is solid for now
    return (t == TILE_WALL);
}

// Convert sprite position to tile coords (sprites use origin offset 8,16)
UINT8 to_tile_x(INT16 px) { return (UINT8)((px - 8) >> 3); }

// subtract UI_ROWS because the room is drawn starting at row 1
UINT8 to_tile_y(INT16 py) {
    INT16 ty = ((py - 16) >> 3) - UI_ROWS;
    if (ty < 0) ty = 0;
    return (UINT8)ty;
}

UINT8 is_solid_at_pxpy(INT16 px, INT16 py) {
    UINT8 tx = to_tile_x(px);
    UINT8 ty = to_tile_y(py);
    return is_solid_tile(tile_at(tx, ty));
}

void draw_room_to_bkg(void) {
    set_bkg_tiles(0, UI_ROWS, MAP_W, MAP_H, room_map);
}

// THIS is your requested fixed layout:
// - tiles_door at top middle (in the "walls" row)
// - tiles_walls on that row
// - tiles_walls_2 row above it
// - floor fills below
// - another tiles_walls_2 row under the wall row
// - sides ignored for now (we’ll leave edges as floor except top area)
void build_room_fixed(void) {
    UINT8 x, y;
    UINT8 door_x = MAP_W / 2;

    // Fill with floor
    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            room_map[y * MAP_W + x] = TILE_FLOOR;
        }
    }

    // Row 0: WALL2 strip
    for (x = 0; x < MAP_W; x++) {
        room_map[0 * MAP_W + x] = TILE_WALL2;
    }

    // Row 1: WALL strip with door in middle
    for (x = 0; x < MAP_W; x++) {
        room_map[1 * MAP_W + x] = TILE_WALL;
    }
    room_map[1 * MAP_W + door_x] = TILE_DOOR;

    // Row 2: WALL2 strip under it
    for (x = 0; x < MAP_W; x++) {
        room_map[2 * MAP_W + x] = TILE_WALL2;
    }

    draw_room_to_bkg();
}


// =====================
// Gameplay helpers
// =====================

UINT8 overlaps8_i16(INT16 ax, INT16 ay, INT16 bx, INT16 by) {
    return (ax + 6 > bx) && (ax < bx + 6) && (ay + 6 > by) && (ay < by + 6);
}

INT8 sign_i16(INT16 v) {
    if (v < 0) return -1;
    if (v > 0) return 1;
    return 0;
}

void ui_draw(UINT8 hp, UINT8 room_id) {
    gotoxy(0, 0);
    printf("HP:%u  ROOM:%u  ", hp, room_id);
}

void ui_show_death(void) {
    gotoxy(0, 2); printf("                ");
    gotoxy(0, 3); printf("                ");
    gotoxy(0, 4); printf("                ");
    gotoxy(3, 2); printf("YOU DIED");
    gotoxy(1, 4); printf("PRESS START");
}

void clamp_player_room(INT16* px, INT16* py) {
    if (*px < 16) *px = 16;
    if (*px > 152) *px = 152;
    if (*py < 32) *py = 32;
    if (*py > 136) *py = 136;
}

int main(void) {
    // Player
    INT16 px = 80, py = 80;
    UINT8 last_dir = DIR_DOWN;

    UINT8 hp = 3;
    UINT8 inv_frames = 0;
    UINT8 player_kb_frames = 0;
    INT8 pkx = 0, pky = 0;

    // Ki
    UINT8 ki_active = 0;
    INT8 kdx = 0, kdy = 0;
    INT16 kx = 0, ky = 0;

    // Spirit
    UINT8 spirit_active = 1;
    INT16 sx = 40, sy = 60;

    UINT8 spirit_kb_frames = 0;
    INT8 skx = 0, sky = 0;
    UINT8 spirit_stun = 0;

    UINT8 spirit_step_timer = 0;
    UINT8 spirit_attack_cooldown = 0;

    // AoE Pulse
    UINT8 pulse_cooldown = 0;
    UINT8 pulse_active_frames = 0;

    UINT8 prev_keys = 0;
    UINT8 is_dead = 0;

    UINT8 room_id = 1;

    DISPLAY_ON;
    SHOW_BKG;
    SHOW_SPRITES;

    printf("");

    if (_cpu == CGB_TYPE) {
        set_bkg_palette(0, 1, bg_palette);
        set_sprite_palette(0, 1, spr_palette_normal);
    }

    // Load BG tile data from png2asset output
    set_bkg_data(tileset_TILE_ORIGIN, tileset_TILE_COUNT, (const unsigned char*)tileset_tiles);


    // Build the fixed layout room you described
    build_room_fixed();

    // Load sprite data
    set_sprite_data(0, 1, monk_sprite_tile);
    set_sprite_data(1, 1, ki_sprite_tile);
    set_sprite_data(2, 1, spirit_sprite_tile);

    set_sprite_tile(0, 0);
    set_sprite_tile(1, 1);
    set_sprite_tile(2, 2);

    ui_draw(hp, room_id);

    while (1) {
        UINT8 keys = joypad();
        UINT8 pressed = (keys ^ prev_keys) & keys;
        prev_keys = keys;

        if (is_dead) {
            if (pressed & J_START) {
                is_dead = 0;
                hp = 3;
                inv_frames = 30;
                player_kb_frames = 0;
                ki_active = 0;

                room_id = 1;
                rng_state ^= 0x1234;

                // Rebuild room on restart
                build_room_fixed();

                px = 80; py = 80;
                sx = 40; sy = 60;
                spirit_active = 1;

                spirit_stun = 30;
                spirit_attack_cooldown = 30;

                gotoxy(0,2); printf("                ");
                gotoxy(0,3); printf("                ");
                gotoxy(0,4); printf("                ");
                ui_draw(hp, room_id);
            }
            wait_vbl_done();
            continue;
        }

        if (inv_frames) inv_frames--;
        if (pulse_cooldown) pulse_cooldown--;
        if (pulse_active_frames) pulse_active_frames--;
        if (spirit_stun) spirit_stun--;
        if (spirit_step_timer) spirit_step_timer--;
        if (spirit_attack_cooldown) spirit_attack_cooldown--;

        // Player movement
        if (player_kb_frames) {
            player_kb_frames--;
            px += pkx;
            py += pky;

            if (is_solid_at_pxpy(px, py)) {
                px -= pkx;
                py -= pky;
                player_kb_frames = 0;
            }
        } else {
            INT8 dx = 0, dy = 0;

            if (keys & J_LEFT)       { dx = -1; dy = 0; last_dir = DIR_LEFT; }
            else if (keys & J_RIGHT) { dx =  1; dy = 0; last_dir = DIR_RIGHT; }
            else if (keys & J_UP)    { dy = -1; dx = 0; last_dir = DIR_UP; }
            else if (keys & J_DOWN)  { dy =  1; dx = 0; last_dir = DIR_DOWN; }

            UINT8 currently_overlapping = spirit_active && overlaps8_i16(px, py, sx, sy);

            if (dx != 0) {
                INT16 npx = px + dx;
                if (!is_solid_at_pxpy(npx, py) && (currently_overlapping || !(spirit_active && overlaps8_i16(npx, py, sx, sy)))) {
                    px = npx;
                }
            }
            if (dy != 0) {
                INT16 npy = py + dy;
                if (!is_solid_at_pxpy(px, npy) && (currently_overlapping || !(spirit_active && overlaps8_i16(px, npy, sx, sy)))) {
                    py = npy;
                }
            }
        }

        clamp_player_room(&px, &py);

        if (inv_frames && (inv_frames & 1)) move_sprite(0, 0, 0);
        else move_sprite(0, (UINT8)px, (UINT8)py);

        // Door transition: for now ONLY top door (row 1)
        {
            UINT8 tx = to_tile_x(px);
            UINT8 ty = to_tile_y(py);
            UINT8 t = tile_at(tx, ty);

            // Since your door is in the "back wall" row, this triggers when you stand on it
            if (t == TILE_DOOR && ty == 1) {
                room_id++;
                // For now we just rebuild the same layout; later you’ll swap room layouts
                build_room_fixed();

                // Spawn player just inside the room
                py = 40;
                ui_draw(hp, room_id);

                // reset enemy a bit
                sx = 40; sy = 80;
                spirit_active = 1;
                spirit_stun = 20;
                spirit_attack_cooldown = 30;

                ki_active = 0;
                move_sprite(1, 0, 0);
            }
        }

        // AoE pulse
        if ((pressed & J_B) && pulse_cooldown == 0) {
            pulse_cooldown = 90;
            pulse_active_frames = 10;
        }

        if (_cpu == CGB_TYPE) {
            if (pulse_active_frames) set_sprite_palette(0, 1, spr_palette_pulse);
            else set_sprite_palette(0, 1, spr_palette_normal);
        }

        if (spirit_active && pulse_active_frames) {
            INT16 ddx = sx - px;
            INT16 ddy = sy - py;
            if ((ddx*ddx + ddy*ddy) < 400) {
                skx = sign_i16(ddx) * 3;
                sky = sign_i16(ddy) * 3;
                spirit_kb_frames = 8;
                spirit_stun = 25;
            }
        }

        // Fire ki
        if (!ki_active && (pressed & J_A)) {
            ki_active = 1;
            kx = px; ky = py;

            if (last_dir == DIR_LEFT)  { kdx = -2; kdy = 0; }
            if (last_dir == DIR_RIGHT) { kdx =  2; kdy = 0; }
            if (last_dir == DIR_UP)    { kdx = 0;  kdy = -2; }
            if (last_dir == DIR_DOWN)  { kdx = 0;  kdy =  2; }
        }

        if (ki_active) {
            INT16 nkx = kx + kdx;
            INT16 nky = ky + kdy;

            if (is_solid_at_pxpy(nkx, nky) || nkx < 1 || nkx > 167 || nky < 1 || nky > 151) {
                ki_active = 0;
                move_sprite(1, 0, 0);
            } else {
                kx = nkx; ky = nky;
                move_sprite(1, (UINT8)kx, (UINT8)ky);
            }
        }

        // Spirit movement
        if (spirit_active) {
            if (spirit_kb_frames) {
                spirit_kb_frames--;
                INT16 nsx = sx + skx;
                INT16 nsy = sy + sky;
                if (!is_solid_at_pxpy(nsx, sy)) sx = nsx;
                if (!is_solid_at_pxpy(sx, nsy)) sy = nsy;
            } else if (!spirit_stun && spirit_step_timer == 0 && spirit_attack_cooldown == 0) {
                spirit_step_timer = 3;

                if (sx < px) { if (!is_solid_at_pxpy(sx + 1, sy)) sx++; }
                else if (sx > px) { if (!is_solid_at_pxpy(sx - 1, sy)) sx--; }

                if (sy < py) { if (!is_solid_at_pxpy(sx, sy + 1)) sy++; }
                else if (sy > py) { if (!is_solid_at_pxpy(sx, sy - 1)) sy--; }
            }

            move_sprite(2, (UINT8)sx, (UINT8)sy);
        }

        // Spirit hits player
        if (spirit_active && inv_frames == 0 && overlaps8_i16(px, py, sx, sy)) {
            if (hp > 0) hp--;
            ui_draw(hp, room_id);

            if (hp == 0) {
                is_dead = 1;
                move_sprite(0, 0, 0);
                move_sprite(1, 0, 0);
                move_sprite(2, 0, 0);
                ui_show_death();
            } else {
                inv_frames = 75;

                pkx = sign_i16(px - sx) * 3;
                pky = sign_i16(py - sy) * 3;
                player_kb_frames = 14;

                spirit_attack_cooldown = 30;
                spirit_stun = 10;
            }
        }

        // Ki seals spirit
        if (spirit_active && ki_active && overlaps8_i16(kx, ky, sx, sy)) {
            spirit_active = 0;
            ki_active = 0;
            move_sprite(2, 0, 0);
            move_sprite(1, 0, 0);
        }

        wait_vbl_done();
    }
}
