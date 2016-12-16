#!/bin/bash
set -x

# set 1080p as new resolution mode 
sudo su -p -c 'export NEW_MODE=1920x1080p-60; find /etc -name "*.sh"|xargs grep "/sys/devices/virtual/display/display0.HDMI/mode"|cut -f1 -d":"|xargs sed -i -e "s/.*\>.*mode/        echo \"${NEW_MODE}\" \>  \/sys\/devices\/virtual\/display\/display0\.HDMI\/mode/g"; echo "${NEW_MODE}" >  /sys/devices/virtual/display/display0.HDMI/mode '
sudo su -p -c 'find /etc -name "*.sh"|xargs grep "/sys/devices/virtual/display/display0.HDMI/mode" 2>/dev/null'
