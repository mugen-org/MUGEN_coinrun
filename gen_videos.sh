# Copyright (c) Meta Platforms, Inc. All Right reserved.

#!/bin/bash

data_root=$1
model_id=$2

echo "Generating videos for $data_root/$model_id"
python3 convert_csv_to_json.py --data_root $data_root --restore_id $model_id
python3 gen_videos.py --input_data $1/$2
