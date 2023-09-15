#!/bin/bash

name=glook
src=$name.c

cc=gcc
std=-std=c89
opt=-O2

wflags=(
    -Wall
    -Wextra
    -pedantic
)

libs=(
    -lglfw
)

mac=(
    -framework OpenGL
)

linux=(
    -lGL
    -lGLEW
)

if echo "$OSTYPE" | grep -q "linux"; then
    libs+=(-lGL -lGLEW)
elif echo "$OSTYPE" | grep -q "darwin"; then 
    libs+=(-framework OpenGL)
fi

cmd() {
    echo "$@" && $@
}

clean() {
    [ -f $name ] && cmd rm -f $name
}

compile() {
    cmd $cc $src -o $name $std $opt ${wflags[*]} ${libs[*]}
}

(( $# < 1 )) && compile && exit

case "$1" in
    "-clean")
        clean;;
    *)
        echo "$0: unknown option: $1";;
esac

