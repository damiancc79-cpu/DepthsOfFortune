#!/bin/zsh
cd "$(dirname "$0")"
if [[ ! -x depths_of_fortune || main.cpp -nt depths_of_fortune ]]; then
  echo "Forging the dungeon..."
  clang++ -std=c++17 -O2 -Wall -Wextra main.cpp -o depths_of_fortune || {
    echo "Build failed. This game needs the standard macOS Command Line Tools."
    read -k 1
    exit 1
  }
fi
./depths_of_fortune
