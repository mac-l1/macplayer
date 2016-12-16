#!/bin/bash
set -x

# set surfaceflinger as eglplatform 
sudo su -p -c 'export NEW_EGLPLATFORM=surfaceflinger; find /etc|xargs grep EGL_PLATFORM 2>/dev/null|cut -f1 -d":"|xargs sed -i -e "s/EGL_PLATFORM=.*/EGL_PLATFORM=${NEW_EGLPLATFORM}/"'
sudo su -c 'find /etc|xargs grep EGL_PLATFORM 2>/dev/null'

sudo su -p -c 'export NEW_EGLPLATFORM=surfaceflinger; sed -i -e "s/EGL_PLATFORM_DEFAULT=.*/EGL_PLATFORM_DEFAULT=${NEW_EGLPLATFORM}/" /usr/local/share/libhybris/libhybris.sh'
sudo su -c 'grep "EGL_PLATFORM_DEFAULT=" /usr/local/share/libhybris/libhybris.sh 2>/dev/null'

chmod a+x *
sudo cp logmacplayer macplay macplay_socket sendkeymacplayer /usr/local/sbin/
sudo cp macplayer /usr/local/bin/
sudo cp set*.sh /usr/local/sbin/
sudo cp macplay.cgi /usr/lib/cgi-bin/
