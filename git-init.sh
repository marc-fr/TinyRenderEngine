#!/bin/bash

# The purpose of the script is to tune a little bit GIT so you can work efficiency on the repository.

# This project uses GIT LFS:
git lfs install

# This project uses GIT submodules:
git submodule init
git submodule update
# git submodule update --remote

git config diff.submodule log

