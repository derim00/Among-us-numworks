#!/bin/sh
set -e
cd "$(dirname "$0")/.."
mkdir -p host/out
(cd src && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 assets.bin ../host/out/assets_host.o)
gcc -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function -Isrc -Dmain=game_main src/main.c src/gfx.c -c 2>&1 | grep -v '^$' || true
gcc -O1 -g -fsanitize=address,undefined -Wall -Isrc -Dmain=game_main -c src/main.c -o host/out/main.o
gcc -O1 -g -fsanitize=address,undefined -Wall -Isrc -c src/gfx.c -o host/out/gfx.o
gcc -O1 -g -fsanitize=address,undefined -Wall -Isrc -c host/eadk_host.c -o host/out/eadk_host.o
gcc -fsanitize=address,undefined host/out/*.o -lm -o host/out/sim
rm -f main.o gfx.o
