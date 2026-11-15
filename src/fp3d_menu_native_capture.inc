// Isolated native CDDA main-menu scene transport.
//
// The child process owns its own --userdir/menu-demo-sandbox, worlds and AI.
// We only crop the original SDL terrain renderer into BMP frames. No player
// save, HUD, handcrafted vehicles, stitched tiles, or user-world recording.
#include <cstring>
#include <fstream>

#include "fp3d_menu_startup.h"

// Defined at the bottom of this file; declared here so the worker launch path
// can scrub a previous session's frames before it starts a new one.
void menu_clear_worker_frames( const std::string &directory );
void set_native_menu_canvas( int width, int height );

namespace {
// Store full-detail, aspect-preserving map frames as PNG.  The original
// 720x405 BMP path shrank tiles before enlarging them again.
constexpr Uint32 menu_frame_interval = 25;
// Hard ceiling for one streamed frame. Beyond this the PNG cost outweighs
// any visible gain on a title screen backdrop.
constexpr int menu_canvas_max_width = 1920;
constexpr int menu_canvas_max_height = 1080;
// The startup gate only trusts a frame that was fully copied *and* uploaded for
// the session that is currently running (see fp3d_menu_startup.h).
fp3d_menu::readiness menu_readiness;
Uint32 menu_last_poll_tick = 0;
bool menu_worker_started = false;
#if defined(_WIN32)
HANDLE menu_worker_process = nullptr;
HANDLE menu_worker_job = nullptr;
#endif
bool menu_is_worker = false;
bool menu_demo_worker_mode = false;
// Night scenes keep the original lighting so lamps and shadows show; other scenes
// are forced fully lit (see lightmap.cpp).
bool menu_demo_night_mode = false;
std::function<void()> menu_frame_hook;
bool menu_demo_terrain_updated = false;
std::string menu_worker_output;
size_t menu_worker_frame_id = 0;
// Pixel canvas requested by the title screen. The worker enlarges its terrain
// viewport past this and crops back to it, so a streamed frame is 1:1 with the
// pixels the title screen will finally show (no double resampling).
int menu_canvas_width = 0;
int menu_canvas_height = 0;
SDL_Rect menu_terrain_rect = { 0, 0, 0, 0 };
bool menu_terrain_drawn = false;
Uint32 menu_last_capture = 0;
Uint32 menu_last_play = 0;
// Liveness of the frame channel: when the worker stops publishing (crashed or
// blocked inside a modal prompt) the title must recover instead of freezing.
Uint32 menu_worker_started_tick = 0;
Uint32 menu_last_progress = 0;
// Fractional (sub-square) camera offset published by the worker, in map squares.
// It is converted to pixels at capture time so the pan is smooth.
float menu_canvas_pan_x = 0.0f;
float menu_capture_pan_x = 0.0f;
float menu_capture_pan_y = 0.0f;
float menu_canvas_pan_y = 0.0f;
// ---- shared-memory frame channel -------------------------------------------------
// PNG encoding cost ~150ms per 1080p frame, which capped the demonstration at ~5
// frames per second and made the camera look like it stepped. Both processes are
// ours, so the pixels travel through a named shared section instead: no encoder,
// no disk, ~3ms per frame.
#if defined(_WIN32)
HANDLE menu_shm_handle = nullptr;
unsigned char *menu_shm_base = nullptr;
size_t menu_shm_bytes = 0;
size_t menu_shm_frame_bytes = 0;
int menu_shm_width = 0;
int menu_shm_height = 0;
// Reader side.
HANDLE menu_shm_read_handle = nullptr;
const unsigned char *menu_shm_read_base = nullptr;
size_t menu_shm_read_bytes = 0;
std::string menu_shm_name;
long menu_shm_last_seq = 0;
// Last sequence number seen by the cheap watchdog probe (no texture upload).
long menu_shm_peek_last = 0;
// Read the producer's sequence number without uploading a texture. Used by the
// watchdog so "worker is alive and publishing" is not mistaken for a stall while
// the title screen is not being drawn (settings, world setup, character creation).
long menu_shm_peek_seq()
{
#if defined(_WIN32)
    if( menu_shm_read_base == nullptr ) {
        return 0;
    }
    return static_cast<long>(
               *reinterpret_cast<const volatile LONG *>( menu_shm_read_base ) );
#else
    return 0;
#endif
}
// Front (last validated) texture plus the candidate it is replaced with. A
// frame is copied into the candidate and only becomes visible once the copy
// finished without the producer writing again, so a torn frame can never
// replace a good one.
SDL_Texture_Ptr menu_shm_texture;
SDL_Texture_Ptr menu_shm_staging;
bool menu_shm_has_frame = false;
int menu_shm_tex_w = 0;
int menu_shm_tex_h = 0;
#endif
constexpr unsigned menu_shm_header_bytes = 64;
#if defined(_WIN32)
// Defined further down; the launch/stop paths detach before the reader does.
void menu_detach_shared_frame();
#endif
size_t menu_play_index = 0;
SDL_Texture_Ptr menu_play_texture;
std::filesystem::path menu_frames_path()
{
    return std::filesystem::path( PATH_INFO::config_dir() ) / "native_menu_live";
}
std::filesystem::path menu_frame_path( size_t n )
{
    return menu_frames_path() / ( "frame_" + std::to_string( n ) + ".png" );
}
template <typename F> void menu_on_target( SDL_Texture *target, const F &f )
{
    if( !renderer ) {
        return;
    }
    SDL_Texture *previous = SDL_GetRenderTarget( renderer.get() );
    SDL_Rect old_view = {}, old_clip = {};
    SDL_RenderGetViewport( renderer.get(), &old_view );
    SDL_RenderGetClipRect( renderer.get(), &old_clip );
    const bool clipping = SDL_RenderIsClipEnabled( renderer.get() ) == SDL_TRUE;
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale( renderer.get(), &sx, &sy );
    int lx = 0, ly = 0;
    SDL_RenderGetLogicalSize( renderer.get(), &lx, &ly );
    if( SDL_SetRenderTarget( renderer.get(), target ) != 0 ) {
        return;
    }
    SDL_RenderSetLogicalSize( renderer.get(), 0, 0 );
    SDL_RenderSetScale( renderer.get(), 1.0f, 1.0f );
    SDL_RenderSetViewport( renderer.get(), nullptr );
    SDL_RenderSetClipRect( renderer.get(), nullptr );
    f();
    SDL_SetRenderTarget( renderer.get(), previous );
    SDL_RenderSetLogicalSize( renderer.get(), lx, ly );
    SDL_RenderSetScale( renderer.get(), sx, sy );
    SDL_RenderSetViewport( renderer.get(), &old_view );
    SDL_RenderSetClipRect( renderer.get(), clipping ? &old_clip : nullptr );
}
#if defined(_WIN32)
// Writer side of the shared-memory frame channel. Returns false when shared
// memory is unavailable, in which case the caller falls back to PNG files.
bool menu_publish_shared_frame( SDL_Surface *surface, int width, int height )
{
    if( surface == nullptr || surface->pixels == nullptr || width <= 0 || height <= 0 ) {
        return false;
    }
    const size_t need = menu_shm_header_bytes + static_cast<size_t>( width ) * height * 4;
    if( menu_shm_base == nullptr || menu_shm_width != width || menu_shm_height != height ) {
        if( menu_shm_base != nullptr ) {
            UnmapViewOfFile( menu_shm_base );
            menu_shm_base = nullptr;
        }
        if( menu_shm_handle != nullptr ) {
            CloseHandle( menu_shm_handle );
            menu_shm_handle = nullptr;
        }
        const std::wstring name = L"Local\\cdda_menu_demo_frame_" +
                                  std::to_wstring( GetCurrentProcessId() );
        menu_shm_handle = CreateFileMappingW( INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                              static_cast<DWORD>( need >> 32 ),
                                              static_cast<DWORD>( need & 0xFFFFFFFFu ), name.c_str() );
        if( menu_shm_handle == nullptr ) {
            return false;
        }
        menu_shm_base = static_cast<unsigned char *>( MapViewOfFile( menu_shm_handle,
                        FILE_MAP_ALL_ACCESS, 0, 0, need ) );
        if( menu_shm_base == nullptr ) {
            CloseHandle( menu_shm_handle );
            menu_shm_handle = nullptr;
            return false;
        }
        menu_shm_bytes = need;
        menu_shm_width = width;
        menu_shm_height = height;
        // Tell the title screen where to attach (small text file, written once).
        std::error_code dir_ec;
        std::filesystem::create_directories( menu_worker_output, dir_ec );
        std::ofstream info( std::filesystem::path( menu_worker_output ) / "frame_shm.txt" );
        info << std::string( name.begin(), name.end() ) << '\n' << width << ' ' << height << '\n';
    }
    unsigned char *header = menu_shm_base;
    unsigned char *pixels = menu_shm_base + menu_shm_header_bytes;
    volatile LONG *seq = reinterpret_cast<volatile LONG *>( header );
    // Seqlock: odd while the pixels are being written, even when complete.
    InterlockedIncrement( seq );
    const size_t row_bytes = static_cast<size_t>( width ) * 4;
    const unsigned char *src = static_cast<const unsigned char *>( surface->pixels );
    if( static_cast<size_t>( surface->pitch ) == row_bytes ) {
        std::memcpy( pixels, src, row_bytes * static_cast<size_t>( height ) );
    } else {
        for( int y = 0; y < height; ++y ) {
            std::memcpy( pixels + row_bytes * static_cast<size_t>( y ),
                         src + static_cast<size_t>( surface->pitch ) * static_cast<size_t>( y ),
                         row_bytes );
        }
    }
    *reinterpret_cast<int *>( header + 4 ) = width;
    *reinterpret_cast<int *>( header + 8 ) = height;
    InterlockedIncrement( seq );
    return true;
}
#endif
void menu_record_worker_frame()
{
    // Advancing the camera before cropping keeps the backdrop alive even when the
    // caller is a vanilla animation loop that only redraws once per step.
    if( menu_frame_hook ) {
        menu_frame_hook();
    }
    if( !menu_is_worker || !menu_demo_terrain_updated || !display_buffer ) {
        return;
    }
    menu_demo_terrain_updated = false;
    Uint32 now = SDL_GetTicks();
    if( now - menu_last_capture < menu_frame_interval ) {
        return;
    }
    int dw = 0, dh = 0;
    SDL_QueryTexture( display_buffer.get(), nullptr, nullptr, &dw, &dh );
    SDL_Rect from = menu_terrain_rect;
    from.x = std::clamp( from.x, 0, dw );
    from.y = std::clamp( from.y, 0, dh );
    from.w = std::clamp( from.w, 0, dw - from.x );
    from.h = std::clamp( from.h, 0, dh - from.y );
    if( from.w < 100 || from.h < 100 ) {
        return;
    }
    // Pixel size of one map square, measured on the *uncropped* terrain viewport
    // (the crop below is smaller than the viewport, so it must not be used here).
    const int px_per_square_x = std::max( 1, from.w / std::max( 1, TERRAIN_WINDOW_WIDTH ) );
    const int px_per_square_y = std::max( 1, from.h / std::max( 1, TERRAIN_WINDOW_HEIGHT ) );
    // Take a centred window of exactly the size the title screen asked for, so
    // the streamed pixels land on screen 1:1. The worker's window was sized
    // larger on purpose, so this normally only trims sidebar / log margins and
    // never magnifies the tileset.
    const int want_width = menu_canvas_width > 0 ?
                           std::min( menu_canvas_width, menu_canvas_max_width ) :
                           std::min( from.w, 1600 );
    const int want_height = menu_canvas_height > 0 ?
                            std::min( menu_canvas_height, menu_canvas_max_height ) :
                            std::min( from.h, 1000 );
    if( from.w > want_width ) {
        from.x += ( from.w - want_width ) / 2;
        from.w = want_width;
    }
    if( from.h > want_height ) {
        from.y += ( from.h - want_height ) / 2;
        from.h = want_height;
    }
    // Sub-tile panning. The camera offset itself is whole map squares, which
    // would step one square at a time; the leftover fraction is applied here as
    // a pixel shift inside the (larger) captured viewport, so the background
    // glides instead of ticking. The margin was sized for exactly this.
    if( menu_capture_pan_x != 0.0f || menu_capture_pan_y != 0.0f ) {
        const int pan_x = static_cast<int>( std::lround( menu_capture_pan_x * px_per_square_x ) );
        const int pan_y = static_cast<int>( std::lround( menu_capture_pan_y * px_per_square_y ) );
        from.x = std::clamp( from.x + pan_x, 0, std::max( 0, dw - from.w ) );
        from.y = std::clamp( from.y + pan_y, 0, std::max( 0, dh - from.h ) );
    }
    const int frame_width = from.w;
    const int frame_height = from.h;
    if( frame_width < 100 || frame_height < 100 ) {
        return;
    }
    SDL_Texture_Ptr output( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_ARGB8888,
                           SDL_TEXTUREACCESS_TARGET, frame_width, frame_height ) );
    SDL_Surface_Ptr surface( SDL_CreateRGBSurfaceWithFormat( 0, frame_width,
                            frame_height, 32, SDL_PIXELFORMAT_ARGB8888 ) );
    if( !output || !surface ) {
        return;
    }
    SDL_SetTextureScaleMode( display_buffer.get(), SDL_ScaleModeNearest );
    bool success = false;
    menu_on_target( output.get(), [&]() {
        SDL_SetRenderDrawColor( renderer.get(), 10, 10, 10, 255 );
        SDL_RenderClear( renderer.get() );
        SDL_Rect dest = { 0, 0, frame_width, frame_height };
        if( SDL_RenderCopy( renderer.get(), display_buffer.get(), &from, &dest ) == 0 ) {
            success = SDL_RenderReadPixels( renderer.get(), nullptr,
                                           SDL_PIXELFORMAT_ARGB8888, surface->pixels,
                                           surface->pitch ) == 0;
        }
    } );
    if( !success ) {
        return;
    }
#if defined(_WIN32)
    // Publish through shared memory when possible: the PNG path below costs an
    // order of magnitude more and is only kept as a fallback.
    if( menu_publish_shared_frame( surface.get(), frame_width, frame_height ) ) {
        ++menu_worker_frame_id;
        menu_last_capture = now;
        return;
    }
#endif
    std::filesystem::path path( menu_worker_output );
    std::error_code ec;
    std::filesystem::create_directories( path, ec );
    if( ec ) {
        return;
    }
    std::filesystem::path tmp = path / ( "tmp_" + std::to_string( menu_worker_frame_id ) + ".png" );
    std::filesystem::path dst = path / ( "frame_" + std::to_string( menu_worker_frame_id ) + ".png" );
    if( IMG_SavePNG( surface.get(), tmp.string().c_str() ) != 0 ) {
        return;
    }
    std::filesystem::remove( dst, ec );
    std::filesystem::rename( tmp, dst, ec );
    if( !ec ) {
        ++menu_worker_frame_id;
        menu_last_capture = now;
    }
}
//! Starts one isolated worker. Returns true when a new frame session was opened,
//! i.e. when the title screen has to wait for a fresh first frame.
bool menu_start_isolated_worker()
{
#if defined(_WIN32)
    if( menu_worker_started ) {
        return false;
    }
    menu_worker_started = true;
    // Every launch is a new session: frames of the previous worker are invalid.
    menu_readiness.begin_session( fp3d_menu::now_ms() );
    std::filesystem::path private_userdir =
        std::filesystem::path( PATH_INFO::user_dir() ) / "menu-demo-sandbox";
    std::error_code ec;
    std::filesystem::create_directories( private_userdir, ec );
    if( ec ) {
        // The title screen must not wait for a process that was never created.
        dbg( D_ERROR ) << "Menu demo sandbox directory unavailable: " << ec.message();
        menu_readiness.mark_failed();
        menu_worker_started = false;
        return true;
    }
    wchar_t executable[MAX_PATH] = {};
    if( !GetModuleFileNameW( nullptr, executable, MAX_PATH ) ) {
        menu_readiness.mark_failed();
        menu_worker_started = false;
        return true;
    }
    std::filesystem::path data_path( PATH_INFO::datadir() );
    if( data_path.filename().empty() ) {
        data_path = data_path.parent_path();
    }
    const std::filesystem::path base = data_path.parent_path();
    // Use forward slashes in the Windows command line so a terminal
    // backslash cannot escape the closing quote of a quoted argument.
    auto quote = []( const std::filesystem::path &path ) {
        return std::wstring( L"\"" ) + path.generic_wstring() + L"\"";
    };
    // Ask the worker for exactly the pixel size the title screen will blit, so
    // no resampling happens on either side of the PNG channel.
    int canvas_w = 0;
    int canvas_h = 0;
    if( renderer ) {
        SDL_GetRendererOutputSize( renderer.get(), &canvas_w, &canvas_h );
    }
    if( canvas_w <= 0 || canvas_h <= 0 ) {
        canvas_w = 1280;
        canvas_h = 720;
    }
    canvas_w = std::min( canvas_w, menu_canvas_max_width );
    canvas_h = std::min( canvas_h, menu_canvas_max_height );
    // A new session must not replay frames left over from the previous one.
    menu_clear_worker_frames( menu_frames_path().string() );
    menu_play_index = 0;
#if defined(_WIN32)
    menu_detach_shared_frame();
    std::error_code shm_ec;
    std::filesystem::remove( menu_frames_path() / "frame_shm.txt", shm_ec );
#endif
    menu_play_texture.reset();
#if defined(_WIN32)
    menu_detach_shared_frame();
#endif
    std::wstring command = quote( executable ) +
                           L" --basepath " + quote( base ) +
                           L" --userdir " + quote( private_userdir ) +
                           L" --menu-demo-worker " + quote( menu_frames_path() ) +
                           L" --menu-demo-canvas " +
                           quote( std::filesystem::path( std::to_string( canvas_w ) + "x" +
                                   std::to_string( canvas_h ) ) );
    const std::string tile_id = get_option<std::string>( "TILES" );
    if( !tile_id.empty() ) {
        command += L" --menu-demo-tileset " +
                   quote( std::filesystem::path( tile_id ) );
    }
    STARTUPINFOW startup = {};
    startup.cb = sizeof( startup );
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process = {};
    // Tie the worker's lifetime to this process. Closing the game window with
    // the title screen still open never unwinds the menu, and an unparented
    // worker would keep simulating (and writing frames) forever.
    if( !menu_worker_job ) {
        menu_worker_job = CreateJobObjectW( nullptr, nullptr );
        if( menu_worker_job ) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info = {};
            job_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject( menu_worker_job, JobObjectExtendedLimitInformation,
                                     &job_info, sizeof( job_info ) );
        }
    }
    if( CreateProcessW( executable, &command[0], nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process ) ) {
        if( menu_worker_job ) {
            AssignProcessToJobObject( menu_worker_job, process.hProcess );
        }
        ResumeThread( process.hThread );
        CloseHandle( process.hThread );
        menu_worker_process = process.hProcess;
        menu_worker_started_tick = SDL_GetTicks();
        menu_last_progress = 0;
        fp3d_menu::timeline_mark( "demo_worker_started" );
        return true;
    }
    // Launch failure is a hard failure, not an endless session.
    dbg( D_ERROR ) << "Could not start the menu demo worker: " << GetLastError();
    menu_readiness.mark_failed();
    menu_worker_started = false;
    return true;
#endif
    return false;
}
void menu_worker_watchdog()
{
#if defined(_WIN32)
    if( !menu_worker_started || !menu_worker_process ) {
        return;
    }
    const Uint32 now = SDL_GetTicks();
    DWORD code = 0;
    const bool exited = GetExitCodeProcess( menu_worker_process, &code ) && code != STILL_ACTIVE;
    // The worker publishing new frames counts as progress even when the title
    // screen is not drawing them (settings, world setup, character creation).
    // Without this the watchdog kills a perfectly healthy worker after 20 s of
    // not being drawn, and the backdrop visibly restarts when you come back.
    const long peeked = menu_shm_peek_seq();
    if( peeked != 0 && peeked != menu_shm_peek_last ) {
        menu_shm_peek_last = peeked;
        menu_last_progress = now;
    }
    // A worker that is alive but no longer publishing frames is equally useless
    // (it can block forever inside a modal prompt); treat it as failed. The
    // first frame legitimately takes a while because the sandbox world is built
    // from scratch, hence the two different limits.
    const Uint32 since = menu_last_progress != 0 ? menu_last_progress : menu_worker_started_tick;
    const Uint32 stall_limit = menu_last_progress != 0 ? 20000 : 120000;
    const bool stalled = !exited && menu_worker_started_tick != 0 && now - since > stall_limit;
    if( !exited && !stalled ) {
        return;
    }
    if( stalled ) {
        TerminateProcess( menu_worker_process, 0 );
    }
    CloseHandle( menu_worker_process );
    menu_worker_process = nullptr;
    menu_worker_started = false;
    menu_play_texture.reset();
    menu_play_index = 0;
    menu_last_progress = 0;
    // The session is over; a frame of the next worker must pass the gate again.
    menu_readiness.end_session();
    fp3d_menu::timeline_mark( stalled ? "demo_worker_stalled" : "demo_worker_exited" );
    // Relaunching immediately would spin if the worker keeps failing (a broken
    // sandbox, a bad tileset). Failures therefore degrade to a static title,
    // and a retry is only attempted after a cool-down.
    static Uint32 last_restart = 0;
    if( last_restart != 0 && now - last_restart < 15000 ) {
        return;
    }
    last_restart = now;
    menu_start_isolated_worker();
#endif
}
#if defined(_WIN32)
// Reader side. Attaches lazily: the worker publishes its section name as soon as
// it has a frame to share.
bool menu_attach_shared_frame()
{
    if( menu_shm_read_base != nullptr ) {
        return true;
    }
    std::ifstream info( menu_frames_path() / "frame_shm.txt" );
    if( !info ) {
        return false;
    }
    std::string name;
    int wanted_w = 0;
    int wanted_h = 0;
    if( !std::getline( info, name ) || !( info >> wanted_w >> wanted_h ) ) {
        return false;
    }
    while( !name.empty() && ( name.back() == '\r' || name.back() == '\n' || name.back() == ' ' ) ) {
        name.pop_back();
    }
    if( name.empty() || wanted_w <= 0 || wanted_h <= 0 ) {
        return false;
    }
    const std::wstring wname( name.begin(), name.end() );
    HANDLE handle = OpenFileMappingW( FILE_MAP_READ, FALSE, wname.c_str() );
    if( handle == nullptr ) {
        return false;
    }
    const size_t need = menu_shm_header_bytes +
                        static_cast<size_t>( wanted_w ) * wanted_h * 4;
    const unsigned char *base = static_cast<const unsigned char *>(
                                    MapViewOfFile( handle, FILE_MAP_READ, 0, 0, need ) );
    if( base == nullptr ) {
        CloseHandle( handle );
        return false;
    }
    SDL_Texture_Ptr texture( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_ARGB8888,
                             SDL_TEXTUREACCESS_STREAMING, wanted_w, wanted_h ) );
    if( !texture ) {
        UnmapViewOfFile( base );
        CloseHandle( handle );
        return false;
    }
    SDL_Texture_Ptr staging( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_ARGB8888,
                             SDL_TEXTUREACCESS_STREAMING, wanted_w, wanted_h ) );
    if( !staging ) {
        UnmapViewOfFile( base );
        CloseHandle( handle );
        return false;
    }
    SDL_SetTextureBlendMode( texture.get(), SDL_BLENDMODE_NONE );
    SDL_SetTextureScaleMode( texture.get(), SDL_ScaleModeNearest );
    SDL_SetTextureBlendMode( staging.get(), SDL_BLENDMODE_NONE );
    SDL_SetTextureScaleMode( staging.get(), SDL_ScaleModeNearest );
    menu_shm_read_handle = handle;
    menu_shm_read_base = base;
    menu_shm_read_bytes = need;
    menu_shm_texture = std::move( texture );
    menu_shm_staging = std::move( staging );
    menu_shm_tex_w = wanted_w;
    menu_shm_tex_h = wanted_h;
    menu_shm_name = name;
    menu_shm_last_seq = 0;
    menu_shm_has_frame = false;
    return true;
}
// Returns true when a newer frame was copied into the texture.
bool menu_try_shared_frame()
{
    if( menu_shm_read_base == nullptr && !menu_attach_shared_frame() ) {
        return false;
    }
    if( !menu_shm_texture || !menu_shm_staging ) {
        return false;
    }
    const volatile LONG *seq = reinterpret_cast<const volatile LONG *>( menu_shm_read_base );
    const long before = *seq;
    if( before == 0 || ( before & 1 ) != 0 || before == menu_shm_last_seq ) {
        return false;
    }
    const int width = *reinterpret_cast<const int *>( menu_shm_read_base + 4 );
    const int height = *reinterpret_cast<const int *>( menu_shm_read_base + 8 );
    if( width != menu_shm_tex_w || height != menu_shm_tex_h ) {
        return false;
    }
    fp3d_menu::frame_report report;
    report.seq = before;
    report.width = width;
    report.height = height;
    // The producer is mid-write or the upload failed: report it, so the gate
    // keeps refusing this frame instead of silently accepting a texture that
    // was created but never received any pixels.
    if( SDL_UpdateTexture( menu_shm_staging.get(), nullptr,
                           menu_shm_read_base + menu_shm_header_bytes, width * 4 ) != 0 ) {
        report.seq_stable = true;
        report.uploaded = false;
        menu_readiness.accept_frame( report, fp3d_menu::now_ms() );
        dbg( D_WARNING ) << "menu demo frame upload failed: " << SDL_GetError();
        return false;
    }
    if( *seq != before ) {
        // The worker started writing again while we copied. Keep the previous
        // valid frame on screen and drop the candidate texture.
        return false;
    }
    std::swap( menu_shm_staging, menu_shm_texture );
    menu_shm_has_frame = true;
    menu_shm_last_seq = before;
    report.seq_stable = true;
    report.uploaded = true;
    menu_readiness.accept_frame( report, fp3d_menu::now_ms() );
    return true;
}
void menu_detach_shared_frame()
{
    menu_shm_texture.reset();
    menu_shm_staging.reset();
    menu_shm_has_frame = false;
    if( menu_shm_read_base != nullptr ) {
        UnmapViewOfFile( menu_shm_read_base );
        menu_shm_read_base = nullptr;
    }
    if( menu_shm_read_handle != nullptr ) {
        CloseHandle( menu_shm_read_handle );
        menu_shm_read_handle = nullptr;
    }
    menu_shm_last_seq = 0;
    menu_shm_tex_w = 0;
    menu_shm_tex_h = 0;
}
#endif
void menu_load_next_frame()
{
    menu_worker_watchdog();
    const Uint32 now = SDL_GetTicks();
#if defined(_WIN32)
    // Shared memory is the real channel now; PNG files remain the fallback.
    if( menu_try_shared_frame() ) {
        menu_last_progress = now;
        menu_last_play = now;
        return;
    }
#endif
    if( now - menu_last_play < menu_frame_interval && menu_play_texture ) {
        return;
    }
    const std::filesystem::path candidate = menu_frame_path( menu_play_index );
    SDL_Surface_Ptr image( IMG_Load( candidate.string().c_str() ) );
    if( image ) {
        SDL_Texture_Ptr texture( SDL_CreateTextureFromSurface( renderer.get(), image.get() ) );
        if( texture ) {
            SDL_SetTextureBlendMode( texture.get(), SDL_BLENDMODE_NONE );
            SDL_SetTextureScaleMode( texture.get(), SDL_ScaleModeNearest );
            menu_play_texture = std::move( texture );
            ++menu_play_index;
            menu_last_progress = now;
            // The fallback channel feeds the very same readiness rules as the
            // shared-memory one: a fully decoded frame plus a created texture.
            fp3d_menu::frame_report report;
            report.seq = static_cast<long>( menu_play_index );
            report.width = image->w;
            report.height = image->h;
            report.seq_stable = true;
            report.uploaded = true;
            menu_readiness.accept_frame( report, fp3d_menu::now_ms() );
            // Completed frames can be discarded to bound disk use during
            // arbitrarily long sequential vanilla-world demonstrations. The
            // occasional sweep also clears frames a missed read left behind,
            // which a single "index - 2" delete per frame would never revisit.
            std::error_code ec;
            std::filesystem::remove( menu_frame_path( menu_play_index - 2 ), ec );
            if( menu_play_index > 8 && ( menu_play_index % 16 ) == 0 ) {
                for( size_t stale = menu_play_index - 8; stale + 4 < menu_play_index; ++stale ) {
                    std::error_code sweep_ec;
                    std::filesystem::remove( menu_frame_path( stale ), sweep_ec );
                }
            }
        }
    }
    menu_last_play = now;
}
/**
 * One receive step, used both by the startup wait loop and by the drawing path.
 * De-duplicated per tick so a frame is never consumed twice for one presented
 * frame, no matter how many callers ask for it.
 */
void menu_poll_frame()
{
    const Uint32 tick = SDL_GetTicks();
    if( tick == menu_last_poll_tick ) {
        return;
    }
    menu_last_poll_tick = tick;
    menu_load_next_frame();
}
/**
 * Publishes a vanilla still image through the very channel the title screen
 * already consumes (the PNG fallback), so the menu and its backdrop are drawn by
 * the same verified path as a live frame instead of a second drawing route.
 * Used when the demonstration process cannot provide a scene.
 */
void menu_commit_static_backdrop( const std::string &image_path )
{
    if( image_path.empty() ) {
        return;
    }
    std::error_code ec;
    const std::filesystem::path directory = menu_frames_path();
    std::filesystem::create_directories( directory, ec );
    if( ec ) {
        dbg( D_ERROR ) << "Static title backdrop directory unavailable: " << ec.message();
        return;
    }
    const std::filesystem::path target = directory / "frame_0.png";
    if( !std::filesystem::is_directory( directory, ec ) ) {
        return;
    }
    std::filesystem::copy_file( image_path, target,
                                std::filesystem::copy_options::overwrite_existing, ec );
    if( ec ) {
        dbg( D_ERROR ) << "Static title backdrop unavailable: " << ec.message();
        return;
    }
    // Play it immediately: the title screen is waiting for this frame now.
    menu_play_index = 0;
    menu_play_texture.reset();
    menu_last_play = 0;
    menu_load_next_frame();
    fp3d_menu::timeline_mark( "static_backdrop_ready" );
}
void menu_draw_native_background( int width, int height )
{
    menu_poll_frame();
    SDL_Texture *live = nullptr;
#if defined(_WIN32)
    // A texture that was merely created (mapping attached, no upload yet) must
    // never hide a usable PNG frame or paint a black rectangle.
    if( menu_shm_texture && menu_shm_has_frame ) {
        live = menu_shm_texture.get();
    }
#endif
    if( live == nullptr ) {
        live = menu_play_texture.get();
    }
    if( live == nullptr || width <= 0 || height <= 0 ) {
        return;
    }
#if defined(_WIN32)
    // Diagnostic: how often the title screen redraws, and which shared frame
    // sequence it consumed. Makes the end-to-end rate measurable.
    {
        static Uint32 last_trace_ms = 0;
        static size_t draw_count = 0;
        static long first_seq = -1;
        ++draw_count;
        if( first_seq < 0 ) {
            first_seq = menu_shm_last_seq;
        }
        const Uint32 now_ms = SDL_GetTicks();
        if( now_ms - last_trace_ms > 2000 ) {
            last_trace_ms = now_ms;
            std::ofstream tr( menu_frames_path() / "parent_draw.txt" );
            tr << "draws=" << draw_count << " seq=" << menu_shm_last_seq
               << " first_seq=" << first_seq << " t=" << now_ms << "\n";
            tr << std::endl;
        }
    }
#endif
    int image_width = 0, image_height = 0;
    SDL_QueryTexture( live, nullptr, nullptr,
                      &image_width, &image_height );
    if( image_width <= 0 || image_height <= 0 ) {
        return;
    }
    // Cover the title without stretching map cells or vehicle proportions.
    // Crop overflow along the longer axis, always at its center.
    SDL_Rect src = { 0, 0, image_width, image_height };
    if( static_cast<int64_t>( image_width ) * height >
        static_cast<int64_t>( width ) * image_height ) {
        src.w = std::max( 1, image_height * width / height );
        src.x = ( image_width - src.w ) / 2;
    } else {
        src.h = std::max( 1, image_width * height / width );
        src.y = ( image_height - src.h ) / 2;
    }
    SDL_Rect dst = { 0, 0, width, height };
    SDL_RenderCopy( renderer.get(), live, &src, &dst );
    // Preserve *all* original ASCII title/menu text and version metadata.
    SDL_BlendMode blend;
    SDL_GetRenderDrawBlendMode( renderer.get(), &blend );
    SDL_SetRenderDrawBlendMode( renderer.get(), SDL_BLENDMODE_BLEND );
    SDL_SetRenderDrawColor( renderer.get(), 4, 8, 11, 140 );
    SDL_Rect left = { 0, 0, std::min( 250, width / 5 ), height };
    SDL_RenderFillRect( renderer.get(), &left );
    SDL_SetRenderDrawBlendMode( renderer.get(), blend );
}
} // namespace
void set_menu_demo_terrain_bounds( const SDL_Rect &rect )
{
    menu_terrain_rect = rect;
    menu_demo_terrain_updated = true;
}
void menu_demo_publish_worker_frame() { menu_record_worker_frame(); }
bool menu_demo_spectator_visibility()
{
    // Only the isolated demonstration world renders omnisciently; normal play
    // keeps the original fog of war, lighting and line-of-sight behaviour.
    return menu_demo_worker_mode;
}
bool menu_demo_night_scene()
{
    return menu_demo_night_mode;
}
void set_native_menu_night( bool night )
{
    menu_demo_night_mode = night;
}
void set_native_menu_frame_hook( std::function<void()> hook )
{
    menu_frame_hook = std::move( hook );
}
void menu_demo_draw_live_frame( int width, int height, Uint32 )
{
    menu_draw_native_background( width, height );
}

// The sub-tile pan that belongs to the frame that was just drawn. The camera hook
// keeps advancing while the frame is being cropped, so reading the live value there
// would shift the crop by one square whenever the offset crosses a boundary - an
// occasional one-tile twitch.
void set_native_menu_capture_pan( float x, float y )
{
    menu_capture_pan_x = x;
    menu_capture_pan_y = y;
}
bool start_native_menu_demo()
{
#if defined(_WIN32)
    return menu_start_isolated_worker();
#else
    // No isolated scene producer on this platform: fail fast instead of making
    // the title screen wait for a frame that cannot arrive.
    menu_readiness.mark_unsupported();
    return false;
#endif
}
void update_native_menu_demo()
{
    // Receiving must not depend on drawing: the title screen waits for the
    // first frame while its menu is still hidden.
    menu_poll_frame();
}
native_menu_demo_report native_menu_demo_status()
{
    native_menu_demo_report status;
    status.state = menu_readiness.evaluate( fp3d_menu::now_ms() );
    status.frames = menu_readiness.frames_accepted();
    status.session_active = menu_readiness.session_active();
#if defined(_WIN32)
    status.worker_running = menu_worker_process != nullptr;
#endif
    return status;
}
void set_main_menu_static_backdrop( const std::string &image_path )
{
    menu_commit_static_backdrop( image_path );
}
void stop_native_menu_demo()
{
#if defined(_WIN32)
    if( menu_worker_process ) {
        DWORD code = 0;
        if( GetExitCodeProcess( menu_worker_process, &code ) && code == STILL_ACTIVE ) {
            TerminateProcess( menu_worker_process, 0 );
        }
        CloseHandle( menu_worker_process );
        menu_worker_process = nullptr;
    }
#endif
    menu_worker_started = false;
    menu_play_index = 0;
    menu_play_texture.reset();
    // Leaving the title ends the session; returning to it starts a fresh one.
    menu_readiness.reset();
#if defined(_WIN32)
    menu_detach_shared_frame();
#endif
}
void set_native_menu_worker_output( const std::string &output )
{
    menu_is_worker = true;
    menu_demo_worker_mode = true;
    menu_worker_output = output;
    menu_worker_frame_id = 0;
    menu_last_capture = 0;
}
void set_native_menu_canvas( int width, int height )
{
    menu_canvas_width = width > 0 ? std::min( width, menu_canvas_max_width ) : 0;
    menu_canvas_height = height > 0 ? std::min( height, menu_canvas_max_height ) : 0;
}
void set_native_menu_pan( float fraction_x, float fraction_y )
{
    // Kept inside one square: the whole-square part is the camera offset itself.
    menu_canvas_pan_x = std::clamp( fraction_x, -0.5f, 0.5f );
    menu_canvas_pan_y = std::clamp( fraction_y, -0.5f, 0.5f );
}
void menu_clear_worker_frames( const std::string &directory )
{
    std::error_code ec;
    if( directory.empty() || !std::filesystem::is_directory( directory, ec ) ) {
        return;
    }
    for( const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator( directory, ec ) ) {
        if( ec ) {
            return;
        }
        if( !entry.is_regular_file( ec ) || ec ) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const bool frame = name.rfind( "frame_", 0 ) == 0 || name.rfind( "tmp_", 0 ) == 0;
        if( !frame ) {
            continue;
        }
        if( entry.path().extension() != ".png" && entry.path().extension() != ".bmp" ) {
            continue;
        }
        std::filesystem::remove( entry.path(), ec );
        ec.clear();
    }
}
