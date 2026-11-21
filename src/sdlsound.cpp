#if defined(SDL_SOUND)

#include "sdlsound.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <cmath>
#include <vector>

#include <SDL3_mixer/SDL_mixer.h>

#include "cached_options.h"
#include "cata_path.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "init.h"
#include "messages.h"
#include "music.h"
#include "options.h"
#include "path_info.h"
#include "rng.h"
#include "sdl_wrappers.h"
#include "sounds.h"
#include "units.h"
#include "avatar.h"
#include "game.h"

#define dbg(x) DebugLog((x),D_SDL) << __FILE__ << ":" << __LINE__ << ": "

struct sfx_args {
    std::string id;
    std::string variant;
    std::string season;
    std::optional<bool> indoors;
    std::optional<bool> night;

    bool operator<( const sfx_args &rhs ) const {
        int r_ind = rhs.indoors.value_or( -1 );
        int r_nit = rhs.night.value_or( -1 );
        int l_ind = indoors.value_or( -1 );
        int l_nit = night.value_or( -1 );
        return std::tie( id, variant, season, l_ind, l_nit ) <
               std::tie( rhs.id, rhs.variant, rhs.season, r_ind, r_nit );
    }
};
struct sound_effect_resource {
    std::string path;
    struct deleter {
        // Operator overloaded to leverage deletion API.
        void operator()( MIX_Audio *const c ) const {
            MIX_DestroyAudio( c );
        }
    };
    std::unique_ptr<MIX_Audio, deleter> chunk;
    bool load_attempted = false;
};

static int add_sfx_path( const std::string &path );

struct sound_effect {
    const int volume = 0;
    const int resource_id = 0;

    sound_effect() = default;
    sound_effect( int volume, const std::string &path )
        : volume( volume ), resource_id( add_sfx_path( path ) ) {}
};

// Sound effects are primarily keyed by id
// They support a variety of optional 'variations', such as:
// - arbitrary variant string
// - season
// - indoors/outdoors
// - nighttime/daytime
// Each of the variations is optional if unspecified. Certain lookup
// functions attempt to find a best matching sound effect and fall back
// to default values if a variant is not found. This can be modelled as
// a multi level lookup, in effect.
// Variants always fall back to their default value, never an opposing value.
// So if a nighttime sfx is requested, a daytime sfx cannot fulfill it.
namespace
{

enum class sfx_season : uint8_t {
    NONE = 0,
    SPRING,
    SUMMER,
    AUTUMN,
    WINTER,
    COUNT,
};

sfx_season season_from_string( const std::string &str )
{
    if( str.empty() ) {
        return sfx_season::NONE;
    }
    if( str == "spring" ) {
        return sfx_season::SPRING;
    }
    if( str == "summer" ) {
        return sfx_season::SUMMER;
    }
    if( str == "autumn" ) {
        return sfx_season::AUTUMN;
    }
    if( str == "winter" ) {
        return sfx_season::WINTER;
    }
    throw std::invalid_argument( std::string( "sfx specified unknown season " ) + str );
}

enum class sfx_in_or_out : uint8_t {
    EITHER = 0,
    OUTDOORS,
    INDOORS,
    COUNT,
};

// This is encoded as an optional bool in json, so we cheat a little and accept -1 for 'not set'
sfx_in_or_out in_or_out_from_int( int value )
{
    int adjusted = value + 1;
    if( adjusted >= static_cast<int>( sfx_in_or_out::COUNT ) || adjusted < 0 ) {
        throw std::invalid_argument( std::string( "sfx specified unknown inside/outside value " ) +
                                     std::to_string( value ) );
    }
    return static_cast<sfx_in_or_out>( adjusted );
}

enum class sfx_time_of_day : uint8_t {
    ANY = 0,
    DAYTIME,
    NIGHTTIME,
    COUNT,
};

// This is encoded as an optional bool in json, so we cheat a little and accept -1 for 'not set'
sfx_time_of_day tod_from_int( int value )
{
    int adjusted = value + 1;
    if( adjusted >= static_cast<int>( sfx_time_of_day::COUNT ) || adjusted < 0 ) {
        throw std::invalid_argument( std::string( "sfx specified unknown day/night value " ) +
                                     std::to_string( value ) );
    }
    return static_cast<sfx_time_of_day>( adjusted );
}

// Fun but ugly template time.
template<typename Map, typename Key>
const std::vector<sound_effect> *find_sfx( const Map &c, Key &&k )
{
    auto it = c.find( std::forward<Key>( k ) );
    if( it == c.end() ) {
        return nullptr;
    }
    return &it->second;
}

template<typename Map, typename Key1, typename Key2, typename ...Keys>
const std::vector<sound_effect> *find_sfx( const Map &c, Key1 &&k, Key2 &&k2, Keys &&...keys )
{
    auto it = c.find( std::forward<Key1>( k ) );
    if( it == c.end() ) {
        return nullptr;
    }
    return find_sfx( it->second, std::forward<Key2>( k2 ),
                     std::forward<Keys>( keys )... );
}

template<typename Map, typename Key, typename Default>
const std::vector<sound_effect> *find_closest_sfx( const Map &c, Key &&k, Default &&d )
{
    auto it = c.find( std::forward<Key>( k ) );
    if( it == c.end() ) {
        it = c.find( std::forward<Default>( d ) );
    }
    if( it == c.end() ) {
        return nullptr;
    }
    return &it->second;
}

template<typename Map, typename Key1, typename Default1, typename Key2, typename Default2, typename ...KDs>
const std::vector<sound_effect> *find_closest_sfx( const Map &c, Key1 &&k, Default1 &&d1, Key2 &&k2,
        Default2 &&d2, KDs &&...kds )
{
    auto it = c.find( std::forward<Key1>( k ) );
    if( it == c.end() ) {
        it = c.find( std::forward<Default1>( d1 ) );
    }
    if( it == c.end() ) {
        return nullptr;
    }
    return find_closest_sfx( it->second, std::forward<Key2>( k2 ),
                             std::forward<Default2>( d2 ),
                             std::forward<KDs>( kds )... );

}

template<typename Map, typename Key>
std::vector<sound_effect> &emplace_sfx( Map &c, Key &&k )
{
    return c[std::forward<Key>( k )];
}

template<typename Map, typename Key1, typename Key2, typename ...Keys>
std::vector<sound_effect> &emplace_sfx( Map &c, Key1 &&k, Key2 &&k2, Keys &&...keys )
{
    auto &nested_container = c[std::forward<Key1>( k )];
    return emplace_sfx( nested_container, std::forward<Key2>( k2 ),
                        std::forward<Keys>( keys )... );
}

int bool_or( const std::optional<bool> &opt, int defl )
{
    return opt.has_value() ? opt.value() : defl;
}

} // namespace

struct sfx_map {
        void clear() {
            effects.clear();
        }

        std::vector<sound_effect> &operator[]( const sfx_args &key ) {
            return emplace_sfx( effects, key.id, key.variant, season_from_string( key.season ),
                                in_or_out_from_int( bool_or( key.indoors, -1 ) ), tod_from_int( bool_or( key.night, -1 ) ) );
        }

        const std::vector<sound_effect> *find( const sfx_args &key ) const {
            return find_sfx( effects, key.id, key.variant, season_from_string( key.season ),
                             in_or_out_from_int( bool_or( key.indoors, -1 ) ), tod_from_int( bool_or( key.night, -1 ) ) );
        }

        std::vector<sound_effect> *end() const {
            return nullptr;
        }

        const std::vector<sound_effect> *find( const std::string &id, const std::string &variant,
                                               const std::string &season, const std::optional<bool> &is_indoors,
                                               const std::optional<bool> &is_night ) const {
            return find_closest_sfx( effects, id, "", variant, "default", season_from_string( season ),
                                     sfx_season::NONE, in_or_out_from_int( bool_or( is_indoors, -1 ) ), sfx_in_or_out::EITHER,
                                     tod_from_int( bool_or( is_night, -1 ) ), sfx_time_of_day::ANY );
        }

    private:
        std::map<std::string, std::map<std::string, std::map<sfx_season, std::map<sfx_in_or_out, std::map<sfx_time_of_day, std::vector<sound_effect>>>>>>
        effects;

};

struct sfx_resources_t {
    std::vector<sound_effect_resource> resource;
    sfx_map sound_effects;
};

struct music_playlist {
    // list of filenames relative to the soundpack location
    struct entry {
        std::string file;
        int volume;
    };
    std::vector<entry> entries;
    bool shuffle;

    music_playlist() : shuffle( false ) {
    }
};
/** The music we're currently playing. */
static MIX_Audio *current_music = nullptr;
static int current_music_track_volume = 0;
static std::string current_playlist;
static size_t current_playlist_at = 0;
static size_t absolute_playlist_at = 0;
static std::vector<std::size_t> playlist_indexes;
bool sound_init_success = false;
static std::map<std::string, music_playlist> playlists;
static cata_path current_soundpack_path;

static std::unordered_map<std::string, int> unique_paths;
static sfx_resources_t sfx_resources;
static std::vector<sfx_args> sfx_preload;

bool sounds::sound_enabled = false;

static bool check_sound( const int volume = 1 )
{
    return sound_init_success && sounds::sound_enabled && volume > 0;
}

namespace
{
struct effect_track {
    MIX_Track *track = nullptr;
    Uint64 expires_at = 0;
    float speed = 1.f;
};

MIX_Mixer *mixer = nullptr;
MIX_Track *music_track = nullptr;
std::array<effect_track, 128> effects;
std::atomic<bool> music_finished{ false };
bool time_slowed = false;
constexpr float mixer_volume_scale = max_sound_channel_volume;

const char *group_tag( sfx::group group )
{
    switch( group ) {
        case sfx::group::time_of_day: return "time_of_day";
        case sfx::group::weather: return "weather";
        case sfx::group::context_themes: return "context_themes";
        case sfx::group::low_stamina: return "low_stamina";
        default: return nullptr;
    }
}

effect_track *channel_at( int channel )
{
    return channel >= 0 && static_cast<size_t>( channel ) < effects.size() ?
           &effects[channel] : nullptr;
}

void SDLCALL queue_next_music( void *, MIX_Track * )
{
    music_finished = true;
}

void apply_music_volume()
{
    if( music_track ) {
        MIX_SetTrackGain( music_track, current_music_track_volume *
                          get_option<int>( "MUSIC_VOLUME" ) / ( 100.f * mixer_volume_scale ) );
    }
}
} // namespace

bool init_sound()
{
    if( sound_init_success ) {
        return true;
    }
    if( !MIX_Init() ) {
        dbg( D_ERROR ) << "Failed to initialize audio mixer: " << SDL_GetError();
        return false;
    }
    const SDL_AudioSpec format{ SDL_AUDIO_S16, 2, 44100 };
    mixer = MIX_CreateMixerDevice( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format );
    if( mixer ) {
        music_track = MIX_CreateTrack( mixer );
        for( auto &effect : effects ) {
            effect.track = MIX_CreateTrack( mixer );
        }
    }
    if( !music_track || std::any_of( effects.begin(), effects.end(),
    []( const effect_track &effect ) { return !effect.track; } ) ) {
        dbg( D_ERROR ) << "Failed to open audio mixer: " << SDL_GetError();
        shutdown_sound();
        return false;
    }
    const auto tag_group = []( sfx::channel begin, sfx::channel end, sfx::group group ) {
        for( int i = static_cast<int>( begin ); i <= static_cast<int>( end ); ++i ) {
            MIX_TagTrack( effects[i].track, group_tag( group ) );
        }
    };
    tag_group( sfx::channel::daytime_outdoors_env, sfx::channel::nighttime_outdoors_env,
               sfx::group::time_of_day );
    tag_group( sfx::channel::underground_env, sfx::channel::outdoor_blizzard, sfx::group::weather );
    tag_group( sfx::channel::danger_extreme_theme, sfx::channel::danger_low_theme,
               sfx::group::context_themes );
    tag_group( sfx::channel::stamina_75, sfx::channel::stamina_35, sfx::group::low_stamina );
    for( const auto &effect : effects ) {
        MIX_TagTrack( effect.track, "effects" );
    }
    sound_init_success = true;
    return true;
}

void shutdown_sound()
{
    stop_music();
    if( mixer ) {
        MIX_DestroyMixer( mixer );
    }
    effects = {};
    mixer = nullptr;
    music_track = nullptr;
    sfx_resources.resource.clear();
    sfx_resources.sound_effects.clear();
    playlists.clear();
    MIX_Quit();
    sound_init_success = false;
}

static void musicFinished();

static void play_music_file( const std::string &filename, int volume )
{
    if( test_mode ) {
        return;
    }

    if( !check_sound( volume ) ) {
        return;
    }

    const std::string path = ( current_soundpack_path / filename ).get_unrelative_path().u8string();
    current_music = MIX_LoadAudio( mixer, path.c_str(), false );
    if( !current_music ) {
        dbg( D_ERROR ) << "Failed to load audio file " << path << ": " << SDL_GetError();
        return;
    }
    current_music_track_volume = volume;
    apply_music_volume();
    MIX_SetTrackStoppedCallback( music_track, queue_next_music, nullptr );
    if( !MIX_SetTrackAudio( music_track, current_music ) || !MIX_PlayTrack( music_track, 0 ) ) {
        dbg( D_ERROR ) << "Starting playlist " << path << " failed: " << SDL_GetError();
        MIX_DestroyAudio( current_music );
        current_music = nullptr;
    }
}

/** Callback called when we finish playing music. */
void musicFinished()
{
    if( test_mode ) {
        return;
    }

    MIX_DestroyAudio( current_music );
    current_music = nullptr;

    std::string new_playlist = music::get_music_id_string();

    if( current_playlist != new_playlist ) {
        play_music( new_playlist );
        return;
    }

    const auto iter = playlists.find( current_playlist );
    if( iter == playlists.end() ) {
        return;
    }
    const music_playlist &list = iter->second;
    if( list.entries.empty() ) {
        return;
    }

    // Load the next file to play.
    absolute_playlist_at++;

    // Wrap around if we reached the end of the playlist.
    if( absolute_playlist_at >= list.entries.size() ) {
        absolute_playlist_at = 0;
    }

    current_playlist_at = playlist_indexes.at( absolute_playlist_at );

    const music_playlist::entry &next = list.entries[current_playlist_at];
    play_music_file( next.file, next.volume );
}

void play_music( const std::string &playlist )
{
    // Don't interrupt playlist that's already playing.
    if( playlist == current_playlist ) {
        return;
    } else {
        stop_music();
    }

    const auto iter = playlists.find( playlist );
    if( iter == playlists.end() ) {
        return;
    }
    const music_playlist &list = iter->second;
    if( list.entries.empty() ) {
        return;
    }

    for( size_t i = 0; i < list.entries.size(); i++ ) {
        playlist_indexes.push_back( i );
    }
    if( list.shuffle ) {
        // Son't need to worry about the determinism check here because it only
        // affects audio, not game logic.
        // NOLINTNEXTLINE(cata-determinism)
        static cata_default_random_engine eng = cata_default_random_engine(
                std::chrono::steady_clock::now().time_since_epoch().count() );
        std::shuffle( playlist_indexes.begin(), playlist_indexes.end(), eng );
    }

    current_playlist = playlist;
    current_playlist_at = playlist_indexes.at( absolute_playlist_at );

    const music_playlist::entry &next = list.entries[current_playlist_at];
    current_music_track_volume = next.volume;
    play_music_file( next.file, next.volume );
}

void stop_music()
{
    if( test_mode ) {
        return;
    }

    if( music_track ) {
        MIX_SetTrackStoppedCallback( music_track, nullptr, nullptr );
        MIX_StopTrack( music_track, 0 );
        MIX_SetTrackAudio( music_track, nullptr );
    }
    music_finished = false;
    MIX_DestroyAudio( current_music );
    current_music = nullptr;

    playlist_indexes.clear();
    current_playlist.clear();
    current_playlist_at = 0;
    absolute_playlist_at = 0;
}

void update_music_volume()
{
    if( test_mode ) {
        return;
    }

    apply_music_volume();

    bool sound_enabled_old = sounds::sound_enabled;
    sounds::sound_enabled = ::get_option<bool>( "SOUND_ENABLED" );

    if( !sounds::sound_enabled ) {
        stop_music();
        music::deactivate_music_id_all();
        return;
    } else if( !sound_enabled_old ) {
        play_music( music::get_music_id_string() );
    }
}

static MIX_Audio *get_sfx_resource( int resource_id )
{
    sound_effect_resource &resource = sfx_resources.resource[resource_id];
    if( !resource.load_attempted ) {
        const cata_path path = current_soundpack_path / resource.path;
        resource.chunk.reset( MIX_LoadAudio( mixer, path.generic_u8string().c_str(), true ) );
        resource.load_attempted = true;
        if( !resource.chunk ) {
            dbg( D_WARNING ) << "Failed to load sfx audio file " << path.generic_u8string() <<
                             ": " << SDL_GetError();
        }
    }
    return resource.chunk.get();
}

static int add_sfx_path( const std::string &path )
{
    auto find_result = unique_paths.find( path );
    if( find_result != unique_paths.end() ) {
        return find_result->second;
    } else {
        int result = sfx_resources.resource.size();
        sound_effect_resource new_resource;
        new_resource.path = path;
        new_resource.chunk.reset();
        sfx_resources.resource.push_back( std::move( new_resource ) );
        unique_paths[ path ] = result;
        return result;
    }
}

void sfx::load_sound_effects( const JsonObject &jsobj )
{
    if( !sound_init_success ) {
        return;
    }
    sfx_args key = {
        jsobj.get_string( "id" ),
        "", // actual variant string is filled in the variant loop
        jsobj.get_string( "season", "" ),
        std::nullopt,
        std::nullopt,
    };
    if( jsobj.has_bool( "is_indoors" ) ) {
        key.indoors = jsobj.get_bool( "is_indoors" );
    }
    if( jsobj.has_bool( "is_night" ) ) {
        key.night = jsobj.get_bool( "is_night" );
    }
    const int volume = jsobj.get_int( "volume", 100 );
    std::vector<std::string> variants;
    if( jsobj.has_array( "variant" ) ) {
        variants = jsobj.get_string_array( "variant" );
    } else if( jsobj.has_string( "variant" ) ) {
        variants = { jsobj.get_string( "variant" ) };
    } else {
        variants = { "default" };
    }
    for( const std::string &variant : variants ) {
        key.variant = variant;
        std::vector<sound_effect> &effects = sfx_resources.sound_effects[key];
        for( const std::string file : jsobj.get_array( "files" ) ) {
            effects.emplace_back( volume, file );
        }
    }
}

void sfx::load_sound_effect_preload( const JsonObject &jsobj )
{
    if( !sound_init_success ) {
        return;
    }

    for( JsonObject aobj : jsobj.get_array( "preload" ) ) {
        sfx_args preload_key = {
            aobj.get_string( "id" ),
            "", // actual variant string is filled in the variant loop
            aobj.get_string( "season", "" ),
            std::nullopt,
            std::nullopt,
        };
        if( aobj.has_bool( "is_indoors" ) ) {
            preload_key.indoors = aobj.get_bool( "is_indoors" );
        }
        if( aobj.has_bool( "is_night" ) ) {
            preload_key.night = aobj.get_bool( "is_night" );
        }
        std::vector<std::string> variants;
        if( aobj.has_array( "variant" ) ) {
            variants = aobj.get_string_array( "variant" );
        } else if( aobj.has_string( "variant" ) ) {
            variants = { aobj.get_string( "variant" ) };
        } else {
            variants = { "default" };
        }
        for( const std::string &variant : variants ) {
            preload_key.variant = variant;
            sfx_preload.push_back( preload_key );
        }
    }
}

void sfx::load_playlist( const JsonObject &jsobj )
{
    if( !sound_init_success ) {
        return;
    }

    for( JsonObject playlist : jsobj.get_array( "playlists" ) ) {
        const std::string playlist_id = playlist.get_string( "id" );
        music_playlist playlist_to_load;
        playlist_to_load.shuffle = playlist.get_bool( "shuffle", false );

        for( JsonObject entry : playlist.get_array( "files" ) ) {
            const music_playlist::entry e{ entry.get_string( "file" ),  entry.get_int( "volume" ) };
            playlist_to_load.entries.push_back( e );
        }

        playlists[playlist_id] = std::move( playlist_to_load );

        music::update_music_id_is_empty_flag( playlist_id, true );
    }
}

// Returns a random sound effect matching given id and variant, but with fallback to "default" variants.
// May still return `nullptr`
static const sound_effect *find_random_effect( const std::string &id, const std::string &variant,
        const std::string &season, const std::optional<bool> &is_indoors,
        const std::optional<bool> &is_night )
{
    const std::vector<sound_effect> *iter = sfx_resources.sound_effects.find( id, variant, season,
                                            is_indoors,
                                            is_night );
    if( !iter ) {
        return nullptr;
    }

    return &random_entry_ref( *iter );
}

bool sfx::has_variant_sound( const std::string &id, const std::string &variant,
                             const std::string &season, const std::optional<bool> &is_indoors,
                             const std::optional<bool> &is_night )
{
    return find_random_effect( id, variant, season, is_indoors, is_night ) != nullptr;
}

static bool is_time_slowed()
{
    if( g == nullptr || g->uquit != QUIT_NO ) {
        return false;
    }
    // if the player have significantly more moves than their speed, they probably used an artifact/CBM to slow time.
    // I checked; the only things that increase a player's # of moves is spells/cbms that slow down time (and also unit tests) so this should work.
    // Would get_speed_base() be better?
    return std::max( get_avatar().get_speed(), 100 ) * 2 < get_avatar().get_moves();
}

void service_audio()
{
    if( !sound_init_success ) {
        return;
    }
    if( music_finished.exchange( false ) ) {
        musicFinished();
    }
    const bool slowed = is_time_slowed();
    const Uint64 now = SDL_GetTicks();
    for( auto &effect : effects ) {
        if( effect.expires_at && now >= effect.expires_at ) {
            MIX_StopTrack( effect.track, 0 );
            effect.expires_at = 0;
        }
        if( slowed != time_slowed && MIX_TrackPlaying( effect.track ) ) {
            MIX_SetTrackFrequencyRatio( effect.track, effect.speed * ( slowed ? .25f : 1.f ) );
        }
    }
    time_slowed = slowed;
}

bool sound_channel_playing( int channel )
{
    if( !sound_init_success ) {
        return false;
    }
    if( auto *effect = channel_at( channel ) ) {
        return MIX_TrackPlaying( effect->track );
    }
    return std::any_of( effects.begin(), effects.end(),
    []( const effect_track &effect ) { return MIX_TrackPlaying( effect.track ); } );
}

void fade_sound_group( int group, int milliseconds )
{
    if( mixer ) {
        if( const char *tag = group_tag( static_cast<sfx::group>( group ) ) ) {
            MIX_StopTag( mixer, tag, milliseconds );
        }
    }
}

void fade_sound_channel( int channel, int milliseconds )
{
    if( !sound_init_success ) {
        return;
    }
    if( auto *effect = channel_at( channel ) ) {
        const auto frames = milliseconds > 0 && MIX_TrackPlaying( effect->track ) ?
                            MIX_TrackMSToFrames( effect->track, milliseconds ) : 0;
        MIX_StopTrack( effect->track, std::max<Sint64>( 0, frames ) );
    } else if( channel == static_cast<int>( sfx::channel::any ) ) {
        MIX_StopTag( mixer, "effects", milliseconds );
    }
}

void expire_sound_channel( int channel, int milliseconds )
{
    if( auto *effect = channel_at( channel ); effect && effect->track ) {
        effect->expires_at = milliseconds >= 0 ? SDL_GetTicks() + milliseconds : 0;
    }
}

int sound_channel_volume( int channel, int volume )
{
    auto *effect = channel_at( channel );
    if( !effect || !effect->track ) {
        return -1;
    }
    const int previous = static_cast<int>( std::round( MIX_GetTrackGain( effect->track ) *
                         mixer_volume_scale ) );
    if( volume >= 0 && !MIX_SetTrackGain( effect->track,
                                         std::min( float( volume ) / mixer_volume_scale, 1.f ) ) ) {
        return -1;
    }
    return previous;
}

bool sound_channel_fading( int channel )
{
    const auto *effect = channel_at( channel );
    return effect && effect->track && MIX_GetTrackFadeFrames( effect->track ) != 0;
}

static void position_track( MIX_Track *track, std::optional<units::angle> angle )
{
    const float radians = angle ? to_radians( *angle ) : 0.f;
    const MIX_Point3D position{ angle ? std::sin( radians ) : 0.f, 0.f,
                                angle ? -std::cos( radians ) : 0.f };
    MIX_SetTrack3DPosition( track, &position );
}

void position_sound_channel( int channel, double angle_degrees )
{
    if( auto *effect = channel_at( channel ); effect && effect->track ) {
        position_track( effect->track, units::from_degrees( angle_degrees ) );
    }
}

static bool play_effect( int channel, MIX_Audio *audio, int loops, int volume,
                         double pitch, const sound_effect &effect,
                         std::optional<units::angle> angle, int fade_in_ms )
{
    if( !audio ) {
        return false; // A missing file was already logged by the loader.
    }
    effect_track *selected = channel_at( channel );
    if( !selected && channel == static_cast<int>( sfx::channel::any ) ) {
        const auto found = std::find_if( effects.begin() + static_cast<int>( sfx::channel::MAX_CHANNEL ),
                                        effects.end(), []( const effect_track &entry ) {
            return !MIX_TrackPlaying( entry.track );
        } );
        if( found != effects.end() ) {
            selected = &*found;
        }
    }
    if( !selected ) {
        return true;
    }
    auto *track = selected->track;
    selected->expires_at = 0;
    // The old pitch factor stretched the sample count. SDL3 mixer specifies
    // the inverse, a playback frequency ratio.
    selected->speed = pitch > 0.0 ? 1.f / static_cast<float>( pitch ) : 1.f;
    MIX_StopTrack( track, 0 );
    if( !MIX_SetTrackAudio( track, audio ) ) {
        return true;
    }
    MIX_SetTrackFrequencyRatio( track, selected->speed * ( is_time_slowed() ? .25f : 1.f ) );
    const float gain = effect.volume * get_option<int>( "SOUND_EFFECT_VOLUME" ) * volume /
                       ( 100.f * 100.f * mixer_volume_scale );
    MIX_SetTrackGain( track, std::clamp( gain, 0.f, 1.f ) );
    position_track( track, angle );
    const auto properties = SDL_CreateProperties();
    SDL_SetNumberProperty( properties, MIX_PROP_PLAY_LOOPS_NUMBER, loops );
    SDL_SetNumberProperty( properties, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, fade_in_ms );
    const bool playing = MIX_PlayTrack( track, properties );
    SDL_DestroyProperties( properties );
    return !playing;
}

void sfx::play_variant_sound( const std::string &id, const std::string &variant,
                              const std::string &season, const std::optional<bool> &is_indoors,
                              const std::optional<bool> &is_night, int volume )
{
    if( test_mode ) {
        return;
    }

    add_msg_debug( debugmode::DF_SOUND, "sound id: %s, variant: %s, volume: %d ", id, variant, volume );

    if( !check_sound( volume ) ) {
        return;
    }
    const sound_effect *eff = find_random_effect( id, variant, season, is_indoors, is_night );
    if( eff == nullptr ) {
        eff = find_random_effect( id, "default", "", std::optional<bool>(), std::optional<bool>() );
        if( eff == nullptr ) {
            return;
        }
    }
    const sound_effect &selected_sound_effect = *eff;

    MIX_Audio *effect_to_play = get_sfx_resource( selected_sound_effect.resource_id );

    bool error = play_effect( static_cast<int>( channel::any ), effect_to_play, 0,
                 volume, 0, selected_sound_effect, std::nullopt, 0 );
    if( error ) {
        dbg( D_ERROR ) << "Failed to play sound effect: " << SDL_GetError() << " id:" << id
                       << " variant:" << variant << " season:" << season;
    }
}

void sfx::play_variant_sound( const std::string &id, const std::string &variant,
                              const std::string &season, const std::optional<bool> &is_indoors,
                              const std::optional<bool> &is_night, int volume, units::angle angle,
                              double pitch_min, double pitch_max )
{
    if( test_mode ) {
        return;
    }

    add_msg_debug( debugmode::DF_SOUND, "sound id: %s, variant: %s, volume: %d ", id, variant, volume );

    if( !check_sound( volume ) ) {
        return;
    }
    const sound_effect *eff = find_random_effect( id, variant, season, is_indoors, is_night );
    if( eff == nullptr ) {
        return;
    }
    const sound_effect &selected_sound_effect = *eff;

    MIX_Audio *effect_to_play = get_sfx_resource( selected_sound_effect.resource_id );
    const double pitch = pitch_min > 0 && pitch_max > 0 ? rng_float( pitch_min, pitch_max ) : 0;
    const bool failed = play_effect( static_cast<int>( channel::any ), effect_to_play, 0,
                                     volume, pitch, selected_sound_effect, angle, 0 );
    if( failed ) {
        dbg( D_ERROR ) << "Failed to play sound effect: " << SDL_GetError() << " id:" << id
                       << " variant:" << variant << " season:" << season;
    }
}

void sfx::play_ambient_variant_sound( const std::string &id, const std::string &variant,
                                      const std::string &season, const std::optional<bool> &is_indoors,
                                      const std::optional<bool> &is_night, int volume,
                                      channel channel, int fade_in_duration, double pitch, int loops )
{
    if( test_mode ) {
        return;
    }
    if( !check_sound( volume ) ) {
        return;
    }
    if( is_channel_playing( channel ) ) {
        return;
    }
    const sound_effect *eff = find_random_effect( id, variant, season, is_indoors, is_night );
    if( eff == nullptr ) {
        return;
    }
    const sound_effect &selected_sound_effect = *eff;

    MIX_Audio *effect_to_play = get_sfx_resource( selected_sound_effect.resource_id );

    volume = selected_sound_effect.volume * get_option<int>( "AMBIENT_SOUND_VOLUME" ) * volume /
             ( 100 * 100 );
    const bool failed = play_effect( static_cast<int>( channel ), effect_to_play, loops,
                                     volume, pitch, selected_sound_effect, std::nullopt, fade_in_duration );
    if( failed ) {
        dbg( D_ERROR ) << "Failed to play sound effect: " << SDL_GetError() << " id:" << id
                       << " variant:" << variant << " season:" << season;
    }
}

void load_soundset()
{
    const cata_path default_path = PATH_INFO::defaultsounddir();
    const std::string default_soundpack = "basic";
    std::string current_soundpack = get_option<std::string>( "SOUNDPACKS" );
    cata_path soundpack_path;

    // Get current soundpack and it's directory path.
    if( current_soundpack.empty() ) {
        dbg( D_ERROR ) << "Soundpack not set in options or empty.";
        soundpack_path = default_path;
        current_soundpack = default_soundpack;
    } else {
        dbg( D_INFO ) << "Current soundpack is: " << current_soundpack;
        soundpack_path = SOUNDPACKS[current_soundpack];
    }

    if( soundpack_path.empty() ) {
        dbg( D_ERROR ) << "Soundpack with name " << current_soundpack << " can't be found or empty string";
        soundpack_path = default_path;
        current_soundpack = default_soundpack;
    } else {
        dbg( D_INFO ) << '"' << current_soundpack << '"' << " soundpack: found path: " << soundpack_path;
    }

    current_soundpack_path = soundpack_path;
    try {
        DynamicDataLoader::get_instance().load_data_from_path( soundpack_path, "core" );
    } catch( const std::exception &err ) {
        debugmsg( "failed to load sounds: %s", err.what() );
    }

    // Preload sound effects
    for( const sfx_args &preload : sfx_preload ) {
        const std::vector<sound_effect> *find_result = sfx_resources.sound_effects.find( preload );
        if( find_result != sfx_resources.sound_effects.end() ) {
            for( const sound_effect &sfx : *find_result ) {
                get_sfx_resource( sfx.resource_id );
            }
        }
    }

    // Memory of unique_paths no longer required, swap with locally scoped unordered_map
    // to force deallocation of resources.
    {
        unique_paths.clear();
        std::unordered_map<std::string, int> t_swap;
        unique_paths.swap( t_swap );
    }
    // Memory of sfx_preload no longer required, swap with locally scoped vector
    // to force deallocation of resources.
    {
        sfx_preload.clear();
        std::vector<sfx_args> t_swap;
        sfx_preload.swap( t_swap );
    }
}

// capitalized to mirror cata_tiles::InitSDL()
void initSDLAudioOnly()
{
    throwErrorIf( !SDL_Init( SDL_INIT_AUDIO ), "SDL_Init failed" );
    if( atexit( SDL_Quit ) ) {
        debugmsg( "atexit failed to register SDL_Quit" );
    }
}

#endif
