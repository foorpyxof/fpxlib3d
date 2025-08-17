#!/bin/bash

if [[ "$#" -eq 0 ]]; then
  echo "requires platform targets as arguments. no arguments were found."
  echo
  echo "Example invocation: \`$0 linux windows\`"
  echo
  echo " - aborting"
  exit
fi

for i in "$@"; do

  TARGET=$(echo -n "$i" | tr "[:lower:]" "[:upper:]")

  make clean >/dev/null 2>&1

  tput setaf 33
  printf "\nBuilding for target %s\n" $i
  tput setaf sgr0

  make test ${TARGET}=true
  if [[ $? -ne 0 ]]; then break $?; fi

  tput setaf 33
  printf "\nPacking for target %s\n" $i
  tput setaf sgr0
  make archive ${TARGET}=true

done
