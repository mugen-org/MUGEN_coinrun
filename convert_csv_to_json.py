# Copyright (c) Meta Platforms, Inc. All Right reserved.

"""
To convert the monitor.csv file cached during CoinRun game rendering to json metadata.
Usage:
    python convert_csv_to_json.py --restore_id paper_500
"""

import argparse
import json
import numpy as np
import os


def parse_args():
    parser = argparse.ArgumentParser(
        description="Convert monitor.csv file to json files"
    )
    parser.add_argument(
        "--data_root", type=str, default="video_data",
        help="Root folder for both input and output sub-folders"
    )
    parser.add_argument(
        "--input", type=str, default=None,
        help="Specify a path to monitor.csv file if it's not the default path with restore_id"
    )
    parser.add_argument(
        "--output_folder", type=str, default="json_metadata",
        help="Output folder for generated json files"
    )
    parser.add_argument(
        "--restore_id", type=str, required=True,
        help="Restore id to obtain the default path to csv file and save result json file"
    )
    args = parser.parse_args()

    # if no input csv location is provided, generate it in default format
    if args.input is None:
        args.input = os.path.join(args.data_root, args.restore_id, "csv", "000.monitor.csv")

    args.output_folder = os.path.join(args.data_root, args.restore_id, args.output_folder)

    if not os.path.isdir(args.output_folder):
        print(f"{args.output_folder} doesn't exist, creating")
        os.makedirs(args.output_folder)

    return args


class Game:
    def __init__(self, **kwargs):
        self.game_id = -1
        self.level_seed = 0
        self.rl_agent_seed = 0
        self.zoom = 5.5
        self.bgzoom = 0.4    # NOTE: hard-coded
        self.world_theme_n = -1
        self.agent_theme_n = -1

        self.background_themes = []
        self.ground_themes = []
        self.agent_themes = []
        self.monster_names = {}
        self.flattened_monster_names = []
       
        # TODO: save and load these from the game engine
        self.video_res = 1024
        self.maze_w = 64
        self.maze_h = 13

        self.reset_game()

        self.__dict__.update(**kwargs)
        self.frames = [Frame(**f) for f in self.frames]
        # self.frames = {f[0]: Frame(**f[1]) for f in self.frames.items()}
 
    def reset_game(self):
        self.maze = None
        self.frames = []
    
    def asdict(self, f_start=-1, f_end=-1):
        if f_end < 0:
            # show all frames by default
            frames_as_dict = [f.asdict() for f in self.frames]
        else:
            frames_as_dict = [f.asdict() for f in self.frames[f_start:f_end]]
        return {
            "game_id": self.game_id,
            "level_seed": self.level_seed,
            "rl_agent_seed": self.rl_agent_seed,
            "zoom": self.zoom,
            "bgzoom": self.bgzoom,
            "world_theme_n": self.world_theme_n,
            "agent_theme_n": self.agent_theme_n,
            "background_themes": self.background_themes,
            "ground_themes": self.ground_themes,
            "agent_themes": self.agent_themes,
            "monster_names": self.monster_names,
            "video_res": self.video_res,
            "maze_w": self.maze_w,
            "maze_h": self.maze_h,
            "maze": self.maze if self.maze is not None else None,
            "frames": frames_as_dict,
        }
    
    def __repr__(self):
        return json.dumps(self.asdict())
    
    def save_json(self, json_path, f_start=-1, f_end=-1):
        with open(json_path, "w") as f:
            json.dump(self.asdict(f_start, f_end), f, indent=2)
    
    def load_json(self, json_path):
        with open(json_path, "r") as f:
            data = json.load(f)
        
        self.reset_game()
        self.__dict__.update(**data)
        self.frames = [Frame(**f) for f in self.frames]
        # self.frames = {f[0]: Frame(**f[1]) for f in self.frames.items()}

        self.flatten_monster_names()
    
    def flatten_monster_names(self):
        # the order is important!
        self.flattened_monster_names = self.monster_names['ground']
        self.flattened_monster_names.extend(self.monster_names['walking'])
        self.flattened_monster_names.extend(self.monster_names['flying'])


class Frame:
    def __init__(self, **kwargs):
        self.frame_id = -1
        self.file_name = ""
        self.state_time = 0
        self.coins_eaten = []
        self.agent = None
        self.monsters = []

        self.__dict__.update(**kwargs)
        if 'agent' in self.__dict__ and self.agent is not None:
            self.agent = Agent(**self.agent)
        if 'monsters' in self.__dict__:
            self.monsters = [Monster(**m) for m in self.monsters]
        
    def asdict(self):
        return {
            "frame_id": self.frame_id,
            "file_name": self.file_name,
            "state_time": self.state_time,
            "coins_eaten": self.coins_eaten,
            "agent": self.agent.asdict() if self.agent is not None else None,
            "monsters": [m.asdict() for m in self.monsters]
        }
    
    def __repr__(self):
        return json.dumps(self.asdict())

        
class Agent:
    def __init__(
        self, x, y, vx=0.0, vy=0.0, time_alive=0,
        ladder=False, spring=0, is_killed=False,
        killed_animation_frame_cnt=0,
        finished_level_frame_cnt=0,
        killed_monster=False,
        bumped_head=False,
        collected_coin=False,
        collected_gem=False,
        power_up_mode=False,
        **kwargs
    ):
        self.x = x
        self.y = y
        self.vx = vx
        self.vy = vy
        self.time_alive = time_alive
        self.ladder = ladder  # for climb pose
        self.spring = spring  # for duck pose
        
        # states related to agent dying or finishing animations
        self.is_killed = is_killed
        self.killed_animation_frame_cnt = killed_animation_frame_cnt
        self.finished_level_frame_cnt = finished_level_frame_cnt
        self.killed_monster = killed_monster
        self.bumped_head = bumped_head
        self.collected_coin = collected_coin
        self.collected_gem = collected_gem
        self.power_up_mode = power_up_mode
        
        self.anim_freq = 5    # hard-coded
        
        # decide whether to flip asset horizontally
        self.is_facing_right = True
        if self.vx < 0:
            self.is_facing_right = False
        
        # decide which of the two walk/climb asset to use
        self.walk1_mode = True
        if (self.time_alive // self.anim_freq) % 2 != 0:
            self.walk1_mode = False
            
        self.pose = self.get_pose()

        # kwargs are ignored
        # self.__dict__.update(**kwargs)

    def get_pose(self):
        if self.is_killed:
            return "hit"
        if self.ladder:
            if self.walk1_mode:
                return "climb1"
            else:
                return "climb2"
        if self.vy != 0:
            return "jump"
        if self.spring != 0:
            return "duck"
        if self.vx == 0:
            return "stand"
        if self.walk1_mode:
            return "walk1"
        else:
            return "walk2"
    
    def asdict(self):
        return {
            "x": self.x,
            "y": self.y,
            "vx": self.vx,
            "vy": self.vy,
            "time_alive": self.time_alive,
            "ladder": self.ladder,
            "spring": self.spring,
            "is_killed": self.is_killed,
            "killed_animation_frame_cnt": self.killed_animation_frame_cnt,
            "finished_level_frame_cnt": self.finished_level_frame_cnt,
            "killed_monster": self.killed_monster,
            "bumped_head": self.bumped_head,
            "collected_coin": self.collected_coin,
            "collected_gem": self.collected_gem,
            "power_up_mode": self.power_up_mode,
            "anim_freq": self.anim_freq,
            "is_facing_right": self.is_facing_right,
            "walk1_mode": self.walk1_mode,
            "pose": self.pose,
        }
    
    def __repr__(self):
        return json.dumps(self.asdict())


class Monster:
    def __init__(
        self, m_id, x, y, vx=0.0, vy=0.0, theme=0,
        is_flying=False, is_walking=False, is_jumping=False, is_dead=False,
        time=0, anim_freq=1, monster_dying_frame_cnt=0, **kwargs
    ):
        self.m_id = m_id
        self.x = x
        self.y = y
        self.vx = vx
        self.vy = vy
        self.theme = theme   # monster type (saw, snail, slime, etc.)
        self.is_flying = is_flying
        self.is_walking = is_walking
        self.is_jumping = is_jumping
        self.is_dead = is_dead
        self.time = time
        self.anim_freq = anim_freq
        self.monster_dying_frame_cnt = monster_dying_frame_cnt
        
        # decide which of the two walk/climb asset to use
        self.walk1_mode = True
        if self.is_jumping:
            # for jumping monster, walk1 asset is decided by vertical speed
            if self.vy != 0:
                self.walk1_mode = False
        elif (self.time // self.anim_freq) % 2 != 0:
            self.walk1_mode = False

        # kwargs are ignored
        # self.__dict__.update(**kwargs)
        
    def asdict(self):
        return {
            "m_id": self.m_id,
            "x": self.x,
            "y": self.y,
            "vx": self.vx,
            "vy": self.vy,
            "theme": self.theme,
            "is_flying": self.is_flying,
            "is_walking": self.is_walking,
            "is_jumping": self.is_jumping,
            "is_dead": self.is_dead,
            "time": self.time,
            "anim_freq": self.anim_freq,
            "monster_dying_frame_cnt": self.monster_dying_frame_cnt,
            "walk1_mode": self.walk1_mode,
        }
    
    def __repr__(self):
        return json.dumps(self.asdict())


def convert_csv_to_json(input_file, output_folder):
    with open(input_file, 'r') as f:
        lines = f.readlines()

    # initialize empty game
    game = Game()
    game_id = -1
    total_frames = 0
    coins_eaten = []   # keep track of which coins were eaten in this game

    for i, l in enumerate(lines):
        if l.startswith('game_id'):
            # game restart, load zoom, seeds, themes, and maze
            if game_id >= 0:
                game_json = os.path.join(output_folder, "level_{:04d}.json".format(game_id))
                game.save_json(game_json)
                game.reset_game()
                
            # increment game_id, reset frame_id and coin status
            game_id += 1
            frame_id = 0
            coins_eaten = []
            
            # general game information in 2nd line
            # game_id,maze_seed,zoom,world_theme_n,agent_theme_n
            data = lines[i + 1].strip().strip(',').split(',')
            assert len(data) == 5   # NOTE: should match how number is saved in game engine
            assert game_id == int(data[0])
            game.game_id = game_id
            game.level_seed = int(data[1])
            game.zoom = float(data[2])   # NOTE: zoom can be float number
            game.world_theme_n = int(data[3])
            game.agent_theme_n = int(data[4])
            
            # maze information in 3rd line
            data = lines[i + 2].strip().strip(',').split(',')
            # NOTE: be careful when maze size is not square
            assert len(data) == game.maze_h * game.maze_w
            # save maze as list of strings, each string is one row
            maze_array = np.array(data).reshape((game.maze_h, -1)).tolist()
            game.maze = [''.join(row) for row in maze_array]

        elif l.startswith('time_alive'):
            # agent & monster information line +1 and +3
            frame = Frame()
            
            # agent information in 2nd line
            # time_alive,agent_x,agent_y,agent_vx,agent_vy,agent_facing_right,agent_ladder,agent_spring,is_killed,killed_animation_frame_cnt,finished_level_frame_cnt,killed_monster,bumped_head,collected_coin,collected_gem,power_up_mode
            data = lines[i + 1].strip().strip(',').split(',')
            assert len(data) == 16   # should match how number is saved in game engine
            frame.frame_id = frame_id
            frame.file_name = "level_{:04d}_frame_{:04d}.png".format(game_id, frame_id)  # new format with level id
            # frame.file_name = "frame_{:04d}.png".format(frame.frame_id)  # old format
            frame.coins_eaten = coins_eaten[:]   # need to assign by value not reference!

            # load other agent info, ignoring agent_facing_right for now (can be inferred from vx)
            frame.agent = Agent(x=float(data[1]), y=float(data[2]),
                                vx=float(data[3]), vy=float(data[4]),
                                time_alive=int(data[0]),
                                ladder=bool(int(data[6])),
                                spring=float(data[7]),
                                is_killed=bool(int(data[8])),
                                killed_animation_frame_cnt=int(data[9]),
                                finished_level_frame_cnt=int(data[10]),
                                killed_monster=bool(int(data[11])),
                                bumped_head=bool(int(data[12])),
                                collected_coin=bool(int(data[13])),
                                collected_gem=bool(int(data[14])),
                                power_up_mode=bool(int(data[15])),
                                )
            
            # monsters information in 4nd line
            # state_time,monsters_count,(m_id,m_x,m_y,m_vx,m_vy,m_theme,m_flying,m_walking,m_jumping,m_dead,m_anim_freq,monster_dying_frame_cnt) <- repeat
            data = lines[i + 3].strip().strip(',').split(',')
            assert (len(data) - 2) % 12 == 0   # should match how number is saved in game engine
            
            # NOTE: state->time may not always match agent->time_alive near game ends
            state_time = int(data[0])
            frame.state_time = state_time
            
            monster_count = int(data[1])
            for mi in range(monster_count):
                si = 2 + mi * 12   # start index of the current monster data
                monster = Monster(m_id=int(data[si]),
                                  x=float(data[si+1]), y=float(data[si+2]),
                                  vx=float(data[si+3]), vy=float(data[si+4]),
                                  theme=int(data[si+5]),
                                  is_flying=bool(int(data[si+6])),
                                  is_walking=bool(int(data[si+7])),
                                  is_jumping=bool(int(data[si+8])),
                                  is_dead=bool(int(data[si+9])),
                                  time=state_time,
                                  anim_freq=int(data[si+10]),
                                  monster_dying_frame_cnt=int(data[si+11])
                                  )
                frame.monsters.append(monster)
            
            game.frames.append(frame)
            frame_id += 1
            total_frames += 1

        elif l.startswith('background_themes'):
            # load theme file names, should only trigger for the first game level (won't reset)
            data = l.strip().strip(',').split(',')
            game.background_themes = data[1:]
            data = lines[i + 1].strip().strip(',').split(',')
            game.ground_themes = data[1:]
            data = lines[i + 2].strip().strip(',').split(',')
            game.agent_themes = data[1:]
            data = lines[i + 3].strip().strip(',').split(',')
            game.monster_names['ground'] = data[1:]
            data = lines[i + 4].strip().strip(',').split(',')
            game.monster_names['flying'] = data[1:]
            data = lines[i + 5].strip().strip(',').split(',')
            game.monster_names['walking'] = data[1:]

        elif l.startswith('eat_coin'):
            # update coins eaten in this game session so far
            data = l.strip().strip(',').split(',')
            assert len(data) == 3   # should only have 'eat_coin' then x and y
            coins_eaten.append((int(data[1]), int(data[2])))

        else:
            continue
    
    # save json for leftover frames
    if len(game.frames) > 0:
        game_json = os.path.join(output_folder, "level_{:04d}.json".format(game_id))
        game.save_json(game_json)
        game_id += 1
    
    return game_id, total_frames


if __name__ == "__main__":
    args = parse_args()

    game_id, total_frames = convert_csv_to_json(args.input, args.output_folder)
    print(f"Converted {total_frames} frames in {game_id} levels")
