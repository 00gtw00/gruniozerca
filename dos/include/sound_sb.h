/* sound_sb.h — backend Sound Blaster */
#ifndef SOUND_SB_H
#define SOUND_SB_H
#include "sound.h"
int  sb_init(const SoundConfig *cfg);
void sb_shutdown(void);
void sb_play_music(MusicTrack track);
void sb_stop_music(void);
void sb_play_sfx(SfxId sfx);
void sb_update(void);
#endif
