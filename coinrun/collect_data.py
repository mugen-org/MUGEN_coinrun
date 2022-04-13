"""
Load an agent trained with train_agent.py and collect video data.

Copyright (c) Meta Platforms, Inc. All Right reserved.
"""

import time

import tensorflow as tf
import numpy as np
from coinrun import setup_utils
import coinrun.main_utils as utils
from coinrun.config import Config
from coinrun import policies, wrappers
import copy

import os
import cv2
import numpy as np
from sys import platform

can_render = platform == 'darwin'
if can_render:
    from gym.envs.classic_control import rendering

class DataCollector:
    def __init__(self):
        self.run_identifier = 'model_' + Config.RESTORE_ID + '_seed_' + str(Config.SET_SEED)
        self.save_audio_seg_map_path = os.path.join(Config.SAVE_DIR, self.run_identifier, 'audio_semantic_map', 'audio_map.txt')

        if not os.path.exists(Config.SAVE_DIR):
            os.mkdir(Config.SAVE_DIR)
        if not os.path.exists(os.path.join(Config.SAVE_DIR, self.run_identifier)):
            os.mkdir(os.path.join(Config.SAVE_DIR, self.run_identifier))
        if not os.path.exists(os.path.dirname(self.save_audio_seg_map_path)):
            os.mkdir(os.path.dirname(self.save_audio_seg_map_path))

        if os.path.exists(self.save_audio_seg_map_path):
            os.remove(self.save_audio_seg_map_path)

        self.level_num = 0
        self.frame_num = 0
        self.num_levels_to_collect = Config.NUM_LEVELS_TO_COLLECT

        with open(self.save_audio_seg_map_path, "a") as f:
            f.write("level_{:04d}\n".format(self.level_num))
         

    def save_frame_info(self, audio_seg_map_array):
        with open(self.save_audio_seg_map_path, "a") as f:
            f.write(','.join([str(s) for s in audio_seg_map_array]) + "\n")

        self.frame_num += 1

    def should_continue(self):
        return self.level_num < self.num_levels_to_collect

    def new_level(self):
        self.level_num += 1
        self.frame_num = 0

        with open(self.save_audio_seg_map_path, "a") as f:
            f.write("level_{:04d}\n".format(self.level_num))


def create_act_model(sess, env, nenvs):
    ob_space = env.observation_space
    ac_space = env.action_space

    policy = policies.get_policy()
    act = policy(sess, ob_space, ac_space, nenvs, 1, reuse=False)

    return act

def run_env_sess(sess):
    env = utils.make_general_env(1)
    env = wrappers.add_final_wrappers(env)
    nenvs = env.num_envs
    agent = create_act_model(sess, env, nenvs)

    sess.run(tf.global_variables_initializer())
    loaded_params = utils.load_params_for_scope(sess, 'model')
    if not loaded_params:
        print('NO save PARAMS LOADED')

    obs = env.reset()
    state = agent.initial_state
    done = np.zeros(nenvs)
    if can_render:
        viewer = rendering.SimpleImageViewer()
    data_collector = DataCollector()
    curr_rews = 0

    while data_collector.should_continue():
        action, values, state, _ = agent.step(obs, state, done)
        obs, rew, done, info, high_res_render, audio_seg_map, new_level = env.step(action)

        if new_level[0]:
            print('level %i reward: %d' % (data_collector.level_num, curr_rews))
            curr_rews = 0
            data_collector.new_level()


        data_collector.save_frame_info(audio_seg_map[0,:])
        curr_rews += rew[0]
        if can_render:
            viewer.imshow(high_res_render[0,:,:,-3:])

def main():
    tf.set_random_seed(2) # to ensure reproducibility of agent
    utils.setup_mpi_gpus()
    setup_utils.setup_and_load()
    with tf.Session() as sess:
        run_env_sess(sess)

if __name__ == '__main__':
    main()