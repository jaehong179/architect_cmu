#ifndef LINUXAUDIO_H
#define LINUXAUDIO_H
void LinuxSetSoundParameters(const char *card_name,const char *mic_name,const char *agc_name,int volume_percent);
void LinuxListSoundCardsAndElements(void);
#endif // LINUXAUDIO_H
