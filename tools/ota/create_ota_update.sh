#! /bin/bash
#
# UCD3 OTA update creation script.
# Creates the Hawkbit deployment artifacts of a firmware build.

set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

BINARIES_DIR="${SCRIPT_DIR}/release"

#=============================================================

function create_ota_update() {
    pushd "${BINARIES_DIR}"

    echo "Generating hashes for ${upd_img_name} ..."
    shasum -b -a 256 "${upd_img_name}" > "${upd_img_name}.sha256sum"
    shasum -b -a 1   "${upd_img_name}" > "${upd_img_name}.sha1sum"
    # MD5SUM file required for hawkBit API compliance
    md5sum -b "${upd_img_name}" > "${upd_img_name}.MD5SUM"

    # check if it's a development build or a release build
    local channel=""
    local short_version=$(cd "$SCRIPT_DIR/../.." && "${git_semver_path}git-semver" -no-pre)
    if [[ v$BUILD_VERSION == $short_version ]]; then
      channel="TESTING"
    else
      channel="DEVELOPMENT"
    fi
    echo "OTA metadata channel: $channel"

    local upd_metadata_name="${base_name}.json"

    echo "Creating OTA update metadata: $upd_metadata_name"
    node "${SCRIPT_DIR}/create-ota-firmware-meta.js" "$upd_img_name" "$BUILD_VERSION" "$channel" "$upd_metadata_name"

    shasum -a 256 "${upd_metadata_name}" > "${upd_metadata_name}.sha256sum"
    shasum -a 1   "${upd_metadata_name}" > "${upd_metadata_name}.sha1sum"
    md5sum "${upd_metadata_name}" > "${upd_metadata_name}.MD5SUM"

    node "${SCRIPT_DIR}/create-ota-hawkbit-meta.js" "$BUILD_VERSION" "$upd_metadata_name" "$upd_img_name"
    mv deployment.json "${base_name}.deployment.json"

    popd
}

#=============================================================

usage() {
  cat << EOF
Create an UCD3 firmware update for the UC OTA server

Usage:
$0 FW_FILE REVISION VERSION 

  FW_FILE   signed firmware file path (usually firmware.bin from the build directory)
  REVISION  board revision without 'r' prefix, e.g. 1.1
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

if [[ ! "$#" -eq 3 ]]; then
  usage
fi

# command line arguments
FW_FILE=$(realpath $1)
REVISION=$2
BUILD_VERSION=$3

if [[ ! -f $FW_FILE ]]; then
    echo "ERROR: firmware file not found: \"$FW_FILE\"" >&2
    exit 1;
fi

base_name="UCD3-firmware-r${REVISION}-v${BUILD_VERSION}"
upd_img_name="${base_name}.bin.signed"


git_semver_path=""

if [[ ! -x "$(command -v git-semver)" ]]; then
    echo 'WARN: git-semver is not installed, using local Linux version' >&2
    git_semver_path="${SCRIPT_DIR}/../git-semver/linux_amd64/"

    if [[ "$(uname)" == "Darwin" ]] || [[ ! -x "${git_semver_path}git-semver" ]]; then
        echo 'ERROR: git-semver is not availabe. Check https://github.com/mdomke/git-semver#installation' >&2
        exit 1
    fi
fi

if [[ ! -x "$(command -v node)" ]]; then
    echo 'ERROR: node.js is not installed' >&2
    exit 1
fi

mkdir -p "$BINARIES_DIR"
cp "$FW_FILE" "$BINARIES_DIR/$upd_img_name"

create_ota_update
