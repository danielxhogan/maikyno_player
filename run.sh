#!/bin/zsh
source ~/.zshrc

meson setup build
cd ./build && ninja && ./src/app/mkp
