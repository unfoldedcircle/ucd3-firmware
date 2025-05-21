#!/bin/bash
#
# UCD3 release upload to UC OTA server
# Uploads a firmware build to the OTA server staging directory.

set -e

# default values, overridable with ENV variables
OTA_SOURCE_PATH=${OTA_SOURCE_PATH:-release}
OTA_SERVER_USER=${OTA_SERVER_USER:-ota}
OTA_SERVER_BASEPATH=${OTA_SERVER_BASEPATH:-/tmp}
OTA_CHANNEL=${OTA_CHANNEL:-staging}

#=============================================================

usage() {
  cat << EOF
Upload an UCD3 firmware update to UC OTA server in the $OTA_CHANNEL channel

Usage:
$0 REVISION VERSION 

  REVISION  board revision without 'r' prefix, e.g. 1.0
  VERSION   build version without 'v' prefix, e.g. 0.8.0

EOF
  exit 1
}

#=============================================================

#------------------------------------------------------------------------------
# Start of script
#------------------------------------------------------------------------------
# check command line arguments
while getopts "h" opt; do
  case ${opt} in
    h )
        usage
        ;;
    : )
        echo "Option: -$OPTARG requires an argument" 1>&2
        usage
        ;;
   \? )
        echo "Invalid option: -$OPTARG" 1>&2
        usage
        ;;
  esac
done

if [[ ! "$#" -eq 2 ]]; then
  usage
fi

# command line arguments
revision=$1
version=$2

base_name=UCD3-firmware-r$revision-v$version
ota_server_path=${OTA_SERVER_BASEPATH}/${OTA_CHANNEL}/UCD3/$revision/$version/

if [[ -z "$OTA_SERVER" ]]; then
    echo "Missing OTA_SERVER environment variable"
    exit 1
fi

echo "OTA channel: $OTA_CHANNEL"

# constants
fileNames="${base_name}.bin.signed ${base_name}.bin.signed.MD5SUM ${base_name}.bin.signed.sha1sum ${base_name}.bin.signed.sha256sum ${base_name}.json ${base_name}.json.MD5SUM ${base_name}.json.sha1sum ${base_name}.json.sha256sum ${base_name}.deployment.json"

read -r -a files <<< "$fileNames"
for file in "${files[@]}"; do
  echo "OTA deployment: $file"
  if [[ ! -f $OTA_SOURCE_PATH/$file ]]; then
    echo "OTA update file not found: $OTA_SOURCE_PATH/$file"
    exit 1;
  fi
done

echo "Ensure OTA target directory doesn't contain a release: $ota_server_path"
# shellcheck disable=SC2029
if ssh "$OTA_SERVER_USER@$OTA_SERVER" "ls -A1q $ota_server_path 2> /dev/null | grep -q ."; then
  echo "OTA target directory is not empty! Aborting"
  exit 1;
fi

pushd "$OTA_SOURCE_PATH"

echo "Starting upload: $OTA_SERVER_USER@$OTA_SERVER:$ota_server_path"
ssh "$OTA_SERVER_USER@$OTA_SERVER" mkdir -p "$ota_server_path"

# shellcheck disable=SC2086
scp $fileNames "$OTA_SERVER_USER@$OTA_SERVER:$ota_server_path"
# shellcheck disable=SC2029
ssh "$OTA_SERVER_USER@$OTA_SERVER" mv "$ota_server_path/${base_name}.deployment.json" "$ota_server_path/deployment.json"

popd
