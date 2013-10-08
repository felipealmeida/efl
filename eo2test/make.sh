#! /bin/bash

S="../src/lib/eo/*.c"
I="-I../src/lib/eo -I../build -I../ -Ibuild -g -ggdb3"
D="-DHAVE_CONFIG_H=1"
L=`pkg-config --cflags --libs eina`

CC=${CC:-gcc}

$CC $I $CFLAGS -save-temps  eo3_simple.c eo_simple.c eo2_simple.c eo2-bench.c eo2_inherit.c eo_inherit.c -DNOMAIN $S $D $L -std=gnu99 -lrt -o eo2-bench && ./eo2-bench || exit 1

g++ $CFLAGS $S $I $D --std=gnu++11 simplesignal.cc -fpermissive $L -o simplesignal && ./simplesignal || exit 1
