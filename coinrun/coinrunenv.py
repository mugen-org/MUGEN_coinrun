"""
Python interface to the CoinRun shared library using ctypes.

On import, this will attempt to build the shared library.

Copyright (c) Meta Platforms, Inc. All Right reserved.
"""

import os
import atexit
import random
import sys
from ctypes import c_int, c_char_p, c_float, c_bool

import gym
import gym.spaces
import numpy as np
import numpy.ctypeslib as npct
from baselines.common.vec_env import VecEnv
from baselines import logger

from coinrun.config import Config

from mpi4py import MPI
from baselines.common import mpi_util

# if the environment is crashing, try using the debug build to get
# a readable stack trace
DEBUG = False
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

def build():
    lrank, _lsize = mpi_util.get_local_rank_size(MPI.COMM_WORLD)
    if lrank == 0:
        dirname = os.path.dirname(__file__)
        if len(dirname):
            make_cmd = "QT_SELECT=5 make -C %s" % dirname
        else:
            make_cmd = "QT_SELECT=5 make"

        r = os.system(make_cmd)
        if r != 0:
            logger.error('coinrun: make failed')
            sys.exit(1)
    MPI.COMM_WORLD.barrier()

build()

if DEBUG:
    lib_path = '.build-debug/coinrun_cpp_d'
else:
    lib_path = '.build-release/coinrun_cpp'

lib = npct.load_library(lib_path, os.path.dirname(__file__))
lib.init.argtypes = [c_int]
lib.get_NUM_ACTIONS.restype = c_int
lib.get_RES_W.restype = c_int
lib.get_RES_H.restype = c_int
lib.get_VIDEORES.restype = c_int

lib.vec_create.argtypes = [
    c_int,    # nenvs
    c_int,    # lump_n
    c_bool,   # collect_data
    c_float,  # default_zoom
    ]
lib.vec_create.restype = c_int

lib.vec_close.argtypes = [c_int]

lib.vec_step_async_discrete.argtypes = [c_int, npct.ndpointer(dtype=np.int32, ndim=1)]

lib.initialize_args.argtypes = [npct.ndpointer(dtype=np.int32, ndim=1), npct.ndpointer(dtype=np.float32, ndim=1)]
lib.initialize_set_monitor_dir.argtypes = [c_char_p, c_int]

lib.vec_wait.argtypes = [
    c_int,
    npct.ndpointer(dtype=np.uint8, ndim=4),    # smaller rgb for input to agent
    npct.ndpointer(dtype=np.uint8, ndim=4),    # larger rgb for hires render
    npct.ndpointer(dtype=np.uint8, ndim=2),    # audio semantic map
    npct.ndpointer(dtype=np.float32, ndim=1),  # reward
    npct.ndpointer(dtype=np.bool, ndim=1),     # done
    npct.ndpointer(dtype=np.bool, ndim=1),     # new_level
    ]

already_inited = False

def init_args_and_threads(cpu_count=4,
                          monitor_csv_policy='all',
                          rand_seed=None):
    """
    Perform one-time global init for the CoinRun library.  This must be called
    before creating an instance of CoinRunVecEnv.  You should not
    call this multiple times from the same process.
    """
    os.environ['COINRUN_RESOURCES_PATH'] = os.path.join(SCRIPT_DIR, 'assets')

    if rand_seed is None:
        rand_seed = random.SystemRandom().randint(0, 1000000000)

        # ensure different MPI processes get different seeds (just in case SystemRandom implementation is poor)
        mpi_rank, mpi_size = mpi_util.get_local_rank_size(MPI.COMM_WORLD)
        rand_seed = rand_seed - rand_seed % mpi_size + mpi_rank

    int_args = np.array([Config.NUM_LEVELS, int(Config.PAINT_VEL_INFO), Config.USE_DATA_AUGMENTATION, Config.SET_SEED, rand_seed, Config.LEVEL_TIMEOUT]).astype(np.int32)
    float_args = np.array([Config.AIR_CONTROL, Config.BUMP_HEAD_PENALTY, Config.DIE_PENALTY, Config.KILL_MONSTER_REWARD, Config.JUMP_PENALTY, Config.SQUAT_PENALTY, Config.JITTER_SQUAT_PENALTY]).astype(np.float32)
    lib.initialize_args(int_args, float_args)
    # this specify the folder to write the monitor csv file in game engine
    csv_folder = Config.SAVE_DIR + '/model_' + (Config.RESTORE_ID or Config.RUN_ID) + '_seed_' + str(Config.SET_SEED) + "/csv" 
    if not os.path.exists(csv_folder):
        os.makedirs(csv_folder)
    lib.initialize_set_monitor_dir(csv_folder.encode('utf-8'), {'off': 0, 'first_env': 1, 'all': 2}[monitor_csv_policy])

    global already_inited
    if already_inited:
        return

    lib.init(cpu_count)
    already_inited = True

@atexit.register
def shutdown():
    global already_inited
    if not already_inited:
        return
    lib.coinrun_shutdown()

class CoinRunVecEnv(VecEnv):
    """
    This is the CoinRun VecEnv, all CoinRun environments are just instances
    of this class.

    `num_envs`: number of environments to create in this VecEnv
    `lump_n`: only used when the environment creates `monitor.csv` files
    `default_zoom`: controls how much of the level the agent can see
    """
    def __init__(self, num_envs, lump_n=0, default_zoom=5.5):
        self.metadata = {'render.modes': []}
        self.reward_range = (-float('inf'), float('inf'))

        self.NUM_ACTIONS = lib.get_NUM_ACTIONS()
        self.RES_W       = lib.get_RES_W()
        self.RES_H       = lib.get_RES_H()
        self.VIDEORES    = lib.get_VIDEORES()
        self.AUDIO_MAP_SIZE = lib.get_AUDIO_MAP_SIZE()

        self.buf_rew = np.zeros([num_envs], dtype=np.float32)
        self.buf_done = np.zeros([num_envs], dtype=np.bool)
        self.buf_new_level = np.zeros([num_envs], dtype=np.bool)
        self.buf_rgb   = np.zeros([num_envs, self.RES_H, self.RES_W, 3], dtype=np.uint8)
        
        self.collect_data = Config.COLLECT_DATA
        if self.collect_data:
            self.buf_render_rgb = np.zeros([num_envs, self.VIDEORES, self.VIDEORES, 3], dtype=np.uint8)
            self.buf_audio_seg_map = np.zeros([num_envs, self.AUDIO_MAP_SIZE], dtype=np.uint8)
        else:
            self.buf_render_rgb = np.zeros([1, 1, 1, 1], dtype=np.uint8)
            self.buf_audio_seg_map = np.zeros([1, 1], dtype=np.uint8)

        num_channels = 1 if Config.USE_BLACK_WHITE else 3
        obs_space = gym.spaces.Box(0, 255, shape=[self.RES_H, self.RES_W, num_channels], dtype=np.uint8)

        super().__init__(
            num_envs=num_envs,
            observation_space=obs_space,
            action_space=gym.spaces.Discrete(self.NUM_ACTIONS),
            )
        self.handle = lib.vec_create(
            self.num_envs,
            lump_n,
            self.collect_data,
            default_zoom)
        self.dummy_info = [{} for _ in range(num_envs)]

    def __del__(self):
        if hasattr(self, 'handle'):
            lib.vec_close(self.handle)
        self.handle = 0

    def close(self):
        lib.vec_close(self.handle)
        self.handle = 0

    def reset(self):
        obs, _, _, _, _, _, r = self.step_wait()
        return obs

    def get_images(self):
        if self.hires_render:
            return self.buf_render_rgb
        else:
            return self.buf_rgb

    def step_async(self, actions):
        assert actions.dtype in [np.int32, np.int64]
        actions = actions.astype(np.int32)
        lib.vec_step_async_discrete(self.handle, actions)

    def step_wait(self):
        self.buf_rew = np.zeros_like(self.buf_rew)
        self.buf_done = np.zeros_like(self.buf_done)
        self.buf_new_level = np.zeros_like(self.buf_new_level)

        lib.vec_wait(
            self.handle,
            self.buf_rgb,
            self.buf_render_rgb,
            self.buf_audio_seg_map,
            self.buf_rew,
            self.buf_done,
            self.buf_new_level)

        obs_frames = self.buf_rgb

        if Config.USE_BLACK_WHITE:
            obs_frames = np.mean(obs_frames, axis=-1).astype(np.uint8)[...,None]

        return obs_frames, self.buf_rew, self.buf_done, self.dummy_info, self.buf_render_rgb, self.buf_audio_seg_map, self.buf_new_level

def make(num_envs, **kwargs):
    return CoinRunVecEnv(num_envs, **kwargs)
