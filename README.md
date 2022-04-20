# MUGEN Dataset
[Project Page](https://mugen-org.github.io/) | [Paper](https://arxiv.org/pdf/2204.08058.pdf)

## Setup
```
conda create --name MUGEN python=3.6
conda activate MUGEN
pip install --ignore-installed https://storage.googleapis.com/tensorflow/linux/gpu/tensorflow_gpu-1.12.0-cp36-cp36m-linux_x86_64.whl 
module load cuda/9.0
module load cudnn/v7.4-cuda.10.0
git clone coinrun_MUGEN
cd coinrun_MUGEN
pip install -r requirements.txt
conda install -c conda-forge mpi4py
pip install -e .
```

## Training Agents

### Basic training commands:

```
python -m coinrun.train_agent --run-id myrun --save-interval 1
```

After each parameter update, this will save a copy of the agent to `./saved_models/`. Results are logged to `/tmp/tensorflow` by default.

Run parallel training using MPI:

```
mpiexec -np 8 python -m coinrun.train_agent --run-id myrun
```

Train an agent on a fixed set of N levels. With N = 0, the training set is unbounded.

```
python -m coinrun.train_agent --run-id myrun --num-levels N
```

Continue training an agent from a checkpoint:

```
python -m coinrun.train_agent --run-id newrun --restore-id myrun
```


View training options:

```
python -m coinrun.train_agent --help
```

### Example commands for MUGEN agents:
Base model
```
python -m coinrun.train_agent --run-id name_your_agent \
                --architecture impala --paint-vel-info 1 --dropout 0.0 --l2-weight 0.0001 \
                --num-levels 0 --use-lstm 1 --num-envs 96 --set-seed 80 \
                --bump-head-penalty 0.25 -kill-monster-reward 10.0
```
Add squat penalty to reduce excessive squating
```
python -m coinrun.train_agent --run-id gamev2_fine_tune_m4_squat_penalty \
                --architecture impala --paint-vel-info 1 --dropout 0.0 --l2-weight 0.0001 \
                --num-levels 0 --use-lstm 1 --num-envs 96 --set-seed 811 \
                --bump-head-penalty 0.1 --kill-monster-reward 5.0 --squat-penalty 0.1 \
                --restore-id gamev2_fine_tune_m4_0
```
Larger model
```
python -m coinrun.train_agent --run-id gamev2_largearch_bump_head_penalty_0.05_0 \
                --architecture impalalarge --paint-vel-info 1 --dropout 0.0 --l2-weight 0.0001 \
                --num-levels 0 --use-lstm 1 --num-envs 96 --set-seed 51 \
                --bump-head-penalty 0.05 -kill-monster-reward 10.0
```
Add reward for dying
```
python -m coinrun.train_agent --run-id gamev2_fine_tune_squat_penalty_die_reward_3.0 \
                --architecture impala --paint-vel-info 1 --dropout 0.0 --l2-weight 0.0001 \
                --num-levels 0 --use-lstm 1 --num-envs 96 --set-seed 857 \
                --bump-head-penalty 0.1 --kill-monster-reward 5.0 --squat-penalty 0.1 \
                --restore-id gamev2_fine_tune_m4_squat_penalty --die-penalty -3.0
```
Add jump penalty
```
python -m coinrun.train_agent --run-id gamev2_fine_tune_m4_jump_penalty \
                --architecture impala --paint-vel-info 1 --dropout 0.0 --l2-weight 0.0001 \
                --num-levels 0 --use-lstm 1 --num-envs 96 --set-seed 811 \
                --bump-head-penalty 0.1 --kill-monster-reward 10.0 --jump-penalty 0.1 \
                --restore-id gamev2_fine_tune_m4_0
```

## Data Collection

Collect video data with trained agent. The following command will create a folder
{save_dir}/{model_name}\_seed_{seed}, which contains the audio semantic maps to reconstruct
game audio, as well as the csv containing all game metadata. We use the csv for reconstructing
video data in the next step. 

```
python -m coinrun.collect_data --collect_data --paint-vel-info 1 \
                --set-seed 406 --restore-id gamev2_fine_tune_squat_penalty_timeout_300 \
                --save-dir <INSERT_SAVE_DIR> \
                --level-timeout 600 --num-levels-to-collect 2000
```
The next step is to create 3.2 second videos with audio by running the script gen_videos.sh. This script first
parses the csv metadata of agent gameplay into a json format. Then, we sample 3 second clips,
render to RGB, generate audio, and save .mp4s. Note that we apply some sampling logic in gen_videos.py
to only generate videos for levels of sufficient length and with interesting game events. You can
adjust the sampling logic to your liking here.

There are three outputs from this script:
1. ./json_metadata - where full level jsons are saved for longer video rendering
2. ./video_metadata - where 3.2 second video jsons are saved
3. ./videos - where 3.2s .mp4 videos with audio are saved. We use these videos for human annotation.
```
bash gen_videos.sh <INSERT_SAVE_DIR> <AGENT_COLLECTION_DIR>
```
For example:
```
bash gen_videos.sh video_data model_gamev2_fine_tune_squat_penalty_timeout_300_seed_406
```

## License Info
The majority of MUGEN is licensed under CC-BY-NC, however portions of the project are available under separate license terms: CoinRun, VideoGPT, VideoCLIP, and S3D are licensed under the MIT license; Tokenizer is licensed under the Apache 2.0 Pycocoevalcap is licensed under the BSD license; VGGSound is licensed under the CC-BY-4.0 license.


