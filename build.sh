#!/usr/bin/env bash
# Build Kadran from source and open it: macOS, Linux (Ubuntu/Debian, Raspberry Pi OS), and Windows from Git Bash (it runs build.ps1;
# without Git Bash, double-click build.cmd).
#
#   ./build.sh                 install what is missing, build, open Kadran
#   ./build.sh --no-install    do not install anything (the requirements are there)
#   ./build.sh --no-run        build only
#   ./build.sh --clean         start from an empty build folder
#   ./build.sh --debug         a Debug build
#   ./build.sh --tests         also build the tests and run them
#
# What it installs: macOS, with Homebrew: qt cmake git. Ubuntu/Debian, with apt: a compiler, cmake, git and Qt 6 (base, WebSockets).
set -euo pipefail

cd "$(dirname "$0")"
INSTALL=1 RUN=1 CLEAN=0 TYPE=Release TESTS=OFF
for arg in "$@"; do
  case "$arg" in
    --no-install) INSTALL=0 ;;
    --no-run) RUN=0 ;;
    --clean) CLEAN=1 ;;
    --debug) TYPE=Debug ;;
    --tests) TESTS=ON ;;
    -h|--help) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Unknown option: $arg (see ./build.sh --help)" >&2; exit 2 ;;
  esac
done

# Windows (Git Bash, MSYS2, Cygwin): the PowerShell script does the work, with the same options
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$(pwd)/build.ps1")" "$@" ;;
esac

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31mError: %s\033[0m\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
brew_shellenv() {
  local b
  for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do
    if [[ -x $b ]]; then eval "$("$b" shellenv)"; return; fi
  done
}

OS=$(uname -s)
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE="$TYPE" -DKADRAN_TESTS="$TESTS")

# --- Requirements
if [[ $OS == Darwin ]]; then
  if ! xcode-select -p >/dev/null 2>&1; then
    step "Installing Apple's command line tools (a window opens: accept, wait for the end, then run ./build.sh again)"
    xcode-select --install || true
    exit 1
  fi
  have brew || brew_shellenv
  if ! have brew; then
    [[ $INSTALL == 1 ]] || fail "Homebrew is needed (https://brew.sh)"
    step "Installing Homebrew (https://brew.sh): it asks for your password"
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    brew_shellenv
    have brew || fail "Homebrew is not available after its installation: open a new terminal and run ./build.sh again"
  fi
  if [[ $INSTALL == 1 ]]; then
    step "Installing the requirements with Homebrew"
    brew install qt cmake git
  fi
  CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$(brew --prefix qt)")
elif [[ $OS == Linux ]]; then
  if [[ $INSTALL == 1 ]]; then
    if have apt-get; then
      step "Installing the requirements with apt (it asks for your password)"
      SUDO=""
      [[ $(id -u) == 0 ]] || SUDO=sudo
      $SUDO apt-get update
      $SUDO apt-get install -y build-essential cmake git qt6-base-dev qt6-websockets-dev libgl1-mesa-dev
    else
      echo "No apt here: install a C++ compiler, cmake, git, Qt 6 (base and WebSockets), then run ./build.sh --no-install."
      echo "Trying with what is installed."
    fi
  fi
else
  fail "Unsupported system $OS"
fi
have cmake || fail "cmake is not installed"
have git || fail "git is not installed"

# --- Sources: the submodule (third_party/pxs)
if [[ -d .git ]]; then
  step "Fetching the submodules"
  git submodule update --init --recursive
elif [[ ! -f third_party/pxs/CMakeLists.txt ]]; then
  fail "third_party/pxs is empty: clone the repository with git (git clone --recurse-submodules https://github.com/PixelStereo/Kadran.git)"
fi

# --- Build
if [[ $CLEAN == 1 ]]; then rm -rf build; fi
step "Configuring (cmake ${CMAKE_ARGS[*]})"
cmake -S . -B build "${CMAKE_ARGS[@]}"
step "Building"
JOBS=$( (have nproc && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build build -j "$JOBS"
if [[ $TESTS == ON ]]; then
  step "Testing"
  QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
fi

# --- Open
APP_NAME=$(python3 -c 'import json; print(json.load(open("build/application.json"))["name"])' 2>/dev/null || sed -n 's/.*"name": *"\([^"]*\)".*/\1/p' build/application.json | head -1)
[[ -n $APP_NAME ]] || fail "build/application.json has no application name"
if [[ $OS == Darwin ]]; then
  APP="build/$APP_NAME.app"
  [[ -d $APP ]] || fail "$APP was not built"
  printf '\nKadran is built: %s\n' "$APP"
  [[ $RUN == 1 ]] && open "$APP"
else
  APP="build/$APP_NAME"
  [[ -x $APP ]] || fail "$APP was not built"
  printf '\nKadran is built: %s\n' "$APP"
  [[ $RUN == 1 ]] && { "$APP" & }
fi
exit 0
