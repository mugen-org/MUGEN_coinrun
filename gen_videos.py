# Copyright (c) Meta Platforms, Inc. All Right reserved.

import glob
import os
import numpy as np
import wave
import imageio
import random
from convert_csv_to_json import Game
import argparse
import re
from construct_data_from_json import (
    define_semantic_color_map, generate_asset_paths, load_assets, load_bg_asset, draw_game_frame
)

def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate videos from png files and audio semantic map"
    )
    parser.add_argument(
        "--input_data", type=str, required=True, help="Where can we find the video frames and audio semantic maps? Typically video_data/restore_id"
    )
    args = parser.parse_args()

    return args

class VideoGenerator:
	def __init__(
		self, 
		input_data,
		video_fps=30, 
		frames_per_video=96, 
		video_sample_rate=96,
		
		input_json_directory="json_metadata",
		input_audio_map_directory="audio_semantic_map",

		output_video_directory="videos",
		output_json_directory="video_metadata",
	):
		self.video_fps = video_fps
		self.video_duration = frames_per_video / video_fps
		self.video_sample_rate = video_sample_rate
		self.frames_per_video = frames_per_video

		self.audio_sample_rate = 48000
		self.audio_samples_per_frame = self.audio_sample_rate // video_fps
		self.audio_map = ["ladder.wav", "jump.wav", "walk.wav", "bump_head.wav", "killed.wav", "collect_coin_notification_2.wav", "monster_killed.wav", "power_up.wav"]
		self.sound_effect_priority = [4, 7, 5, 6, 1, 3, 0, 2]
		self.min_sound_effect_durations = [0, 0, 0, 0, 10, 4, 2, 8]
		self.silence_audio = "silence.wav"
		self.background_music = [os.path.join("coinrun/assets/sound_effects", filename) for filename in ["bg_japan.wav", "bg_technicolor.wav"]]
		self.power_up_multiplier = 3

		self.game = Game()  # for save/load metadata accompanying the video

		self.input_json_directory = os.path.join(input_data, input_json_directory)
		self.input_audio_map_directory = os.path.join(input_data, input_audio_map_directory)
		self.output_json_directory = os.path.join(input_data, output_json_directory)
		self.output_video_directory = os.path.join(input_data, output_video_directory)
		self.filename_prefix = os.path.basename(input_data) + '_'

		if not os.path.exists(self.output_video_directory):
		    os.makedirs(self.output_video_directory)

		if not os.path.exists(self.output_json_directory):
		    os.makedirs(self.output_json_directory)


		self.load_sound_effects()

		self.preprocess_audio_map_files()

		self.json_level_files = sorted([path for path in glob.glob(self.input_json_directory + "/*.json")])
		self.game.load_json(self.json_level_files[0])

		# load assets according to game world grid size
		# NOTE: game engine now hard-code both kx and ky with 64 for maze_size
		self.kx: float = self.game.zoom * self.game.video_res / self.game.maze_w 
		self.ky: float = self.kx
		zx = self.game.video_res * self.game.zoom
		zy = zx
		semantic_color_map = define_semantic_color_map()

		self.asset_map = {}
		for world_theme_n in range(2):
			self.game.world_theme_n = world_theme_n
			asset_files = generate_asset_paths(self.game)
			self.asset_map[world_theme_n] = load_assets(asset_files, semantic_color_map, self.kx, self.ky, True)

			# background asset is loaded separately due to not following the grid
			self.asset_map[world_theme_n]['background'] = load_bg_asset(asset_files, semantic_color_map, zx, zy)


	def load_sound_effects(self):
		self.audio_file_readers = []
		self.audio_params = []
		for wav_path in self.audio_map + [self.silence_audio]:
		    wav_in = wave.open(os.path.join("coinrun/assets/sound_effects", wav_path), 'rb')
		    self.audio_file_readers.append(wav_in)
		    self.audio_params.append(wav_in.getparams())


	def find_sound_triggered(self, audio_map):
		for sound_effect_idx in self.sound_effect_priority:
			if (audio_map[sound_effect_idx]) == '1':
				return sound_effect_idx
			
		return None


	def get_silence_pad(self, num_samples_silence):
		self.audio_file_readers[-1].rewind()
		silence_pad = self.audio_file_readers[-1].readframes(num_samples_silence)
		
		return silence_pad


	def find_sound_duration(self, start_frame_num, sounds_triggered, min_duration=0):
		idx_of_next_sound_trigger = start_frame_num + 1
		num_samples_for_sound = self.audio_samples_per_frame
		num_sounds_to_skip = 0
		while idx_of_next_sound_trigger < len(sounds_triggered) and (
			sounds_triggered[idx_of_next_sound_trigger] is None or (
				idx_of_next_sound_trigger - start_frame_num < min_duration and 
				self.sound_effect_priority.index(sounds_triggered[idx_of_next_sound_trigger]) >= 
				self.sound_effect_priority.index(sounds_triggered[start_frame_num]))):
			if sounds_triggered[idx_of_next_sound_trigger] is not None:
				num_sounds_to_skip += 1
			idx_of_next_sound_trigger += 1
			num_samples_for_sound += self.audio_samples_per_frame

		return num_samples_for_sound, num_sounds_to_skip


	def write_sound_effects_file(self, video_fn, audio_maps):
		wav_out = wave.open(os.path.join(self.output_video_directory, video_fn + '_effects.wav'), 'wb')
		wav_out.setparams(self.audio_params[0]) # all sound effects need to have the same parameters

		# preprocess audio maps to choose one sound
		sounds_triggered = [self.find_sound_triggered(m) for m in audio_maps]

		# pad the beginning with silence until first sound effect
		num_sounds_to_skip = 0
		if sounds_triggered[0] is None:
			beginning_silence_pad_duration, _ = self.find_sound_duration(0, sounds_triggered)
			silence_pad = self.get_silence_pad(beginning_silence_pad_duration)
			wav_out.writeframes(silence_pad)
		for frame_num, sound_triggered in enumerate(sounds_triggered):
			if sound_triggered is not None:
				if num_sounds_to_skip > 0:
					num_sounds_to_skip -= 1
					continue
				num_samples_for_sound, num_sounds_to_skip = self.find_sound_duration(frame_num, sounds_triggered, self.min_sound_effect_durations[sound_triggered])
				
				self.audio_file_readers[sound_triggered].rewind() # make sure we read from the beginning of the sound
				sound_effect_truncated = self.audio_file_readers[sound_triggered].readframes(num_samples_for_sound)
				wav_out.writeframes(sound_effect_truncated)

				if self.audio_params[sound_triggered][3] < num_samples_for_sound:
					# need to pad with silence
					silence_pad = self.get_silence_pad(num_samples_for_sound - self.audio_params[sound_triggered][3])
					wav_out.writeframes(silence_pad)

		wav_out.close()


	def write_background_track_and_mix(self, audio_maps, video_fn, video_index):
		background_music_wav = wave.open(self.background_music[self.game.world_theme_n], 'rb')
		background_music_duration = background_music_wav.getnframes() // background_music_wav.getframerate()
		background_music_start = (video_index * self.video_duration) % (background_music_duration - self.video_duration * self.power_up_multiplier)
		background_music_wav.readframes(int(self.audio_sample_rate*background_music_start))

		power_up_mode_wav_out = wave.open(os.path.join(self.output_video_directory, video_fn + '_pm_bg.wav'), 'wb')
		power_up_mode_wav_out.setparams(background_music_wav.getparams())
		power_up_mode_wav_out.setframerate(self.audio_sample_rate * self.power_up_multiplier)

		normal_mode_wav_out = wave.open(os.path.join(self.output_video_directory, video_fn + '_nm_bg.wav'), 'wb')
		normal_mode_wav_out.setparams(background_music_wav.getparams())
		normal_mode_wav_out.setframerate(self.audio_sample_rate)

		for frame_num, audio_map in enumerate(audio_maps):
			power_up_mode = audio_map[-1] == '1'
			if power_up_mode:
				power_up_mode_wav_out.writeframes(background_music_wav.readframes(self.audio_samples_per_frame * self.power_up_multiplier))
				normal_mode_wav_out.writeframes(self.get_silence_pad(self.audio_samples_per_frame))
			else:
				normal_mode_wav_out.writeframes(background_music_wav.readframes(self.audio_samples_per_frame))
				power_up_mode_wav_out.writeframes(self.get_silence_pad(self.audio_samples_per_frame * self.power_up_multiplier))
			
		power_up_mode_wav_out.close()
		normal_mode_wav_out.close()

		os.system("ffmpeg -y -i {} -i {} -filter_complex amix=inputs=2:duration=longest {}".format(
			os.path.join(self.output_video_directory, video_fn + '_pm_bg.wav'),
			os.path.join(self.output_video_directory, video_fn + '_nm_bg.wav'),
			os.path.join(self.output_video_directory, video_fn + '_bg_mix.wav')))
		os.system("ffmpeg -y -i {} -i {} -filter_complex amix=inputs=2:duration=longest {}".format(
			os.path.join(self.output_video_directory, video_fn + '_bg_mix.wav'),
			os.path.join(self.output_video_directory, video_fn + '_effects.wav'),
			os.path.join(self.output_video_directory, video_fn + '_final_mix.wav')))
		os.system("ffmpeg -y -i {} -i {} {}".format(
			os.path.join(self.output_video_directory, video_fn + '_final_mix.wav'),
			os.path.join(self.output_video_directory, video_fn + '_tmp.mp4'),
			os.path.join(self.output_video_directory, video_fn + '.mp4')))

		os.remove(os.path.join(self.output_video_directory, video_fn + '_pm_bg.wav'))
		os.remove(os.path.join(self.output_video_directory, video_fn + '_nm_bg.wav'))
		os.remove(os.path.join(self.output_video_directory, video_fn + '_bg_mix.wav'))
		os.remove(os.path.join(self.output_video_directory, video_fn + '_effects.wav'))
		os.remove(os.path.join(self.output_video_directory, video_fn + '_final_mix.wav'))
		os.remove(os.path.join(self.output_video_directory, video_fn + '_tmp.mp4'))


	def is_interesting_level(self, level_num):
		if len(self.game.frames) < self.frames_per_video:
			return False

		return any(self.find_sound_triggered(m) in (4, 5, 6) for m in self.audio_map_data[level_num])


	def save_videos_for_level(
		self,
		level_num, 
		):
		n_frames_in_level = len(self.game.frames)
		random.seed(self.game.level_seed) # want this to be reproducible
		start_frame_idx = random.randint(0, min(self.video_sample_rate, max(n_frames_in_level - self.frames_per_video, 0))) # choose an initial frame
		n_vids = 0
		for video_index, start_frame in enumerate(range(start_frame_idx, n_frames_in_level, self.video_sample_rate)):
			if start_frame + self.frames_per_video <= n_frames_in_level:
				video_fn = self.filename_prefix + 'level_{:04d}_video_frames_{:04d}_to_{:04d}'.format(level_num, start_frame, start_frame + self.frames_per_video - 1)

				writer = imageio.get_writer(os.path.join(self.output_video_directory, video_fn + '_tmp.mp4'), fps=self.video_fps)
				for i in range(start_frame, start_frame + self.frames_per_video):
					writer.append_data(np.array(draw_game_frame(self.game, i, self.asset_map[self.game.world_theme_n], self.kx, self.ky, gen_original=True)))
				writer.close()

				# save json within the range of frames used in the video
				json_output_path = os.path.join(self.output_json_directory, video_fn + '.json')
				self.game.save_json(json_output_path, f_start=start_frame, f_end=(start_frame + self.frames_per_video))
				
				self.write_sound_effects_file(
					video_fn,
					self.audio_map_data[level_num][start_frame:start_frame + self.frames_per_video],
				)

				self.write_background_track_and_mix(
					self.audio_map_data[level_num][start_frame:start_frame + self.frames_per_video],
					video_fn,
					n_vids
				)

				n_vids += 1

		return n_vids


	def preprocess_audio_map_files(self):
		with open(self.input_audio_map_directory + "/audio_map.txt", 'r') as f:
			lines = f.readlines()

		self.audio_map_data = {}
		curr_level = 0
		for l in lines:
			level_num = re.search(r"(?<=level_)[0-9]{4}", l)
			if level_num is not None:
				curr_level = int(level_num.group(0))
				self.audio_map_data[curr_level] = []
			else:
				decoded_audio_map = l[:-1].split(',')
				self.audio_map_data[curr_level].append(decoded_audio_map)


	def generate_videos(self):
		tot_videos_generated = 0
		for level_json in self.json_level_files:
			level_num = int(re.search(r"(?<=level_)[0-9]{4}", level_json).group(0))
			self.game.load_json(level_json)

			if not self.is_interesting_level(level_num):
				continue

			tot_videos_generated += self.save_videos_for_level(level_num)
	

		return tot_videos_generated


if __name__ == "__main__":
    args = parse_args()

    video_generator = VideoGenerator(input_data=args.input_data)
    tot_videos_generated = video_generator.generate_videos()

    print(f"Generated {tot_videos_generated} videos")

