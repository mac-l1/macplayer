#ifndef __MACFFPLAY_H
#define __MACFFPLAY_H

extern "C" {

int macffplay(int argc, char **argv, int scrWidth, int scrHeight);
void macffplay_quit(void);
void macffplay_key(unsigned char key);

}

#endif
