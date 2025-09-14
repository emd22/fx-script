#!/bin/sh

clang Out.asm -o Out
./Out
echo $?
