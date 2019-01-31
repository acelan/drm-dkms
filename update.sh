#!/bin/bash

gpu_drm="amd bridge i2c i915 nouveau panel radeon scheduler ttm"
include_drm="drm"
include_uapi="drm"

function print_usage {
	echo "$0 kernel_path"
}

if [ ${#} != 1 ] ; then
	print_usage $0
	exit
fi
kernel_path=$1

drm_path="$kernel_path/drivers/gpu/drm"
if [ ! -d $drm_path ] ; then
	echo "Can't find drm path"
	print_usage $0
	exit
fi

`rsync -d --delete $drm_path/* drivers/gpu/drm/`
for i in $gpu_drm; do
	`rsync -r --delete $drm_path/$i drivers/gpu/drm/`
done

include_path="$kernel_path/include"
for i in $include_drm; do
	`rsync -r --delete $include_path/$i include/`
done

for i in $include_uapi; do
	`rsync -r --delete $include_path/uapi/$i include/uapi`
done
