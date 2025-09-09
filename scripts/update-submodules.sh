#!/usr/bin/env bash
# Copyright (c) Erynn Scholtes
# SPDX-License-Identifier: MIT

git submodule update --init --remote --rebase --recursive

tput setaf 33
printf " ------------------------- REFRESHING FPXLIBC ------------------------- \n"
tput setaf sgr0

cd modules/fpxlibc
make clean
cd -
