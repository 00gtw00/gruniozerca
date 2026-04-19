/* sound_opl.h — backend OPL2/AdLib */
#ifndef SOUND_OPL_H
#define SOUND_OPL_H
#include "sound.h"
int  opl_init(const SoundConfig *cfg);
void opl_shutdown(void);
void opl_play_music(MusicTrack track);
void opl_stop_music(void);
void opl_play_sfx(SfxId sfx);
void opl_update(void);
/* Zapis rejestru OPL2: addr → $388, data → $389 */
void opl_write(uint8_t reg, uint8_t val);
#endif
