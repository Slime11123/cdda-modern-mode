#pragma once
#ifndef CATA_SRC_SDLSOUND_H
#define CATA_SRC_SDLSOUND_H

#include <string>
#if defined(SDL_SOUND)

/**
 * Attempt to initialize an audio device.  Returns false if initialization fails.
 */
bool init_sound();
void initSDLAudioOnly();
void shutdown_sound();
void play_music( const std::string &playlist );
void stop_music();
void update_music_volume();
void load_soundset();
void service_audio();
inline constexpr int max_sound_channel_volume = 128;
bool sound_channel_playing( int channel );
bool sound_channel_fading( int channel );
int sound_channel_volume( int channel, int volume );
void fade_sound_group( int group, int milliseconds );
void fade_sound_channel( int channel, int milliseconds );
void expire_sound_channel( int channel, int milliseconds );
void position_sound_channel( int channel, double angle_degrees );
extern bool sound_init_success;

#else

inline bool init_sound()
{
    return false;
}
inline void shutdown_sound() { }
inline void play_music( const std::string &/*playlist*/ )
{
}
inline void update_music_volume() { }
inline void load_soundset() { }
inline void service_audio() { }

#endif

#endif // CATA_SRC_SDLSOUND_H
