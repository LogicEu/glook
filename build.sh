#!/bin/bash

comp=gcc
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
    mkdir lib/
    pushd glee/
    ./build.sh -s
    popd
    mv glee/libglee.a lib/libglee.a
}

clean() {
    rm -r lib/
}

compile() {
    if echo "$OSTYPE" | grep -q "linux"; then
        $comp *.c -o $exe ${std[*]} ${inc[*]} ${lib[*]} ${linux[*]} 
    elif echo "$OSTYPE" | grep -q "darwin"; then 
        $comp *.c -o $exe ${std[*]} ${inc[*]} ${lib[*]} ${mac[*]}
    else
        echo "OS is not supported yet..."
        exit
    fi
}

install() {
    sudo mv $exe /usr/local/bin/$exe
}

run() {
    compile
    ./$exe "$@"
}

fail() {
    echo "Use with '-build' to build dependencies, '-comp' to compile the tool"
    exit
}

if [[ $# < 1 ]]; then
    fail
elif [[ "$1" == "-run" ]]; then
    shift
    run "$@"
elif [[ "$1" == "-comp" ]]; then
    compile
elif [[ "$1" == "-build" ]]; then
    build
elif [[ "$1" == "-clean" ]]; then
    clean
elif [[ "$1" == "-install" ]]; then
    install
else
    fail
fi
