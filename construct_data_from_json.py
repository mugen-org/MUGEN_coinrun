# Copyright (c) Meta Platforms, Inc. All Right reserved.

"""
To load json metadata of a given level and construct the semantic map of a given frame.
Usage:
    python construct_data_from_json.py \
        --restore_id paper_500 \
        --frame_id 11 \
        --level_id 1

frame_id and restore_id are required input (level_id is default to 0).
If frame_id < 0, will draw all frames.

To render as a video instead of pngs (frame_id should be < 0):
    python construct_data_from_json.py \
        --restore_id paper_500
        --input_json video_data/video_metadata/simple_coinrun_video_0_0.json \
        --save_as_video \
        --frame_id -1 \
        --level_id 1

To generate original color frame instead of semantic maps, add argument:
    --gen_original
"""

import argparse
import imageio
import math
import numpy as np
import os
from tqdm import tqdm
from PIL import Image

from convert_csv_to_json import Game

ASSET_ROOT = 'coinrun/assets'

# TODO: save these from the game engine? would they ever change?
DEATH_ANIM_LENGTH = 30
FINISHED_LEVEL_ANIM_LENGTH = 20
MONSTER_DEATH_ANIM_LENGTH = 3

# constant symbols for the maze used in game engine
SPACE = '.'
LADDER = '='
LAVA_SURFACE = '^'
LAVA_MIDDLE = '|'
WALL_SURFACE = 'S'
WALL_MIDDLE = 'A'
WALL_CLIFF_LEFT = 'a'
WALL_CLIFF_RIGHT = 'b'
COIN_OBJ1 = '1'
COIN_OBJ2 = '2'
SPIKE_OBJ = 'P'
CRATE_NORMAL = '#'
CRATE_DOUBLE = '$'
CRATE_SINGLE = '&'
CRATE_WARNING = '%'


# color names from: https://www.rapidtables.com/web/color/RGB_Color.html#color-table
def define_semantic_color_map(single_channel_label=False, readable_label=False):
    semantic_color_map = {}

    if not single_channel_label:
        # NOTE: this color is not actually used when generating semantic maps (hard-coded as 0)
        semantic_color_map['background'] = (0, 0, 0) # black

        semantic_color_map['world'] = {
            WALL_MIDDLE: (139, 69, 19),    # saddle brown
            WALL_SURFACE: (0, 255, 0),     # lime
            WALL_CLIFF_LEFT: (0, 128, 0),     # green
            WALL_CLIFF_RIGHT: (0, 100, 0),    # dark green
            COIN_OBJ1: (255, 255, 0),      # yellow
            COIN_OBJ2: (255, 69, 0),       # orange red
            CRATE_NORMAL: (205, 133, 63),  # peru
            CRATE_DOUBLE: (205, 133, 63),  # peru
            CRATE_SINGLE: (205, 133, 63),  # peru
            CRATE_WARNING: (205, 133, 63), # peru
            LAVA_MIDDLE: (255, 0, 0),      # red
            LAVA_SURFACE: (255, 0, 0),     # red
            SPIKE_OBJ: (176, 196, 222),    # light steel blue
            LADDER: (244, 164, 96),        # sandy brown
        }

        semantic_color_map['alien'] = (0, 0, 255)    # blue
        semantic_color_map['shield'] = (0, 0, 128)    # navy

        semantic_color_map['monster'] = {
            'sawHalf': (169, 169, 169),     # dark gray
            'bee': (255, 215, 0),           # gold
            'slimeBlock': (255, 192, 203),  # pink
            'slimePurple': (255, 192, 203), # pink
            'slimeBlue': (255, 192, 203),   # pink
            'slimeGreen': (255, 192, 203),  # pink
            'mouse': (230, 230, 250),       # lavender
            'snail': (255, 0, 255),         # magenta
            'ladybug': (210, 105, 30),      # chocolate
            'wormGreen': (154, 205, 50),    # yellow green
            'wormPink': (154, 205, 50),     # yellow green
            'barnacle': (85, 85, 85),       # light gray
            'frog': (190, 190, 50), 
        }
    else:
        # will genereate single channel semantic maps
        semantic_color_map['background'] = 0

        if readable_label:
            semantic_color_map['world'] = {
                WALL_MIDDLE: 40,
                WALL_SURFACE: 50,
                WALL_CLIFF_LEFT: 55,
                WALL_CLIFF_RIGHT: 60,
                COIN_OBJ1: 220,
                COIN_OBJ2: 230,
                CRATE_NORMAL: 100,
                CRATE_DOUBLE: 100,
                CRATE_SINGLE: 100,
                CRATE_WARNING: 100,
                LAVA_MIDDLE: 10,
                LAVA_SURFACE: 20,
                SPIKE_OBJ: 30,       # NOTE: not enabled in current game engine
                LADDER: 70,
            }

            semantic_color_map['alien'] = 255
            semantic_color_map['shield'] = 250

            semantic_color_map['monster'] = {
                'sawHalf': 200,
                'bee': 190,
                'slimeBlock': 180,
                'slimePurple': 170,   # NOTE: not enabled in current game engine
                'slimeBlue': 170,
                'slimeGreen': 170,    # NOTE: not enabled in current game engine
                'mouse': 160,
                'snail': 150,
                'ladybug': 140,
                'wormGreen': 130,     # NOTE: not enabled in current game engine
                'wormPink': 130,
                'barnacle': 120,
                'frog': 110,
            }
        else:
            semantic_color_map['world'] = {
                WALL_MIDDLE: 3,
                WALL_SURFACE: 4,
                WALL_CLIFF_LEFT: 5,
                WALL_CLIFF_RIGHT: 6,
                COIN_OBJ1: 19,
                COIN_OBJ2: 20,
                CRATE_NORMAL: 8,
                CRATE_DOUBLE: 8,
                CRATE_SINGLE: 8,
                CRATE_WARNING: 8,
                LAVA_MIDDLE: 1,
                LAVA_SURFACE: 2,
                # SPIKE_OBJ: -1,       # NOTE: not enabled in current game engine
                LADDER: 7,
            }

            semantic_color_map['alien'] = 22
            semantic_color_map['shield'] = 21

            semantic_color_map['monster'] = {
                'sawHalf': 16,
                'bee': 15,
                'slimeBlock': 14,
                'slimePurple': 13,   # NOTE: not enabled in current game engine
                'slimeBlue': 13,
                'slimeGreen': 13,    # NOTE: not enabled in current game engine
                'mouse': 12,
                'snail': 11,
                'ladybug': 10,
                'wormGreen': 9,     # NOTE: not enabled in current game engine
                'wormPink': 9,
                'barnacle': 17,
                'frog': 18,
            }
    return semantic_color_map


def generate_asset_paths(game):
    # use background corresponding with ground theme
    bgtheme = game.background_themes[game.world_theme_n]

    gtheme = game.ground_themes[game.world_theme_n]
    walls = 'kenney/Ground/' + gtheme + '/' + gtheme.lower()

    atheme = game.agent_themes[game.agent_theme_n]
    alien = 'kenneyLarge/Players/128x256_no_helmet/' + atheme + "/alien" + atheme

    tiles = 'kenney/Tiles/'
    items = 'kenneyLarge/Items/'
    enemy = 'kenneyLarge/Enemies/'

    asset_files = {}

    asset_files['background'] = bgtheme

    asset_files['world'] = {
        WALL_MIDDLE: walls + 'Center.png',
        WALL_SURFACE: walls + 'Mid.png',
        WALL_CLIFF_LEFT: walls + 'Cliff_left.png',
        WALL_CLIFF_RIGHT: walls + 'Cliff_right.png',
        COIN_OBJ1: items + 'coinGold.png',
        COIN_OBJ2: items + 'gemRed.png',
        CRATE_NORMAL: tiles + "boxCrate.png",
        CRATE_DOUBLE: tiles + "boxCrate_double.png",
        CRATE_SINGLE: tiles + "boxCrate_single.png",
        CRATE_WARNING: tiles + "boxCrate_warning.png",
        LAVA_MIDDLE: tiles + "lava.png",
        LAVA_SURFACE: tiles + "lavaTop_low.png",
        SPIKE_OBJ: tiles + "spikes.png",
        LADDER: tiles + "ladderMid.png",
    }

    asset_files['alien'] = {
        'walk1': alien + '_walk1.png',
        'walk2': alien + '_walk2.png',
        'climb1': alien + '_climb1.png',
        'climb2': alien + '_climb2.png',
        'stand': alien + '_stand.png',
        'jump': alien + '_jump.png',
        'duck': alien + '_duck.png',
        'hit': alien + '_hit.png',
    }
    asset_files['shield'] = 'bubble_shield.png'

    game.flatten_monster_names()
    # monster assets are generated based on list of names used at rendering
    asset_files['monster'] = {
        name: enemy + name + '.png'
        for name in game.flattened_monster_names
    }

    return asset_files


# binarize alpha channel if input img is in RGBA mode, set anything above 0 to 255
def binarize_alpha_channel(img):
    if img.mode != 'RGBA':
        return img

    w, h = img.size
    for i in range(w):
        for j in range(h):
            pixel = img.getpixel((i, j))

            # set alpha to 255 if alpha > 0
            if pixel[3] > 0:
                img.putpixel((i, j), (pixel[0], pixel[1], pixel[2], 255))

    return img


class Asset:
    def __init__(
        self, name, file, kind='world', kx=80, ky=80,
        semantic_color=(0, 0, 0), flip=False, binarize_alpha=False
    ):
        self.name = name
        self.file = file
        self.kind = kind
        self.kx = kx
        self.ky = ky
        self.semantic_color = semantic_color
        self.flip = flip
        self.binarize_alpha = binarize_alpha
        
        self.load_asset()
    
    def load_asset(self):
        asset_path = os.path.join(ASSET_ROOT, self.file)
        assert os.path.isfile(asset_path), asset_path
        self.asset = Image.open(asset_path)
        
        # used for (user control) asset swap, because alien h:w == 2:1 while others is 1:1
        # the asset resize at loading and render grid size all need to change respectively
        self.aspect_ratio = self.asset.size[1] / self.asset.size[0]

        # TODO: check if ceil is needed
        if self.kind == 'world':
            if self.name != LAVA_MIDDLE and self.name != LAVA_SURFACE:
                # LAVA has a special way of rendering animation so don't resize now
                self.asset = self.asset.resize((math.ceil(self.kx + .5), math.ceil(self.ky + .5)))
        elif self.kind == 'alien':
            self.asset = self.asset.resize((math.ceil(self.kx), math.ceil(self.aspect_ratio * self.ky)))
        elif self.kind == 'shield':
            # TODO: magic number hard-coded for shield asset rendering size, this won't work for user-input character
            # we need either a way to draw shield on-the-fly based on character,
            # or automatic resizing of shield based on aspect ratio
            self.asset = self.asset.resize((math.ceil(self.kx * 1.15), math.ceil(self.ky * 2.1)))
        elif self.kind == 'monster' or self.kind == 'background':
            self.asset = self.asset.resize((math.ceil(self.kx), math.ceil(self.ky)))
        else:
            raise NotImplementedError(f"Unknown asset kind {self.kind}")
        
        # flip if needed (for facing left/right)
        if self.flip:
            self.asset = self.asset.transpose(Image.FLIP_LEFT_RIGHT)

        # NOTE: this must happen in the end, or resize will result in new intepolated alpha value!!
        if self.binarize_alpha:
            self.asset = binarize_alpha_channel(self.asset)


def load_assets(asset_files, semantic_color_map, kx=80, ky=80, gen_original=False):
    asset_map = {}

    for kind in asset_files.keys():
        assert kind in semantic_color_map

        if kind == 'background':
            # background will be loaded separately
            continue

        if kind == 'shield':
            # asset file for the bubble shield in agent power-up mode
            asset_map[kind] = Asset(
                name=kind, file=asset_files[kind], kind=kind,
                kx=kx, ky=ky, semantic_color=semantic_color_map[kind],
                binarize_alpha=not gen_original,
            )
            continue

        # NOTE: if not generating original, binarize alpha channel of all assets
        # this is a bit slow (~1s) but only run once at init
        # an alternative is to pre-binarize another copy of assets for semantic maps
        for key in asset_files[kind].keys():
            if kind == 'world':
                # ground asset, no need to worry about pose or facing
                asset_map[key] = Asset(
                    name=key, file=asset_files[kind][key], kind=kind,
                    kx=kx, ky=ky, semantic_color=semantic_color_map[kind][key],
                    binarize_alpha=not gen_original,
                )
            elif kind == 'alien':
                # facing right is default to empty
                # NOTE: this is the opposite of monster (default facing left)
                all_facings = ['', '_left']
                for facing in all_facings:
                    a_key = key + facing

                    asset_map[a_key] = Asset(
                        name=a_key, file=asset_files[kind][key], kind=kind,
                        kx=kx, ky=ky, semantic_color=semantic_color_map[kind],
                        flip=(facing != ''),    # flip the asset if facing is not ''
                        binarize_alpha=not gen_original,
                    )
            elif kind == 'monster':
                # for monsters, 3 types of assets will be loaded
                # for each of them, facing can be left or right
                all_poses = ['', '_move', '_dead']       # walk1 is default to empty
                all_facings = ['', '_right']             # facing left is default to empty
                base_fn = os.path.splitext(asset_files[kind][key])[0]   # e.g. Enemies/bee
                for pose in all_poses:
                    for facing in all_facings:
                        m_key = key + pose + facing
                        file_name = base_fn + pose + '.png'

                        asset_map[m_key] = Asset(
                            name=m_key, file=file_name, kind='monster',
                            kx=kx, ky=ky, semantic_color=semantic_color_map[kind][key],
                            flip=(facing != ''),    # flip the asset if facing is not ''
                            binarize_alpha=not gen_original,
                        )
            else:
                raise NotImplementedError(f"Unknown asset kind {kind}")

    return asset_map


# load background asset, zoom is different so need a separate function
def load_bg_asset(asset_files, semantic_color_map, zx, zy):
    kind = 'background'
    bg_asset = Asset(
        name=kind, file=asset_files[kind], kind=kind,
        kx=zx, ky=zy, semantic_color=semantic_color_map[kind]
    )
    return bg_asset


# used for alien dying animation in gen_original mode
def get_transparent_asset(input_asset, transparency):
    assert input_asset.mode == 'RGBA'
    np_asset = np.array(input_asset, dtype=np.int16)
    np_asset[:, :, 3] -= transparency
    np_asset[:, :, 3] = np.clip(np_asset[:, :, 3], 0, None)
    return Image.fromarray(np_asset.astype(np.uint8))


# return rect in integer values, floor for x1,y1, ceil for x2,y2 or w,h
def integer_rect(rect):
    return [math.floor(rect[0]), math.floor(rect[1]), math.ceil(rect[2]), math.ceil(rect[3])]


def convert_xywh_to_xyxy(rect):
    return [rect[0], rect[1], rect[0] + rect[2], rect[1] + rect[3]]


def convert_xyxy_to_xywh(rect):
    return [rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]]


# rect format is xywh, img_size is (w,h)
def check_out_of_bounds(rect, img_size):
    if rect[0] + rect[2] < 0:
        return True
    if rect[0] > img_size[0]:
        return True
    if rect[1] + rect[3] < 0:
        return True
    if rect[1] > img_size[1]:
        return True
    return False


# return intersect of two rects, input and output are both in xywh format
def intersect_rects(rect1, rect2):
    xyxy_rect1 = convert_xywh_to_xyxy(rect1)
    xyxy_rect2 = convert_xywh_to_xyxy(rect2)
    xyxy_res_rect = [
        max(xyxy_rect1[0], xyxy_rect2[0]),
        max(xyxy_rect1[1], xyxy_rect2[1]),
        min(xyxy_rect1[2], xyxy_rect2[2]),
        min(xyxy_rect1[3], xyxy_rect2[3])
    ]

    xywh_res_rect = convert_xyxy_to_xywh(xyxy_res_rect)

    # check if the intersection is empty
    if xywh_res_rect[2] > 0 and xywh_res_rect[3] > 0:
        return xywh_res_rect
    else:
        return None


# rect is in the format of xywh
def paint_color_in_rect_with_mask(img, rect, color, mask, gen_original=False):
    w, h = mask.size
    img_w, img_h = img.size
    # in some cases, mask size doesn't match the rect (e.g. monster dying)
    if rect[2] != w or rect[3] != h:
        if not gen_original:
            mask = mask.resize((rect[2], rect[3]), resample=Image.NEAREST)
        else:
            mask = mask.resize((rect[2], rect[3]))
        w, h = mask.size

    if not gen_original:
        # paste in single color if generating semantic maps (so not original)
        img = img.paste(
            color, convert_xywh_to_xyxy(rect), mask if mask.mode == 'RGBA' else None
        )
    else:
        img = img.paste(
            mask, convert_xywh_to_xyxy(rect), mask if mask.mode == 'RGBA' else None
        )

    return


def draw_game_frame(
    game, frame_id, asset_map, kx, ky,
    gen_original=False, single_channel_label=False
):
    # initialize an empty image (all zero, for background)
    if (not gen_original) and single_channel_label:
        img = Image.new('L', (game.video_res, game.video_res))
    else:
        img = Image.new('RGB', (game.video_res, game.video_res))

    video_center = (game.video_res - 1) // 2

    frame = game.frames[frame_id]

    # for agent-centric
    # dx = -frame.agent.x * kx + video_center - 0.5 * kx
    # dy = frame.agent.y * ky - video_center - 0.5 * ky
    # for video data (no vertical camera move)
    dx = -frame.agent.x * kx + video_center  - 0.5 * kx
    dy = -video_center + 5.0 * ky

    # update background image with proper zoom for gen_original mode
    # NOTE: if desired background label is not zero, set it here to asset_map['background'].semantic_color
    if gen_original:
        zx = game.video_res * game.zoom
        zy = zx
        for tile_x in range(-1, 3):
            for tile_y in range(-1, 2):
                bg_rect = [0, 0, zx, zy]
                bg_rect[0] = zx * tile_x + video_center + game.bgzoom * (dx + kx * game.maze_h / 2) - zx * 0.5
                bg_rect[1] = zy * tile_y + video_center + game.bgzoom * (dy - ky * game.maze_h / 2) - zy * 0.5
                if check_out_of_bounds(bg_rect, img.size):
                    continue
                img.paste(asset_map['background'].asset, convert_xywh_to_xyxy(integer_rect(bg_rect)))

    # NOTE: game engine now hard-code 64 for maze_size
    radius = int(1 + game.maze_w / game.zoom)
    ix = int(frame.agent.x + .5)
    iy = int(frame.agent.y + .5)
    x_start = max(ix - radius, 0)
    x_end = min(ix + radius + 1, game.maze_w)
    y_start = max(iy - radius, 0)
    y_end = min(iy + radius + 1, game.maze_h)
    win_h = game.video_res

    # convert eaten coins to a set for faster checking coordinates
    coins_eaten_set = set([tuple(coin_coord) for coin_coord in frame.coins_eaten])

    ## paint the world with background, ground, crates, coins, etc.
    for y in range(y_start, y_end):
        for x in range(x_start, x_end):
            wkey = game.maze[y][x]
            if wkey == SPACE:
                continue

            # eaten coins is treated the same as SPACE, just continue
            # but we should not modify the coins in maze to SPACE, or it may cause inconsistency
            # if we ever need to render backwards or save json after drawing
            if (x, y) in coins_eaten_set:
                continue

            assert wkey in asset_map, f'{wkey} not in assets!'

            tile_rect = [
                kx * x + dx - 0.1,
                win_h - ky * y + dy - 0.1,
                kx + .5 + 0.2,
                ky + .5 + 0.2]

            # skip tile if the rect is completely out-of-bounds
            if check_out_of_bounds(tile_rect, img.size):
                continue

            # NOTE: this is quite complex and might be slow
            # in practice we may want to skip this and show some approximate mask?
            if wkey==LAVA_MIDDLE or wkey==LAVA_SURFACE:
                d1 = tile_rect[:]
                d2 = tile_rect[:]
                asset_size = asset_map[wkey].asset.size
                sr = [0, 0, asset_size[0], asset_size[1]]
                sr1 = sr[:]
                sr2 = sr[:]
                tr = frame.state_time * 0.1
                tr -= int(tr)
                tr *= -1
                d1[0] += tr * tile_rect[2]
                d2[0] += tile_rect[2] + tr * tile_rect[2]
                sr1[0] += -tr * asset_size[0]
                sr2[0] += -asset_size[0] - tr * asset_size[0]
                d1 = intersect_rects(d1, tile_rect)
                d2 = intersect_rects(d2, tile_rect)
                if d1 is not None:
                    d1[2] += 0.5
                if d2 is not None:
                    d2[0] -= 0.5
                    d2[2] += 0.5
                sr1 = intersect_rects(sr1, sr)
                sr2 = intersect_rects(sr2, sr)
                if sr1 is not None and d1 is not None:
                    # crop and render one half of the asset
                    # NOTE: not sure if this should be convert_xywh_to_xyxy(integer_rect(sr1))
                    #       to be validated with more lava frames
                    crop_mask = asset_map[wkey].asset.crop(integer_rect(convert_xywh_to_xyxy(sr1)))
                    paint_color_in_rect_with_mask(
                        img, integer_rect(d1), asset_map[wkey].semantic_color,
                        crop_mask, gen_original=gen_original
                    )
                if sr2 is not None and d2 is not None:
                    # crop and render the other half of the asset (swapped places horizontally)
                    crop_mask = asset_map[wkey].asset.crop(integer_rect(convert_xywh_to_xyxy(sr2)))
                    paint_color_in_rect_with_mask(
                        img, integer_rect(d2), asset_map[wkey].semantic_color,
                        crop_mask, gen_original=gen_original
                    )
            else:
                paint_color_in_rect_with_mask(
                    img, integer_rect(tile_rect), asset_map[wkey].semantic_color,
                    asset_map[wkey].asset, gen_original=gen_original
                )

    ## paint monsters
    for mi in range(len(frame.monsters)):
        if frame.monsters[mi].is_dead:
            dying_frame_cnt = max(0, frame.monsters[mi].monster_dying_frame_cnt)
            monster_shrinkage = (MONSTER_DEATH_ANIM_LENGTH - dying_frame_cnt) * 0.8 / MONSTER_DEATH_ANIM_LENGTH
            monster_rect = [
                math.floor(kx * frame.monsters[mi].x + dx),
                math.floor(win_h - ky * frame.monsters[mi].y + dy + ky * monster_shrinkage),
                math.ceil(kx),
                math.ceil(ky * (1 - monster_shrinkage))
            ]
        else:
            monster_rect = [
                math.floor(kx * frame.monsters[mi].x + dx),
                math.floor(win_h - ky * frame.monsters[mi].y + dy),
                math.ceil(kx),
                math.ceil(ky)
            ]

        m_name = game.flattened_monster_names[frame.monsters[mi].theme]
        # add pose and facing to the key to find correct asset
        # TODO: validate if walk1/walk2 might be mixed up
        m_pose = '' if frame.monsters[mi].walk1_mode else '_move'
        if frame.monsters[mi].is_dead:
            m_pose = '_dead'
        m_key = m_name + m_pose + ('_right' if frame.monsters[mi].vx > 0 else '')

        paint_color_in_rect_with_mask(
            img, monster_rect, asset_map[m_key].semantic_color,
            asset_map[m_key].asset, gen_original=gen_original
        )

    ## paint agent - do it after monsters so agent is always in front
    a_key = frame.agent.pose + ('' if frame.agent.is_facing_right else '_left')
    # note how aspect_ratio is used for alien rect, this can be applied to
    # monster rect to support asset that's not 1:1 (e.g. use alien as monster)
    # NOTE: current implementation always use the same rendering width and just height based on aspect ratio
    alien_rect = [
        math.floor(kx * frame.agent.x + dx),
        # math.floor(win_h - ky * (frame.agent.y + 1) + dy),    # default for 2:1 alien, no asset swap
        math.floor(win_h - ky * (frame.agent.y + asset_map[a_key].aspect_ratio - 1) + dy),
        math.ceil(kx),
        # math.ceil(2 * ky),    # default for 2:1 alien, no asset swap
        math.ceil(asset_map[a_key].aspect_ratio * ky),
    ]
    if frame.agent.is_killed:
        transparency = (DEATH_ANIM_LENGTH + 1 - frame.agent.killed_animation_frame_cnt)*12
        # only render if not fully transparent
        if transparency > 255:
            agent_asset = None
        else:
            if gen_original:
                # NOTE: now only changing alpha but not saturation like in the game engine
                # (HSV doesn't work for some reason), but the effect is pretty similar
                agent_asset = get_transparent_asset(asset_map[a_key].asset, transparency)
            else:
                # when generating semantic map, alien mask won't change unless fully transparent
                agent_asset = asset_map[a_key].asset
    else:
        agent_asset = asset_map[a_key].asset
    if agent_asset is not None:
        paint_color_in_rect_with_mask(
            img, alien_rect, asset_map[a_key].semantic_color,
            agent_asset, gen_original=gen_original
        )

    ## paint the bubble shield if agent is in power-up mode
    if frame.agent.power_up_mode:
        # TODO: make this work for user-input object with different aspect ratio
        shield_rect =  [
            # NOTE: game engine hard-codes 7 and 8 for co-ordinates which won't work with video-res that's not 1024
            # (for training we usually generate with 256 or 128 video_res), so need to convert them
            math.floor(kx * frame.agent.x + dx - 7 * game.video_res / 1024),
            math.floor(win_h - ky * (frame.agent.y + 1) + dy + 8 * game.video_res / 1024),
            math.ceil(kx * 1.15),
            math.ceil(ky * 2.1),
        ]
        # pull bubble down when Mugen crouches
        if frame.agent.pose == 'duck':
            shield_rect[1] += math.floor(8 * game.video_res / 1024)

        paint_color_in_rect_with_mask(
            img, shield_rect, asset_map['shield'].semantic_color,
            asset_map['shield'].asset, gen_original=gen_original
        )

    return img


def draw_and_save_single_frame(
    game, frame_id, level_id, asset_map, output_folder, kx, ky,
    gen_original=False, single_channel_label=False
):
    img = draw_game_frame(
        game, frame_id, asset_map, kx, ky,
        gen_original=gen_original, single_channel_label=single_channel_label
    )
    output_path = os.path.join(
        output_folder,
        "level_{:04d}_frame_{:04d}.png".format(level_id, frame_id)
    )
    img.save(output_path)
    return 


def parse_args():
    parser = argparse.ArgumentParser(
        description="Load json metadata and construct semantic maps"
    )
    parser.add_argument(
        "--frame_id", type=int, default=-1,
        help="frame id from the level to construct, -1 to generate all frames"
    )
    parser.add_argument(
        "--gen_original", action="store_true", default=False,
        help="generate original color video rather than semantic maps",
    )
    parser.add_argument(
        "--input_json", type=str, default=None,
        help="input json file, if None, will generate from level_id and restore_id"
    )
    parser.add_argument(
        "--json_folder", type=str, default="json_metadata",
        help="input folder of json files"
    )
    parser.add_argument(
        "--level_id", type=int, default=0,
        help="level id to load json metadata"
    )
    parser.add_argument(
        "--output_folder", type=str,
        default="constructed_data",
        help="output folder to save the constructed semantic maps"
    )
    parser.add_argument(
        "--restore_id", type=str, required=True,
        help="Restore id to obtain the input json path and save constructed result"
    )
    parser.add_argument(
        "--save_as_video", action="store_true", default=False,
        help="save result as video instead of pngs, only effective when frame_id < 0",
    )
    parser.add_argument(
        "--single_channel_label", action="store_true", default=False,
        help="use single channel label for semantic maps, instead of three channels",
    )
    parser.add_argument(
        "--readable_label", action="store_true", default=False,
        help="use label in the range of 0-255 so they're more readable",
    )
    args = parser.parse_args()

    # if not providing a specific json, generate filename from folder and level id
    if args.input_json is None:
        args.input_json = os.path.join(
            "video_data", args.restore_id, args.json_folder, "level_{:04d}.json".format(args.level_id)
        )

    args.output_folder = os.path.join("video_data", args.restore_id, args.output_folder)

    if not os.path.isdir(args.output_folder):
        print(f"{args.output_folder} doesn't exist, creating")
        os.makedirs(args.output_folder)
    
    return args


if __name__ == "__main__":
    args = parse_args()

    game = Game()
    game.load_json(args.input_json)

    semantic_color_map = define_semantic_color_map(
        single_channel_label=args.single_channel_label, readable_label=args.readable_label
    )
    asset_files = generate_asset_paths(game)

    # load assets according to game world grid size
    # NOTE: game engine now hard-code both kx and ky with 64 for maze_size
    kx: float = game.zoom * game.video_res / game.maze_w 
    ky: float = kx
    asset_map = load_assets(asset_files, semantic_color_map, kx, ky, args.gen_original)

    # background asset is loaded separately due to not following the grid
    zx = game.video_res * game.zoom
    zy = zx
    asset_map['background'] = load_bg_asset(asset_files, semantic_color_map, zx, zy)

    if args.frame_id >= 0:
        # draw a specific frame
        assert args.frame_id < len(game.frames)
        draw_and_save_single_frame(
            game, args.frame_id, args.level_id, asset_map, args.output_folder, kx, ky,
            gen_original=args.gen_original, single_channel_label=args.single_channel_label
        )
    else:
        if not args.save_as_video:
            # draw all frames in this level
            for fi in tqdm(range(len(game.frames))):
                draw_and_save_single_frame(
                    game, fi, args.level_id, asset_map, args.output_folder, kx, ky,
                    gen_original=args.gen_original, single_channel_label=args.single_channel_label
                )
        else:
            # save as video instead of pngs, use same filename as json
            video_fn = os.path.splitext(os.path.basename(args.input_json))[0] + '_constructed.mp4'
            video_path = os.path.join(args.output_folder, video_fn)
            writer = imageio.get_writer(video_path, fps=30)
            for fi in tqdm(range(len(game.frames))):
                frame = draw_game_frame(
                    game, fi, asset_map, kx, ky,
                    gen_original=args.gen_original, single_channel_label=args.single_channel_label
                )
                writer.append_data(np.array(frame))
            writer.close()
