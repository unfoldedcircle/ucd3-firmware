#!/usr/bin/env bash
# Original source: https://github.com/transmission/transmission/blob/main/code_style.sh

# Usage: ./code_style.sh
# Usage: ./code_style.sh --check

set -o noglob

# PATH workaround for SourceTree
# for Intel Mac
PATH="${PATH}:/usr/local/bin"
# for Apple Silicon Mac
PATH="${PATH}:/opt/homebrew/bin"

if [[ "x$1" == *"check"* ]]; then
  echo "checking code format"
else
  echo "formatting source files"
  fix=1
fi

root="$(dirname "$0")"
root="$(cd "${root}" && pwd)"
cd "${root}" || exit 1

trim_comments() {
  # 1. remove comments, ignoring backslash-escaped characters
  # 2. trim spaces
  # 3. remove empty lines
  # note: GNU extensions like +?| aren't supported on macOS
  sed 's/^\(\(\(\\.\)*[^\\#]*\)*\)#.*/\1/;s/^[[:blank:]]*//;s/[[:blank:]]*$//;/^$/d' "$@"
}

get_find_path_args() {
  local args=$(printf " -o -path ./%s" "$@")
  echo "${args:4}"
}

find_cfiles() {
  find . \( $(get_find_path_args $(trim_comments .clang-format-include)) \)\
       ! \( $(get_find_path_args $(trim_comments .clang-format-ignore)) \) "$@"
}

clang_format_exe_names=(
  'clang-format'
)
for name in ${clang_format_exe_names[@]}; do
  clang_format_exe=$(command -v "${name}")
  if [ "$?" -eq 0 ]; then
    clang_format_exe="${name}"
    break
  fi
done
if [ -z "${clang_format_exe}" ]; then
  echo "error: clang-format not found";
  exit 1;
fi

# format C/C++
clang_format_args="$([ -n "$fix" ] && echo '-i' || echo '--dry-run --Werror')"
echo "`find_cfiles`"
if ! find_cfiles -exec "${clang_format_exe}" $clang_format_args '{}' '+'; then
  [ -n "$fix" ] || echo 'C/C++ code needs formatting'
  exitcode=1
fi

exit $exitcode
