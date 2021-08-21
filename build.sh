#!/bin/bash

src=*.c
cc=gcc
exe=glook

std=(
    -std=c99
    -Wall
    -Wextra
    -O2
)

inc=(
    -Iglee/
)

lib=(
    -Llib/
    -lglee
    -lglfw
)

mac=(
    -framework OpenGL
    # -mmacos-version-min=10.9
)

linux=(
    -lGL
    -lGLEW
)

build() {
    mkdir lib
    pushd glee/ &&./build.sh -s && popd && mv glee/libglee.a lib/libglee.a
}

clean() {
    rm -r lib/
}

compile() {
    if echo "$OSTYPE" | grep -q "linux"; then
        $cc $src -o $exe ${std[*]} ${inc[*]} ${lib[*]} ${linux[*]} 
    elif echo "$OSTYPE" | grep -q "darwin"; then 
        $cc $src -o $exe ${std[*]} ${inc[*]} ${lib[*]} ${mac[*]}
    else
        echo "OS is not supported yet..." && exit
    fi
}

install() {
    sudo mv $exe /usr/local/bin/$exe
}

fail() {
    echo "Use with '-build' to build dependencies, '-comp' to compile the tool"
    exit
}

case "$1" in
    "-run")
        shift
        compile && ./$exe "$@";;
    "-comp")
        compile;;
    "-build")
        build;;
    "-clean")
        clean;;
    "-install")
        install;;
    *)
        fail;;
esac
