#ifndef WINDOWSAUDIO_H
#define WINDOWSAUDIO_H
void WindowsSetSoundParameters(const char *enpoint_name,const char *mic_name,int volume_percent);
void WindowsListSoundCardsAndElements(void);
#endif // WINDOWSAUDIO_H
