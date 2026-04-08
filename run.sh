#!/bin/zsh
source ~/.zshrc

INSTALL="$HOME/programs/compiled/install"

mkdir -p build && \
cd build && \
meson setup .. --prefix "$INSTALL" --libdir="$INSTALL/lib" && \
meson compile && \
meson install && \
mkp
