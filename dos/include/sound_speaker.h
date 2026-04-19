/* sound_speaker.h — backend PC Speaker */
#ifndef SOUND_SPEAKER_H
#define SOUND_SPEAKER_H
#include "sound.h"
int  spk_init(const SoundConfig *cfg);
void spk_shutdown(void);
void spk_play_music(MusicTrack track);
void spk_stop_music(void);
void spk_play_sfx(SfxId sfx);
void spk_update(void);
/* Włącz/wyłącz głośnik z częstotliwością (Hz) */
void spk_tone(uint16_t freq_hz);
void spk_off(void);
#endif
