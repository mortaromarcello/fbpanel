#!/usr/bin/env bash
LOCALE="it_IT.UTF-8"
VERSION=$(echo | cat ./version)

if [ $1 ]; then
    LOCALE=$1
fi
./create_po.sh --update --package=fbpanel --version=${VERSION} --locale=${LOCALE}
