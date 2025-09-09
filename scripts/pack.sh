#!/usr/bin/env bash
# Copyright (c) Erynn Scholtes
# SPDX-License-Identifier: MIT

if [[ "$#" -eq 0 ]]; then
  echo "requires platform targets as arguments. no arguments were found."
  echo
  echo "Example invocation: \`$0 linux windows\`"
  echo
  echo " - aborting"
  exit
fi

MAKE_CMD="make -j$(nproc)"

for i in "$@"; do

  TARGET=$(echo -n "$i" | tr "[:lower:]" "[:upper:]")

  $MAKE_CMD clean >/dev/null 2>&1

  tput setaf 33
  printf "\nBuilding for target %s\n" $i
  tput setaf sgr0

  $MAKE_CMD test ${TARGET}=true VERBOSE=1
  RES=$?
  if [[ $RES -ne 0 ]]; then
    break
  fi

  tput setaf 33
  printf "\nPacking for target %s\n" $i
  tput setaf sgr0
  $MAKE_CMD archive ${TARGET}=true

done
