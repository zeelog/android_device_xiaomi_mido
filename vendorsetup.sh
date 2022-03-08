#!/usr/bin/env bash

DIR="$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

echo 'Downloading latest prebuilt Camera.apk from GrapheneOS...'
camera_dir=/tmp/GrapheneOSCamera
camera_branch=12.1
rm -rf $camera_dir &&
git clone --depth=1 https://github.com/GrapheneOS/platform_external_Camera.git -b $camera_branch $camera_dir &&
mv $camera_dir/prebuilt/Camera.apk "$DIR"/GrapheneCamera/ || exit $?
