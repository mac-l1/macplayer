#!/bin/bash

VERSION=v1.0
rm -rf $VERSION
mkdir -p $VERSION
cp /usr/local/sbin/macplay* .
cp /usr/local/sbin/set*.sh .
cp /usr/local/bin/macplayer .
cp /usr/lib/cgi-bin/macplay.cgi .
cp * $VERSION
