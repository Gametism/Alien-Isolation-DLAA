//Alien: Isolation DLAA by Gametism

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <string>
#include <array>
#include <algorithm>
#include <new>

#include "asi_bridge_v100.hpp"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    HMODULE g_module = nullptr;
    wchar_t g_log_path[MAX_PATH]{};
    std::mutex g_mutex;

    constexpr bool g_release_verbose_diagnostics = false;
    constexpr bool g_release_keep_periodic_status = false;
    bool g_release_ready_logged = false;

    wchar_t g_ini_path[MAX_PATH]{};

    UINT g_menu_hotkey = VK_F7;
    UINT g_toggle_dlaa_hotkey = VK_F8;

    bool g_overlay_visible = false;
    bool g_overlay_dirty = true;
    int g_overlay_selection = 0;
    int g_overlay_capture_target = 0; 

    int g_menu_scale_percent = 100;
    bool g_overlay_capture_armed = false;

    std::array<bool, 256> g_overlay_key_prev{};

    bool g_menu_hotkey_wait_for_release = false;
    bool g_toggle_hotkey_wait_for_release = false;

    using PFN_Present = HRESULT (STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT);
    PFN_Present g_orig_present = nullptr;
    bool g_present_hooked = false;

    ID3D11VertexShader *g_overlay_vs = nullptr;
    ID3D11PixelShader *g_overlay_ps = nullptr;
    ID3D11SamplerState *g_overlay_sampler = nullptr;
    ID3D11RasterizerState *g_overlay_rasterizer = nullptr;
    ID3D11DepthStencilState *g_overlay_depth_state = nullptr;
    ID3D11BlendState *g_overlay_blend_state = nullptr;
    ID3D11ShaderResourceView *g_overlay_srv = nullptr;
    ID3D11Texture2D *g_overlay_texture = nullptr;

    bool g_overlay_resources_logged = false;
    bool g_overlay_first_render_logged = false;

    HWND g_game_window = nullptr;
    WNDPROC g_original_wndproc = nullptr;
    bool g_window_input_hooked = false;

    int g_overlay_mouse_client_x = 0;
    int g_overlay_mouse_client_y = 0;
    bool g_overlay_mouse_inside = false;
    bool g_overlay_mouse_left_down = false;

    bool g_overlay_virtual_cursor_initialized = false;
    float g_overlay_virtual_cursor_x = 0.0f;
    float g_overlay_virtual_cursor_y = 0.0f;

    constexpr UINT kOverlayWidth = 720;
    constexpr UINT kOverlayHeight = 500;

    bool patch_vtable(void *object, size_t index, void *replacement, void **original);

    LRESULT CALLBACK overlay_window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void install_window_input_hook(HWND hwnd);
    bool overlay_mouse_to_local(int client_x, int client_y, int &local_x, int &local_y);
    int overlay_row_from_local_y(int y);
    void process_overlay_mouse_click(int client_x, int client_y);

    enum class ShaderRole : uint32_t
    {
        none = 0,
        ps_rgbm_encode,
        ps_dof_encode,
        ps_camera_motion,
        vs_smaa,
        vs_rgbm_encode,
    };

    struct DxbcHash
    {
        uint32_t a, b, c, d;
    };

    constexpr DxbcHash kPsRgbmEncode   {0x4fcfc7f7u, 0x5c5e12cfu, 0x059e8b33u, 0x11f8489bu};
    constexpr DxbcHash kPsDofEncode    {0x29ed6504u, 0x77d5438cu, 0xe9c206c8u, 0xb1f27ba2u};
    constexpr DxbcHash kPsCameraMotion {0x1fb3edd4u, 0xe984323bu, 0x11bcf154u, 0x5a029c94u};
    constexpr DxbcHash kVsSmaa         {0x021e3541u, 0xc8808c25u, 0x81beb6beu, 0x342bfdddu};
    constexpr DxbcHash kVsRgbmEncode   {0xa851671du, 0xbe79cf68u, 0x2e6d9376u, 0x567ba13cu};

    std::unordered_map<ID3D11VertexShader *, ShaderRole> g_vs_roles;
    std::unordered_map<ID3D11PixelShader *, ShaderRole> g_ps_roles;
    std::unordered_map<ID3D11VertexShader *, DxbcHash> g_vs_hashes;
    std::unordered_map<ID3D11PixelShader *, DxbcHash> g_ps_hashes;

    ID3D11VertexShader *g_current_vs = nullptr;
    ID3D11PixelShader *g_current_ps = nullptr;

    bool g_zero_jitter_enabled = false;
    bool g_dlaa_injection_enabled = true;
    uint64_t g_dlaa_toggle_count = 0;
    uint64_t g_dlaa_disable_frame = 0;
    constexpr bool g_native_smaa_passthrough_enabled = true;
    uint32_t g_dlss_mode = 0;
    bool g_ngx_auto_exposure = true;
    uint64_t g_smaa_passthrough_copies = 0;
    uint64_t g_smaa_passthrough_rejected = 0;
    uint64_t g_zero_jitter_writes = 0;
    uint64_t g_draw_counter = 0;

    uint64_t g_texture_create_serial = 0;
    uint64_t g_candidate_texture_creates = 0;
    uint64_t g_view_create_serial = 0;
    uint64_t g_viewport_change_serial = 0;

    struct TextureProbeInfo
    {
        uint64_t serial = 0;
        D3D11_TEXTURE2D_DESC desc{};
    };

    std::unordered_map<ID3D11Texture2D *, TextureProbeInfo> g_texture_probe_info;

    float g_last_viewport_w = -1.0f;
    float g_last_viewport_h = -1.0f;

    bool g_short_probe_armed = true;
    bool g_short_probe_active = false;
    bool g_short_probe_completed = false;
    uint64_t g_short_probe_start_frame = 0;
    uint64_t g_short_probe_end_frame = 0;
    uint64_t g_short_probe_events = 0;
    constexpr uint64_t HALFRES_PROBE_EVENT_CAP = 300;

    ID3D11Texture2D *g_target_half_color = nullptr;
    ID3D11Texture2D *g_target_half_depth = nullptr;
    uint64_t g_target_half_color_serial = 0;
    uint64_t g_target_half_depth_serial = 0;
    uint64_t g_target_generation = 0;

    ID3D11Texture2D *g_pending_half_color = nullptr;
    uint64_t g_pending_half_color_serial = 0;
    uint64_t g_halfres_scene_input_events = 0;
    uint64_t g_halfres_target_events = 0;
    constexpr uint64_t PRODUCER_EVENT_CAP = 500;
    uint64_t g_half_color_producer_hits = 0;
    std::unordered_map<ID3D11PixelShader *, uint64_t> g_half_color_ps_hits;
    std::unordered_map<ID3D11VertexShader *, uint64_t> g_half_color_vs_hits;
    const char *g_probe_draw_kind = "unknown";
    UINT g_probe_draw_count = 0;
    UINT g_probe_draw_start = 0;
    INT g_probe_draw_base_vertex = 0;
    constexpr uint32_t SCENE_WRITER_RING_SIZE = 12;
    constexpr uint32_t SCENE_POST_CONSUMER_LIMIT = 12;
    constexpr uint64_t SCENE_BOUNDARY_EVENT_CAP = 320;

    struct SceneInputSnapshot
    {
        bool bound = false;
        uint64_t serial = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t format = 0;
    };

    struct SceneWriterSnapshot
    {
        bool valid = false;
        uint64_t frame = 0;
        uint64_t draw = 0;
        const char *kind = "unknown";
        UINT count = 0;
        UINT start = 0;
        INT base = 0;
        uint32_t topology = 0;
        float viewport_w = 0.0f;
        float viewport_h = 0.0f;
        ID3D11VertexShader *vs = nullptr;
        DxbcHash vs_hash{};
        int vs_role = 0;
        ID3D11PixelShader *ps = nullptr;
        DxbcHash ps_hash{};
        int ps_role = 0;
        uint64_t dsv_serial = 0;
        uint32_t dsv_width = 0;
        uint32_t dsv_height = 0;
        uint32_t dsv_format = 0;
        SceneInputSnapshot inputs[16]{};
    };

    SceneWriterSnapshot g_scene_writer_ring[SCENE_WRITER_RING_SIZE]{};
    uint32_t g_scene_writer_ring_next = 0;
    uint32_t g_scene_writer_ring_count = 0;

    bool g_scene_post_handoff_active = false;
    uint64_t g_scene_post_handoff_frame = 0;
    uint32_t g_scene_post_consumer_count = 0;
    uint64_t g_scene_boundary_handoffs = 0;

    constexpr uint64_t GEOMETRY_JITTER_EVENT_CAP = 240;
    uint64_t g_geometry_scene_draws = 0;
    uint64_t g_geometry_jitter_reads = 0;
    uint64_t g_geometry_jitter_nonzero = 0;
    uint64_t g_geometry_jitter_matches_frame = 0;
    uint64_t g_geometry_jitter_mismatches_frame = 0;

    constexpr uint64_t GEOMETRY_CB_EVENT_CAP = 260;
    constexpr uint32_t GEOMETRY_CB_MAX_FLOATS = 256;
    constexpr uint32_t GEOMETRY_CB_TRACKED_SHADERS = 6;

    struct GeometryCbFloatSample
    {
        bool valid = false;
        float value = 0.0f;
    };

    struct GeometryCbShaderTrack
    {
        bool used = false;
        DxbcHash vs_hash{};
        ID3D11VertexShader *vs = nullptr;
        uint64_t seen_draws = 0;
        uint32_t cb_slot = 0;
        uint32_t cb_bytes = 0;
        uint32_t sample_count = 0;
        uint64_t frame_ids[6]{};
        float frame_jitter_x[6]{};
        float frame_jitter_y[6]{};
        float values[6][GEOMETRY_CB_MAX_FLOATS]{};
        bool value_valid[6][GEOMETRY_CB_MAX_FLOATS]{};
    };

    GeometryCbShaderTrack g_geometry_cb_tracks[GEOMETRY_CB_TRACKED_SHADERS]{};
    uint64_t g_geometry_cb_samples = 0;
    uint64_t g_geometry_cb_candidates = 0;

    bool g_zero_geometry_jitter = false;
    uint64_t g_geometry_jitter_patch_count = 0;
    uint64_t g_geometry_jitter_patch_failures = 0;

    bool g_prev_pageup_down = false;
    bool g_prev_f8_down = false;
    uint64_t g_jitter_hotkey_edges = 0;

    constexpr uint64_t RES_ARCH_EVENT_CAP = 420;
    uint64_t g_res_arch_events = 0;
    uint64_t g_res_arch_scene_writes = 0;
    uint64_t g_res_arch_depth_writes = 0;
    uint64_t g_res_arch_motion_writes = 0;
    uint64_t g_res_arch_other_primary_writes = 0;
    bool g_res_arch_capture_active = false;

    bool g_native_quality_test_enabled = false;
    bool g_prev_native_quality_f8_down = false;
    uint64_t g_native_quality_draws = 0;
    uint64_t g_native_quality_jitter_scales = 0;
    uint64_t g_native_quality_rejected_fullscreen = 0;
    uint64_t g_native_quality_rejected_target = 0;

    constexpr uint64_t RESOURCE_FAMILY_EVENT_CAP = 520;
    bool g_resource_family_probe_active = false;
    uint64_t g_resource_family_probe_end_frame = 0;
    uint64_t g_resource_family_events = 0;
    uint64_t g_resource_family_reads = 0;
    uint64_t g_resource_family_writes = 0;
    uint64_t g_resource_family_scene_hits = 0;
    uint64_t g_resource_family_depth_hits = 0;

    ID3D11Texture2D *g_primary_geom_color = nullptr;
    ID3D11Texture2D *g_primary_geom_depth = nullptr;

    constexpr uint64_t TRANSITION_PROBE_EVENT_CAP = 120;
    bool g_transition_probe_active = false;
    uint64_t g_transition_probe_end_frame = 0;
    uint64_t g_transition_probe_events = 0;
    uint64_t g_transition_primary_writes = 0;
    uint64_t g_transition_linear_depth_writes = 0;
    uint64_t g_transition_motion_writes = 0;
    uint64_t g_transition_scene_writes = 0;

    bool g_transition_bound_primary_color = false;
    bool g_transition_bound_primary_depth = false;
    bool g_transition_bound_linear_depth_rtv = false;
    bool g_transition_bound_motion_rtv = false;
    bool g_transition_bound_scene_rtv = false;

    bool g_transition_reads_primary_color = false;
    bool g_transition_reads_primary_depth = false;
    bool g_transition_reads_linear_depth = false;
    bool g_transition_reads_motion = false;
    bool g_transition_reads_scene = false;

    enum class TransitionSrvRole : uint8_t
    {
        none = 0,
        primary_color,
        primary_depth,
        linear_depth,
        motion,
        scene
    };

    TransitionSrvRole g_transition_ps_srv_roles[16]{};

    enum class TransitionStage : uint8_t
    {
        none = 0,
        primary_geometry,
        linear_depth,
        motion,
        scene_color
    };

    TransitionStage g_transition_last_logged_stage = TransitionStage::none;

    struct TransitionShaderPair
    {
        bool used = false;
        TransitionStage stage = TransitionStage::none;
        DxbcHash vs{};
        DxbcHash ps{};
    };

    TransitionShaderPair g_transition_seen_pairs[96]{};
    uint32_t g_transition_seen_pair_count = 0;


    bool g_primary_interval_test_enabled = false;
    bool g_prev_primary_interval_f8_down = false;
    uint64_t g_primary_interval_draws = 0;
    uint64_t g_primary_interval_frames = 0;
    uint64_t g_primary_interval_last_frame = ~0ull;

    constexpr UINT PRIMARY_INTERVAL_W = 2560;
    constexpr UINT PRIMARY_INTERVAL_H = 1440;


    ID3D11RenderTargetView *g_primary_geom_rtv = nullptr;
    ID3D11DepthStencilView *g_primary_geom_dsv = nullptr;

    ID3D11Texture2D *g_sub_primary_color = nullptr;
    ID3D11RenderTargetView *g_sub_primary_rtv = nullptr;
    ID3D11Texture2D *g_sub_primary_depth = nullptr;
    ID3D11DepthStencilView *g_sub_primary_dsv = nullptr;

    bool g_primary_substitution_ready = false;
    bool g_primary_substitution_bound = false;
    bool g_primary_copyback_in_progress = false;
    uint64_t g_primary_substitution_binds = 0;
    uint64_t g_primary_substitution_copybacks = 0;
    uint64_t g_primary_substitution_frames = 0;
    uint64_t g_primary_substitution_last_frame = ~0ull;

    ID3D11ShaderResourceView *g_sub_primary_color_srv = nullptr;
    uint64_t g_primary_color_srv_substitutions = 0;
    uint64_t g_primary_color_srv_calls = 0;


    bool g_handoff42_active = false;
    bool g_handoff42_prev_f8_down = false;
    uint64_t g_handoff42_end_frame = 0;
    uint64_t g_handoff42_events = 0;
    uint64_t g_handoff42_draws = 0;
    uint64_t g_handoff42_copies = 0;
    uint64_t g_handoff42_resolves = 0;
    uint64_t g_handoff42_om_binds = 0;
    uint64_t g_handoff42_ps_binds = 0;
    constexpr uint64_t HANDOFF42_EVENT_CAP = 260;

    constexpr UINT NATIVE_QUALITY_W = 2560;
    constexpr UINT NATIVE_QUALITY_H = 1440;
    uint64_t g_res_arch_capture_end_frame = 0;
    uint64_t g_consumer_a_hits = 0;
    uint64_t g_consumer_b_hits = 0;
    DxbcHash g_consumer_a_ps_hash{};
    DxbcHash g_consumer_b_ps_hash{};
    constexpr DxbcHash CONSUMER_A_HASH{
        0x44F5DF55u, 0xA2E6B9E3u, 0xB1E4B01Eu, 0xA03EDB4Fu
    };

    constexpr DxbcHash CONSUMER_B_HASH{
        0xE5245D87u, 0xB8EFF548u, 0x56877121u, 0x0A0D4305u
    };


    ID3D11Texture2D *g_probe_scene_color = nullptr;
    ID3D11Texture2D *g_probe_velocity = nullptr;
    ID3D11Texture2D *g_probe_depth = nullptr;

    uint64_t g_render_frame = 0;
    uint64_t g_last_camera_motion_logged = ~0ull;
    uint64_t g_last_rgbm_logged = ~0ull;

    ID3D11Texture2D *g_last_velocity_tex = nullptr;
    ID3D11Texture2D *g_last_depth_tex = nullptr;
    ID3D11Texture2D *g_last_scene_tex = nullptr;

    float g_current_jitter_x_pixels = 0.0f;
    float g_current_jitter_y_pixels = 0.0f;

    constexpr float g_dlaa_jitter8[8][2] =
    {
        {  0.000000f, -0.166667f },
        { -0.250000f,  0.166667f },
        {  0.250000f, -0.388889f },
        { -0.375000f, -0.055556f },
        {  0.125000f,  0.277778f },
        { -0.125000f, -0.277778f },
        {  0.375000f,  0.055556f },
        { -0.437500f,  0.388889f }
    };

    inline uint32_t dlaa_jitter_index_for_frame(uint64_t frame)
    {
        return static_cast<uint32_t>(frame & 7ull);
    }

    inline void dlaa_jitter_for_frame(
        uint64_t frame,
        float &x_pixels,
        float &y_pixels)
    {
        const uint32_t index = dlaa_jitter_index_for_frame(frame);
        x_pixels = g_dlaa_jitter8[index][0];
        y_pixels = g_dlaa_jitter8[index][1];
    }

    uint64_t g_dlaa_jitter_patch_draws = 0;
    uint64_t g_dlaa_jitter_patch_failures = 0;

    void set_tracked_texture(ID3D11Texture2D *&slot, ID3D11Texture2D *value)
    {
        if(slot==value)
            return;
        if(value)
            value->AddRef();
        if(slot)
            slot->Release();
        slot=value;
    }

    void init_log_path()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(g_module, path, MAX_PATH);
        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash != nullptr)
            slash[1] = L'\0';
        else
            path[0] = L'\0';

        _snwprintf_s(
            g_log_path, _countof(g_log_path), _TRUNCATE,
            L"%sAlienIsolationDLAA.log", path);
    }

    void reset_log()
    {
        FILE *f = nullptr;
        if (_wfopen_s(&f, g_log_path, L"wb") == 0 && f != nullptr)
        {
            fputs("Alien: Isolation DLAA v1.0\n", f);
            fputs("Native-resolution NVIDIA DLAA integration with in-game configuration overlay.\n", f);
            fputs("Version 1.0 Final | Created by Gametism\n", f);
            fclose(f);
        }
    }

    void log_line(const char *fmt, ...)
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        FILE *f = nullptr;
        if (_wfopen_s(&f, g_log_path, L"a") != 0 || f == nullptr)
            return;

        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fputc('\n', f);
        fclose(f);
    }


    void init_ini_path()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(g_module, path, MAX_PATH);

        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash != nullptr)
            *(slash + 1) = L'\0';
        else
            path[0] = L'\0';

        _snwprintf_s(
            g_ini_path,
            _countof(g_ini_path),
            _TRUNCATE,
            L"%sAlienIsolationDLAA.ini",
            path);
    }

    std::wstring hotkey_name(UINT vk)
    {
        if (vk >= VK_F1 && vk <= VK_F24)
            return L"F" + std::to_wstring(vk - VK_F1 + 1);

        if (vk >= 'A' && vk <= 'Z')
            return std::wstring(1, static_cast<wchar_t>(vk));

        if (vk >= '0' && vk <= '9')
            return std::wstring(1, static_cast<wchar_t>(vk));

        switch (vk)
        {
        case VK_INSERT: return L"Insert";
        case VK_DELETE: return L"Delete";
        case VK_HOME: return L"Home";
        case VK_END: return L"End";
        case VK_PRIOR: return L"Page Up";
        case VK_NEXT: return L"Page Down";
        case VK_TAB: return L"Tab";
        case VK_SPACE: return L"Space";
        case VK_CAPITAL: return L"Caps Lock";
        case VK_SCROLL: return L"Scroll Lock";
        case VK_PAUSE: return L"Pause";
        case VK_NUMPAD0: return L"Numpad 0";
        case VK_NUMPAD1: return L"Numpad 1";
        case VK_NUMPAD2: return L"Numpad 2";
        case VK_NUMPAD3: return L"Numpad 3";
        case VK_NUMPAD4: return L"Numpad 4";
        case VK_NUMPAD5: return L"Numpad 5";
        case VK_NUMPAD6: return L"Numpad 6";
        case VK_NUMPAD7: return L"Numpad 7";
        case VK_NUMPAD8: return L"Numpad 8";
        case VK_NUMPAD9: return L"Numpad 9";
        default:
            break;
        }

        wchar_t name[64]{};
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16;
        if (GetKeyNameTextW(static_cast<LONG>(scan), name, _countof(name)) > 0)
            return name;

        return L"Key " + std::to_wstring(vk);
    }

    void save_config()
    {
        if (g_ini_path[0] == L'\0')
            return;

        wchar_t value[32]{};

        _snwprintf_s(value, _countof(value), _TRUNCATE, L"%u", g_menu_hotkey);
        WritePrivateProfileStringW(L"Hotkeys", L"Menu", value, g_ini_path);

        _snwprintf_s(value, _countof(value), _TRUNCATE, L"%u", g_toggle_dlaa_hotkey);
        WritePrivateProfileStringW(L"Hotkeys", L"ToggleDLAA", value, g_ini_path);

        WritePrivateProfileStringW(
            L"DLAA",
            L"Enabled",
            g_dlaa_injection_enabled ? L"1" : L"0",
            g_ini_path);

        _snwprintf_s(
            value,
            _countof(value),
            _TRUNCATE,
            L"%d",
            g_menu_scale_percent);

        WritePrivateProfileStringW(
            L"Interface",
            L"MenuScale",
            value,
            g_ini_path);
    }

    void load_config()
    {
        init_ini_path();

        g_menu_hotkey = static_cast<UINT>(
            GetPrivateProfileIntW(L"Hotkeys", L"Menu", VK_F7, g_ini_path));

        g_toggle_dlaa_hotkey = static_cast<UINT>(
            GetPrivateProfileIntW(L"Hotkeys", L"ToggleDLAA", VK_F8, g_ini_path));

        g_dlaa_injection_enabled =
            GetPrivateProfileIntW(L"DLAA", L"Enabled", 1, g_ini_path) != 0;

        g_menu_scale_percent =
            GetPrivateProfileIntW(
                L"Interface",
                L"MenuScale",
                100,
                g_ini_path);

        if (g_menu_scale_percent != 80 &&
            g_menu_scale_percent != 100 &&
            g_menu_scale_percent != 125 &&
            g_menu_scale_percent != 150)
        {
            g_menu_scale_percent = 100;
        }

        if (g_menu_hotkey == 0 || g_menu_hotkey > 255)
            g_menu_hotkey = VK_F7;

        if (g_toggle_dlaa_hotkey == 0 || g_toggle_dlaa_hotkey > 255)
            g_toggle_dlaa_hotkey = VK_F8;
    }

    void apply_dlaa_enabled(bool enabled, const char *source)
    {
        if (g_dlaa_injection_enabled == enabled)
            return;

        g_dlaa_injection_enabled = enabled;
        ++g_dlaa_toggle_count;

        if (enabled)
        {
            asi_bridge_v100::request_history_reset();

            log_line(
                "DLAA ON source=%s toggle=%llu frame=%llu historyReset=1",
                source,
                static_cast<unsigned long long>(g_dlaa_toggle_count),
                static_cast<unsigned long long>(g_render_frame));
        }
        else
        {
            g_dlaa_disable_frame = g_render_frame;

            log_line(
                "DLAA OFF source=%s toggle=%llu frame=%llu",
                source,
                static_cast<unsigned long long>(g_dlaa_toggle_count),
                static_cast<unsigned long long>(g_render_frame));
        }

        save_config();
        g_overlay_dirty = true;
    }

    bool overlay_key_pressed(UINT vk)
    {
        if (vk == 0 || vk > 255)
            return false;

        const bool down = (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
        const bool pressed = down && !g_overlay_key_prev[vk];
        g_overlay_key_prev[vk] = down;
        return pressed;
    }


    bool release_latched_hotkey_pressed(
        UINT vk,
        bool &wait_for_release)
    {
        if (vk == 0 || vk > 255)
            return false;

        const bool down =
            (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;

        if (wait_for_release)
        {
            if (!down)
                wait_for_release = false;

            return false;
        }

        if (down)
        {
            wait_for_release = true;

            g_overlay_key_prev[vk] = true;
            return true;
        }

        g_overlay_key_prev[vk] = false;
        return false;
    }

    bool any_overlay_key_down()
    {
        for (UINT vk = 1; vk < 256; ++vk)
        {
            if ((GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0)
                return true;
        }
        return false;
    }


    void cycle_menu_scale()
    {
        switch (g_menu_scale_percent)
        {
        case 80:  g_menu_scale_percent = 100; break;
        case 100: g_menu_scale_percent = 125; break;
        case 125: g_menu_scale_percent = 150; break;
        default:  g_menu_scale_percent = 80;  break;
        }

        save_config();
        g_overlay_dirty = true;

        log_line(
            "MENU scale=%d%%",
            g_menu_scale_percent);
    }

    void begin_hotkey_capture(int target)
    {
        g_overlay_capture_target = target;
        g_overlay_capture_armed = false;
        g_overlay_dirty = true;
    }



    void initialize_overlay_virtual_cursor()
    {
        if (!g_game_window)
            return;

        RECT client{};
        if (!GetClientRect(g_game_window, &client))
            return;

        const int client_w = client.right - client.left;
        const int client_h = client.bottom - client.top;

        g_overlay_virtual_cursor_x =
            48.0f + static_cast<float>(
                std::min<UINT>(
                    kOverlayWidth,
                    client_w > 96 ? static_cast<UINT>(client_w - 96) : static_cast<UINT>(client_w))) * 0.5f;

        g_overlay_virtual_cursor_y =
            48.0f + static_cast<float>(
                std::min<UINT>(
                    kOverlayHeight,
                    client_h > 96 ? static_cast<UINT>(client_h - 96) : static_cast<UINT>(client_h))) * 0.5f;

        g_overlay_mouse_client_x =
            static_cast<int>(g_overlay_virtual_cursor_x);

        g_overlay_mouse_client_y =
            static_cast<int>(g_overlay_virtual_cursor_y);

        g_overlay_virtual_cursor_initialized = true;
        g_overlay_dirty = true;
    }

    void clamp_overlay_virtual_cursor()
    {
        if (!g_game_window)
            return;

        RECT client{};
        if (!GetClientRect(g_game_window, &client))
            return;

        const float max_x =
            static_cast<float>(
                std::max<LONG>(0, client.right - 1));

        const float max_y =
            static_cast<float>(
                std::max<LONG>(0, client.bottom - 1));

        g_overlay_virtual_cursor_x =
            std::clamp(
                g_overlay_virtual_cursor_x,
                0.0f,
                max_x);

        g_overlay_virtual_cursor_y =
            std::clamp(
                g_overlay_virtual_cursor_y,
                0.0f,
                max_y);

        g_overlay_mouse_client_x =
            static_cast<int>(g_overlay_virtual_cursor_x);

        g_overlay_mouse_client_y =
            static_cast<int>(g_overlay_virtual_cursor_y);
    }

    void update_overlay_hover_from_virtual_cursor()
    {
        int local_x = 0;
        int local_y = 0;

        g_overlay_mouse_inside =
            overlay_mouse_to_local(
                g_overlay_mouse_client_x,
                g_overlay_mouse_client_y,
                local_x,
                local_y);

        if (g_overlay_mouse_inside &&
            g_overlay_capture_target == 0)
        {
            const int row =
                overlay_row_from_local_y(local_y);

            if (row >= 0 && row != g_overlay_selection)
                g_overlay_selection = row;
        }

        g_overlay_dirty = true;
    }

    void handle_overlay_raw_mouse(HRAWINPUT handle)
    {
        if (!g_overlay_visible || !handle)
            return;

        UINT size = 0;

        if (GetRawInputData(
                handle,
                RID_INPUT,
                nullptr,
                &size,
                sizeof(RAWINPUTHEADER)) != 0 ||
            size == 0)
        {
            return;
        }

        BYTE stack_buffer[256]{};
        BYTE *buffer = stack_buffer;

        if (size > sizeof(stack_buffer))
        {
            buffer = new (std::nothrow) BYTE[size];

            if (!buffer)
                return;
        }

        UINT read_size = size;

        const UINT result =
            GetRawInputData(
                handle,
                RID_INPUT,
                buffer,
                &read_size,
                sizeof(RAWINPUTHEADER));

        if (result == static_cast<UINT>(-1) ||
            result != read_size)
        {
            if (buffer != stack_buffer)
                delete[] buffer;

            return;
        }

        RAWINPUT *raw =
            reinterpret_cast<RAWINPUT *>(buffer);

        if (raw->header.dwType == RIM_TYPEMOUSE)
        {
            if (!g_overlay_virtual_cursor_initialized)
                initialize_overlay_virtual_cursor();

            const RAWMOUSE &mouse = raw->data.mouse;

            if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
            {
                RECT client{};
                if (g_game_window && GetClientRect(g_game_window, &client))
                {
                    const float client_w =
                        static_cast<float>(client.right - client.left);

                    const float client_h =
                        static_cast<float>(client.bottom - client.top);

                    g_overlay_virtual_cursor_x =
                        (static_cast<float>(mouse.lLastX) / 65535.0f) * client_w;

                    g_overlay_virtual_cursor_y =
                        (static_cast<float>(mouse.lLastY) / 65535.0f) * client_h;
                }
            }
            else
            {
                constexpr float kRawMouseSensitivity = 1.0f;

                g_overlay_virtual_cursor_x +=
                    static_cast<float>(mouse.lLastX) *
                    kRawMouseSensitivity;

                g_overlay_virtual_cursor_y +=
                    static_cast<float>(mouse.lLastY) *
                    kRawMouseSensitivity;
            }

            clamp_overlay_virtual_cursor();
            update_overlay_hover_from_virtual_cursor();

            if ((mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0)
            {
                g_overlay_mouse_left_down = true;

                process_overlay_mouse_click(
                    g_overlay_mouse_client_x,
                    g_overlay_mouse_client_y);
            }

            if ((mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) != 0)
                g_overlay_mouse_left_down = false;
        }

        if (buffer != stack_buffer)
            delete[] buffer;
    }

    bool overlay_mouse_to_local(
        int client_x,
        int client_y,
        int &local_x,
        int &local_y)
    {
        if (!g_game_window)
            return false;

        RECT client{};
        if (!GetClientRect(g_game_window, &client))
            return false;

        const int client_w = client.right - client.left;
        const int client_h = client.bottom - client.top;

        const int draw_w =
            static_cast<int>(
                std::min<UINT>(
                    static_cast<UINT>(
                        (static_cast<unsigned long long>(kOverlayWidth) *
                         static_cast<unsigned long long>(g_menu_scale_percent)) /
                        100ull),
                    client_w > 96 ? static_cast<UINT>(client_w - 96) : static_cast<UINT>(client_w)));

        const int draw_h =
            static_cast<int>(
                std::min<UINT>(
                    static_cast<UINT>(
                        (static_cast<unsigned long long>(kOverlayHeight) *
                         static_cast<unsigned long long>(g_menu_scale_percent)) /
                        100ull),
                    client_h > 96 ? static_cast<UINT>(client_h - 96) : static_cast<UINT>(client_h)));

        const int left = 48;
        const int top = 48;

        if (client_x < left ||
            client_y < top ||
            client_x >= left + draw_w ||
            client_y >= top + draw_h)
        {
            return false;
        }

        local_x = static_cast<int>(
            (static_cast<long long>(client_x - left) * kOverlayWidth) /
            (draw_w > 0 ? draw_w : 1));

        local_y = static_cast<int>(
            (static_cast<long long>(client_y - top) * kOverlayHeight) /
            (draw_h > 0 ? draw_h : 1));

        return true;
    }

    int overlay_row_from_local_y(int y)
    {
        const int row_top[7] =
        {
            130, 182, 234, 286, 338, 390, 442
        };

        const int row_bottom[7] =
        {
            174, 226, 278, 330, 382, 434, 486
        };

        for (int i = 0; i < 7; ++i)
        {
            if (y >= row_top[i] && y <= row_bottom[i])
                return i;
        }

        return -1;
    }

    void activate_overlay_selection()
    {
        switch (g_overlay_selection)
        {
        case 0:
            apply_dlaa_enabled(
                !g_dlaa_injection_enabled,
                "menu");
            break;

        case 1:
            asi_bridge_v100::request_history_reset();
            log_line("DLAA history reset requested source=menu");
            break;

        case 2:
            cycle_menu_scale();
            break;

        case 3:
            begin_hotkey_capture(1);
            break;

        case 4:
            begin_hotkey_capture(2);
            break;

        case 5:
            g_menu_hotkey = VK_F7;
            g_toggle_dlaa_hotkey = VK_F8;

            g_menu_hotkey_wait_for_release =
                (GetAsyncKeyState(VK_F7) & 0x8000) != 0;

            g_toggle_hotkey_wait_for_release =
                (GetAsyncKeyState(VK_F8) & 0x8000) != 0;

            save_config();
            g_overlay_dirty = true;
            break;

        case 6:
            g_dlaa_injection_enabled = true;
            g_menu_hotkey = VK_F7;
            g_toggle_dlaa_hotkey = VK_F8;
            g_menu_scale_percent = 100;

            g_menu_hotkey_wait_for_release =
                (GetAsyncKeyState(VK_F7) & 0x8000) != 0;

            g_toggle_hotkey_wait_for_release =
                (GetAsyncKeyState(VK_F8) & 0x8000) != 0;

            asi_bridge_v100::request_history_reset();
            save_config();
            g_overlay_dirty = true;

            log_line("SETTINGS reset all defaults");
            break;
        }
    }

    void process_overlay_mouse_click(int client_x, int client_y)
    {
        if (!g_overlay_visible || g_overlay_capture_target != 0)
            return;

        int local_x = 0;
        int local_y = 0;

        if (!overlay_mouse_to_local(
                client_x,
                client_y,
                local_x,
                local_y))
        {
            return;
        }

        const int row = overlay_row_from_local_y(local_y);

        if (row >= 0)
        {
            g_overlay_selection = row;
            activate_overlay_selection();
            g_overlay_dirty = true;
        }
    }

    bool is_overlay_mouse_message(UINT message)
    {
        switch (message)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_INPUT:
            return true;

        default:
            return false;
        }
    }

    LRESULT CALLBACK overlay_window_proc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        if (message == WM_INPUT && g_overlay_visible)
        {
            handle_overlay_raw_mouse(
                reinterpret_cast<HRAWINPUT>(lParam));

            return 0;
        }

        if (message == WM_MOUSEMOVE)
        {
            g_overlay_mouse_client_x =
                static_cast<short>(LOWORD(lParam));

            g_overlay_mouse_client_y =
                static_cast<short>(HIWORD(lParam));

            if (g_overlay_visible)
            {
                g_overlay_virtual_cursor_x =
                    static_cast<float>(g_overlay_mouse_client_x);

                g_overlay_virtual_cursor_y =
                    static_cast<float>(g_overlay_mouse_client_y);

                g_overlay_virtual_cursor_initialized = true;
            }

            int local_x = 0;
            int local_y = 0;

            g_overlay_mouse_inside =
                overlay_mouse_to_local(
                    g_overlay_mouse_client_x,
                    g_overlay_mouse_client_y,
                    local_x,
                    local_y);

            if (g_overlay_visible &&
                g_overlay_mouse_inside &&
                g_overlay_capture_target == 0)
            {
                const int row =
                    overlay_row_from_local_y(local_y);

                if (row >= 0 && row != g_overlay_selection)
                {
                    g_overlay_selection = row;
                    g_overlay_dirty = true;
                }
            }

            if (g_overlay_visible)
                g_overlay_dirty = true;
        }
        else if (message == WM_LBUTTONDOWN)
        {
            g_overlay_mouse_left_down = true;

            if (g_overlay_visible)
            {
                process_overlay_mouse_click(
                    static_cast<short>(LOWORD(lParam)),
                    static_cast<short>(HIWORD(lParam)));

                return 0;
            }
        }
        else if (message == WM_LBUTTONUP)
        {
            g_overlay_mouse_left_down = false;

            if (g_overlay_visible)
                return 0;
        }

        if (g_overlay_visible && is_overlay_mouse_message(message))
        {
            return 0;
        }

        if (g_original_wndproc)
            return CallWindowProcW(
                g_original_wndproc,
                hwnd,
                message,
                wParam,
                lParam);

        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }

    void install_window_input_hook(HWND hwnd)
    {
        if (!hwnd || g_window_input_hooked)
            return;

        SetLastError(0);

        LONG_PTR previous =
            SetWindowLongPtrW(
                hwnd,
                GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&overlay_window_proc));

        if (previous == 0 && GetLastError() != 0)
        {
            log_line(
                "ERROR: failed to install menu input hook error=%lu",
                GetLastError());
            return;
        }

        g_game_window = hwnd;
        g_original_wndproc =
            reinterpret_cast<WNDPROC>(previous);

        g_window_input_hooked = true;

        log_line(
            "In-game menu keyboard/mouse input hook installed hwnd=%p",
            hwnd);
    }

    void process_overlay_input()
    {
        if (g_overlay_capture_target == 0 &&
            release_latched_hotkey_pressed(
                g_menu_hotkey,
                g_menu_hotkey_wait_for_release))
        {
            g_overlay_visible = !g_overlay_visible;
            g_overlay_dirty = true;

            if (g_overlay_visible)
            {
                g_overlay_virtual_cursor_initialized = false;
                initialize_overlay_virtual_cursor();
            }

            log_line(
                "MENU %s hotkey=%ls mouseCaptured=%d keyboardPassthrough=1 controllerPassthrough=1",
                g_overlay_visible ? "OPEN" : "CLOSED",
                hotkey_name(g_menu_hotkey).c_str(),
                g_overlay_visible ? 1 : 0);

            return;
        }

        if (!g_overlay_visible)
        {
            if (release_latched_hotkey_pressed(
                    g_toggle_dlaa_hotkey,
                    g_toggle_hotkey_wait_for_release))
            {
                apply_dlaa_enabled(
                    !g_dlaa_injection_enabled,
                    "hotkey");
            }

            return;
        }

        if (g_overlay_capture_target != 0)
        {
            if (!g_overlay_capture_armed)
            {
                if (!any_overlay_key_down())
                    g_overlay_capture_armed = true;
                return;
            }

            for (UINT vk = 1; vk < 256; ++vk)
            {
                const bool down =
                    (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;

                if (!down)
                    continue;

                if (vk == VK_ESCAPE)
                {
                    g_overlay_capture_target = 0;
                    g_overlay_capture_armed = false;
                    g_overlay_dirty = true;
                    return;
                }

                if (g_overlay_capture_target == 1)
                    g_menu_hotkey = vk;
                else if (g_overlay_capture_target == 2)
                    g_toggle_dlaa_hotkey = vk;

                g_overlay_capture_target = 0;
                g_overlay_capture_armed = false;

                if (g_menu_hotkey < 256)
                    g_overlay_key_prev[g_menu_hotkey] =
                        (GetAsyncKeyState(static_cast<int>(g_menu_hotkey)) & 0x8000) != 0;

                if (g_toggle_dlaa_hotkey < 256)
                    g_overlay_key_prev[g_toggle_dlaa_hotkey] =
                        (GetAsyncKeyState(static_cast<int>(g_toggle_dlaa_hotkey)) & 0x8000) != 0;

                g_menu_hotkey_wait_for_release =
                    (GetAsyncKeyState(static_cast<int>(g_menu_hotkey)) & 0x8000) != 0;

                g_toggle_hotkey_wait_for_release =
                    (GetAsyncKeyState(static_cast<int>(g_toggle_dlaa_hotkey)) & 0x8000) != 0;

                save_config();
                g_overlay_dirty = true;
                return;
            }

            return;
        }

        if (overlay_key_pressed(VK_ESCAPE))
        {
            g_overlay_visible = false;
            g_overlay_virtual_cursor_initialized = false;
            g_overlay_dirty = true;
            log_line("MENU CLOSED source=Escape");
            return;
        }

        if (overlay_key_pressed(VK_UP))
        {
            g_overlay_selection =
                (g_overlay_selection + 6) % 7;
            g_overlay_dirty = true;
        }

        if (overlay_key_pressed(VK_DOWN))
        {
            g_overlay_selection =
                (g_overlay_selection + 1) % 7;
            g_overlay_dirty = true;
        }

        if (overlay_key_pressed(VK_RETURN) || overlay_key_pressed(VK_SPACE))
            activate_overlay_selection();

        if (release_latched_hotkey_pressed(
                g_toggle_dlaa_hotkey,
                g_toggle_hotkey_wait_for_release))
        {
            apply_dlaa_enabled(
                !g_dlaa_injection_enabled,
                "hotkey");
        }
    }

    void release_overlay_resources()
    {
        if (g_overlay_srv) { g_overlay_srv->Release(); g_overlay_srv = nullptr; }
        if (g_overlay_texture) { g_overlay_texture->Release(); g_overlay_texture = nullptr; }
        if (g_overlay_blend_state) { g_overlay_blend_state->Release(); g_overlay_blend_state = nullptr; }
        if (g_overlay_depth_state) { g_overlay_depth_state->Release(); g_overlay_depth_state = nullptr; }
        if (g_overlay_rasterizer) { g_overlay_rasterizer->Release(); g_overlay_rasterizer = nullptr; }
        if (g_overlay_sampler) { g_overlay_sampler->Release(); g_overlay_sampler = nullptr; }
        if (g_overlay_ps) { g_overlay_ps->Release(); g_overlay_ps = nullptr; }
        if (g_overlay_vs) { g_overlay_vs->Release(); g_overlay_vs = nullptr; }
    }

    bool ensure_overlay_resources(ID3D11Device *device)
    {
        if (!device)
            return false;

        if (g_overlay_vs &&
            g_overlay_ps &&
            g_overlay_sampler &&
            g_overlay_rasterizer &&
            g_overlay_depth_state &&
            g_overlay_blend_state &&
            g_overlay_texture &&
            g_overlay_srv)
        {
            return true;
        }

        release_overlay_resources();

        static const char *vs_source =
            "struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
            "O main(uint id:SV_VertexID){"
            "float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};"
            "float2 u[3]={float2(0,1),float2(0,-1),float2(2,1)};"
            "O o;o.p=float4(p[id],0,1);o.uv=u[id];return o;}";

        static const char *ps_source =
            "Texture2D t0:register(t0);"
            "SamplerState s0:register(s0);"
            "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
            "return t0.Sample(s0,uv);}";

        ID3DBlob *vs_blob = nullptr;
        ID3DBlob *ps_blob = nullptr;
        ID3DBlob *error_blob = nullptr;

        HRESULT hr = D3DCompile(
            vs_source,
            std::strlen(vs_source),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "vs_4_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            &vs_blob,
            &error_blob);

        if (FAILED(hr))
        {
            log_line(
                "ERROR: overlay vertex shader compile failed hr=0x%08X%s%s",
                static_cast<uint32_t>(hr),
                error_blob ? " message=" : "",
                error_blob ? static_cast<const char *>(error_blob->GetBufferPointer()) : "");
            if (error_blob) error_blob->Release();
            return false;
        }

        if (error_blob) { error_blob->Release(); error_blob = nullptr; }

        hr = D3DCompile(
            ps_source,
            std::strlen(ps_source),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "ps_4_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            &ps_blob,
            &error_blob);

        if (FAILED(hr))
        {
            if (vs_blob) vs_blob->Release();
            if (error_blob) error_blob->Release();
            return false;
        }

        if (error_blob) { error_blob->Release(); error_blob = nullptr; }

        hr = device->CreateVertexShader(
            vs_blob->GetBufferPointer(),
            vs_blob->GetBufferSize(),
            nullptr,
            &g_overlay_vs);

        if (SUCCEEDED(hr))
        {
            hr = device->CreatePixelShader(
                ps_blob->GetBufferPointer(),
                ps_blob->GetBufferSize(),
                nullptr,
                &g_overlay_ps);
        }

        vs_blob->Release();
        ps_blob->Release();

        if (FAILED(hr))
        {
            log_line("ERROR: overlay shader creation failed hr=0x%08X", static_cast<uint32_t>(hr));
            release_overlay_resources();
            return false;
        }

        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;

        hr = device->CreateSamplerState(&sd, &g_overlay_sampler);
        if (FAILED(hr))
        {
            log_line("ERROR: overlay CreateSamplerState failed hr=0x%08X", static_cast<uint32_t>(hr));
            release_overlay_resources();
            return false;
        }

        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.FrontCounterClockwise = FALSE;
        rd.DepthClipEnable = TRUE;
        rd.ScissorEnable = FALSE;
        rd.MultisampleEnable = FALSE;
        rd.AntialiasedLineEnable = FALSE;

        hr = device->CreateRasterizerState(&rd, &g_overlay_rasterizer);
        if (FAILED(hr))
        {
            log_line("ERROR: overlay CreateRasterizerState failed hr=0x%08X", static_cast<uint32_t>(hr));
            release_overlay_resources();
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = FALSE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        dd.StencilEnable = FALSE;

        hr = device->CreateDepthStencilState(&dd, &g_overlay_depth_state);
        if (FAILED(hr))
        {
            log_line("ERROR: overlay CreateDepthStencilState failed hr=0x%08X", static_cast<uint32_t>(hr));
            release_overlay_resources();
            return false;
        }

        D3D11_BLEND_DESC bd{};
        bd.AlphaToCoverageEnable = FALSE;
        bd.IndependentBlendEnable = FALSE;
        bd.RenderTarget[0].BlendEnable = FALSE;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        hr = device->CreateBlendState(&bd, &g_overlay_blend_state);
        if (FAILED(hr))
        {
            log_line("ERROR: overlay CreateBlendState failed hr=0x%08X", static_cast<uint32_t>(hr));
            release_overlay_resources();
            return false;
        }

        D3D11_TEXTURE2D_DESC td{};
        td.Width = kOverlayWidth;
        td.Height = kOverlayHeight;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        hr = device->CreateTexture2D(&td, nullptr, &g_overlay_texture);
        if (SUCCEEDED(hr))
            hr = device->CreateShaderResourceView(
                g_overlay_texture,
                nullptr,
                &g_overlay_srv);

        if (FAILED(hr))
        {
            log_line("ERROR: overlay texture/SRV creation failed hr=0x%08X", static_cast<uint32_t>(hr));
            release_overlay_resources();
            return false;
        }

        if (!g_overlay_resources_logged)
        {
            g_overlay_resources_logged = true;
            log_line("MENU renderer resources ready texture=%ux%u", kOverlayWidth, kOverlayHeight);
        }

        g_overlay_dirty = true;
        return true;
    }

    void draw_overlay_text(
        HDC dc,
        HFONT font,
        int x,
        int y,
        int w,
        int h,
        const wchar_t *text,
        COLORREF color,
        UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
    {
        HFONT old_font = static_cast<HFONT>(SelectObject(dc, font));
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);

        RECT r{x, y, x + w, y + h};
        DrawTextW(dc, text, -1, &r, format);

        SelectObject(dc, old_font);
    }

    void fill_overlay_rect(
        HDC dc,
        int left,
        int top,
        int right,
        int bottom,
        COLORREF color)
    {
        RECT r{left, top, right, bottom};
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &r, brush);
        DeleteObject(brush);
    }

    bool update_overlay_texture(ID3D11DeviceContext *ctx)
    {
        if (!ctx || !g_overlay_texture)
            return false;

        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);

        if (!dc)
            return false;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(kOverlayWidth);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(kOverlayHeight);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void *bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(
            dc,
            &bmi,
            DIB_RGB_COLORS,
            &bits,
            nullptr,
            0);

        if (!bitmap || !bits)
        {
            if (bitmap) DeleteObject(bitmap);
            DeleteDC(dc);
            return false;
        }

        HGDIOBJ old_bitmap = SelectObject(dc, bitmap);

        fill_overlay_rect(
            dc,
            0,
            0,
            static_cast<int>(kOverlayWidth),
            static_cast<int>(kOverlayHeight),
            RGB(20, 20, 22));

        fill_overlay_rect(dc, 0, 0, kOverlayWidth, 5, RGB(190, 190, 190));

        HFONT title_font = CreateFontW(
            36, 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");

        HFONT normal_font = CreateFontW(
            25, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");

        HFONT small_font = CreateFontW(
            20, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");

        const COLORREF white = RGB(235, 235, 235);
        const COLORREF muted = RGB(165, 165, 170);
        const COLORREF highlight = RGB(62, 62, 68);
        const COLORREF enabled = RGB(190, 235, 190);

        draw_overlay_text(
            dc, title_font,
            34, 22, 650, 48,
            L"ALIEN: ISOLATION DLAA",
            white);

        draw_overlay_text(
            dc, small_font,
            36, 72, 640, 28,
            L"Version 1.0",
            muted);

        fill_overlay_rect(dc, 32, 112, 688, 115, RGB(65, 65, 70));

        const int row_y[7] = {132, 184, 236, 288, 340, 392, 444};

        for (int i = 0; i < 7; ++i)
        {
            if (g_overlay_selection == i)
                fill_overlay_rect(dc, 26, row_y[i] - 5, 694, row_y[i] + 40, highlight);
        }

        draw_overlay_text(
            dc, normal_font,
            40, row_y[0], 390, 34,
            L"Enable DLAA",
            white);

        draw_overlay_text(
            dc, normal_font,
            465, row_y[0], 190, 34,
            g_dlaa_injection_enabled ? L"ON" : L"OFF",
            g_dlaa_injection_enabled ? enabled : muted,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        draw_overlay_text(
            dc, normal_font,
            40, row_y[1], 615, 34,
            L"Reset DLAA History",
            white);

        draw_overlay_text(
            dc, normal_font,
            40, row_y[2], 390, 34,
            L"Menu Scale",
            white);

        wchar_t scale_text[32]{};
        _snwprintf_s(
            scale_text,
            _countof(scale_text),
            _TRUNCATE,
            L"%d%%",
            g_menu_scale_percent);

        draw_overlay_text(
            dc, normal_font,
            465, row_y[2], 190, 34,
            scale_text,
            white,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        draw_overlay_text(
            dc, normal_font,
            40, row_y[3], 390, 34,
            L"Open/Close Menu",
            white);

        const std::wstring menu_key_text =
            g_overlay_capture_target == 1
                ? L"Press any key..."
                : hotkey_name(g_menu_hotkey);

        draw_overlay_text(
            dc, normal_font,
            430, row_y[3], 225, 34,
            menu_key_text.c_str(),
            white,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        draw_overlay_text(
            dc, normal_font,
            40, row_y[4], 390, 34,
            L"Toggle DLAA",
            white);

        const std::wstring toggle_key_text =
            g_overlay_capture_target == 2
                ? L"Press any key..."
                : hotkey_name(g_toggle_dlaa_hotkey);

        draw_overlay_text(
            dc, normal_font,
            430, row_y[4], 225, 34,
            toggle_key_text.c_str(),
            white,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        draw_overlay_text(
            dc, normal_font,
            40, row_y[5], 615, 34,
            L"Reset Hotkeys to Defaults",
            white);

        draw_overlay_text(
            dc, normal_font,
            40, row_y[6], 615, 34,
            L"Reset All Settings",
            white);

        fill_overlay_rect(dc, 32, 486, 688, 489, RGB(65, 65, 70));

        draw_overlay_text(
            dc, normal_font,
            36, 468, 640, 26,
            L"Created by Gametism",
            white,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (g_overlay_mouse_inside)
        {
            int mouse_x = 0;
            int mouse_y = 0;

            if (overlay_mouse_to_local(
                    g_overlay_mouse_client_x,
                    g_overlay_mouse_client_y,
                    mouse_x,
                    mouse_y))
            {
                POINT cursor_points[3] =
                {
                    { mouse_x, mouse_y },
                    { mouse_x + 13, mouse_y + 30 },
                    { mouse_x + 20, mouse_y + 18 }
                };

                HBRUSH cursor_brush =
                    CreateSolidBrush(RGB(245, 245, 245));

                HPEN cursor_pen =
                    CreatePen(PS_SOLID, 2, RGB(20, 20, 20));

                HGDIOBJ old_brush =
                    SelectObject(dc, cursor_brush);

                HGDIOBJ old_pen =
                    SelectObject(dc, cursor_pen);

                Polygon(dc, cursor_points, 3);

                SelectObject(dc, old_pen);
                SelectObject(dc, old_brush);
                DeleteObject(cursor_pen);
                DeleteObject(cursor_brush);
            }
        }

        DeleteObject(title_font);
        DeleteObject(normal_font);
        DeleteObject(small_font);

        ctx->UpdateSubresource(
            g_overlay_texture,
            0,
            nullptr,
            bits,
            kOverlayWidth * 4,
            0);

        SelectObject(dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);

        g_overlay_dirty = false;
        return true;
    }

    void render_overlay(IDXGISwapChain *swap_chain)
    {
        if (!g_overlay_visible || !swap_chain)
            return;

        ID3D11Device *device = nullptr;
        if (FAILED(swap_chain->GetDevice(
                __uuidof(ID3D11Device),
                reinterpret_cast<void **>(&device))) ||
            !device)
        {
            return;
        }

        ID3D11DeviceContext *ctx = nullptr;
        device->GetImmediateContext(&ctx);

        if (!ctx || !ensure_overlay_resources(device))
        {
            log_line("ERROR: MENU render resource initialization failed");
            if (ctx) ctx->Release();
            device->Release();
            return;
        }

        if (g_overlay_dirty && !update_overlay_texture(ctx))
        {
            log_line("ERROR: MENU texture update failed");
            ctx->Release();
            device->Release();
            return;
        }

        ID3D11Texture2D *backbuffer = nullptr;
        HRESULT hr = swap_chain->GetBuffer(
            0,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void **>(&backbuffer));

        if (FAILED(hr) || !backbuffer)
        {
            log_line("ERROR: MENU GetBuffer failed hr=0x%08X", static_cast<uint32_t>(hr));
            ctx->Release();
            device->Release();
            return;
        }

        D3D11_TEXTURE2D_DESC back_desc{};
        backbuffer->GetDesc(&back_desc);

        ID3D11RenderTargetView *back_rtv = nullptr;
        hr = device->CreateRenderTargetView(
            backbuffer,
            nullptr,
            &back_rtv);

        if (FAILED(hr) || !back_rtv)
        {
            log_line("ERROR: MENU backbuffer RTV creation failed hr=0x%08X fmt=%u",
                static_cast<uint32_t>(hr),
                static_cast<uint32_t>(back_desc.Format));
            backbuffer->Release();
            ctx->Release();
            device->Release();
            return;
        }

        ID3D11RenderTargetView *old_rtv = nullptr;
        ID3D11DepthStencilView *old_dsv = nullptr;
        ctx->OMGetRenderTargets(1, &old_rtv, &old_dsv);

        D3D11_VIEWPORT old_vp{};
        UINT vp_count = 1;
        ctx->RSGetViewports(&vp_count, &old_vp);

        ID3D11VertexShader *old_vs = nullptr;
        ID3D11PixelShader *old_ps = nullptr;
        ID3D11InputLayout *old_layout = nullptr;
        ID3D11ShaderResourceView *old_srv = nullptr;
        ID3D11SamplerState *old_sampler = nullptr;
        ID3D11RasterizerState *old_rs = nullptr;
        ID3D11DepthStencilState *old_depth = nullptr;
        ID3D11BlendState *old_blend = nullptr;
        UINT old_stencil_ref = 0;
        FLOAT old_blend_factor[4]{};
        UINT old_sample_mask = 0xFFFFFFFFu;
        D3D11_PRIMITIVE_TOPOLOGY old_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

        ctx->VSGetShader(&old_vs, nullptr, nullptr);
        ctx->PSGetShader(&old_ps, nullptr, nullptr);
        ctx->IAGetInputLayout(&old_layout);
        ctx->IAGetPrimitiveTopology(&old_topology);
        ctx->PSGetShaderResources(0, 1, &old_srv);
        ctx->PSGetSamplers(0, 1, &old_sampler);
        ctx->RSGetState(&old_rs);
        ctx->OMGetDepthStencilState(&old_depth, &old_stencil_ref);
        ctx->OMGetBlendState(&old_blend, old_blend_factor, &old_sample_mask);

        const UINT scaled_overlay_width =
            static_cast<UINT>(
                (static_cast<unsigned long long>(kOverlayWidth) *
                 static_cast<unsigned long long>(g_menu_scale_percent)) /
                100ull);

        const UINT scaled_overlay_height =
            static_cast<UINT>(
                (static_cast<unsigned long long>(kOverlayHeight) *
                 static_cast<unsigned long long>(g_menu_scale_percent)) /
                100ull);

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 48.0f;
        vp.TopLeftY = 48.0f;
        vp.Width = static_cast<float>(
            std::min<UINT>(
                scaled_overlay_width,
                back_desc.Width > 96 ? back_desc.Width - 96 : back_desc.Width));

        vp.Height = static_cast<float>(
            std::min<UINT>(
                scaled_overlay_height,
                back_desc.Height > 96 ? back_desc.Height - 96 : back_desc.Height));
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        ctx->OMSetRenderTargets(1, &back_rtv, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(g_overlay_rasterizer);
        ctx->OMSetDepthStencilState(g_overlay_depth_state, 0);
        const FLOAT overlay_blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        ctx->OMSetBlendState(g_overlay_blend_state, overlay_blend_factor, 0xFFFFFFFFu);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(g_overlay_vs, nullptr, 0);
        ctx->PSSetShader(g_overlay_ps, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &g_overlay_srv);
        ctx->PSSetSamplers(0, 1, &g_overlay_sampler);
        ctx->Draw(3, 0);

        if (!g_overlay_first_render_logged)
        {
            g_overlay_first_render_logged = true;
            log_line(
                "MENU first render submitted backbuffer=%ux%u viewport=%.0fx%.0f",
                back_desc.Width,
                back_desc.Height,
                vp.Width,
                vp.Height);
        }

        ID3D11ShaderResourceView *null_srv = nullptr;
        ctx->PSSetShaderResources(0, 1, &null_srv);

        ctx->OMSetRenderTargets(1, &old_rtv, old_dsv);

        if (vp_count > 0)
            ctx->RSSetViewports(1, &old_vp);

        ctx->RSSetState(old_rs);
        ctx->OMSetDepthStencilState(old_depth, old_stencil_ref);
        ctx->OMSetBlendState(old_blend, old_blend_factor, old_sample_mask);
        ctx->IASetInputLayout(old_layout);
        ctx->IASetPrimitiveTopology(old_topology);
        ctx->VSSetShader(old_vs, nullptr, 0);
        ctx->PSSetShader(old_ps, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &old_srv);
        ctx->PSSetSamplers(0, 1, &old_sampler);

        if (old_blend) old_blend->Release();
        if (old_depth) old_depth->Release();
        if (old_rs) old_rs->Release();
        if (old_sampler) old_sampler->Release();
        if (old_srv) old_srv->Release();
        if (old_layout) old_layout->Release();
        if (old_ps) old_ps->Release();
        if (old_vs) old_vs->Release();
        if (old_dsv) old_dsv->Release();
        if (old_rtv) old_rtv->Release();

        back_rtv->Release();
        backbuffer->Release();
        ctx->Release();
        device->Release();
    }

    HRESULT STDMETHODCALLTYPE hook_present(
        IDXGISwapChain *swap_chain,
        UINT sync_interval,
        UINT flags)
    {
        process_overlay_input();

        if (g_overlay_visible)
            render_overlay(swap_chain);

        return g_orig_present(
            swap_chain,
            sync_interval,
            flags);
    }

    void hook_swap_chain(IDXGISwapChain *swap_chain)
    {
        if (!swap_chain)
            return;

        DXGI_SWAP_CHAIN_DESC swap_desc{};
        if (SUCCEEDED(swap_chain->GetDesc(&swap_desc)) &&
            swap_desc.OutputWindow)
        {
            install_window_input_hook(swap_desc.OutputWindow);
        }

        if (g_present_hooked)
            return;

        if (patch_vtable(
                swap_chain,
                8,
                reinterpret_cast<void *>(&hook_present),
                reinterpret_cast<void **>(&g_orig_present)))
        {
            g_present_hooked = true;
            log_line("In-game configuration overlay Present hook installed.");
        }
        else
        {
            log_line("ERROR: failed to install in-game overlay Present hook.");
        }
    }

    bool read_dxbc_hash(const void *code, size_t code_size, DxbcHash &out)
    {
        if (code == nullptr || code_size < 20)
            return false;

        const auto *bytes = static_cast<const uint8_t *>(code);
        if (std::memcmp(bytes, "DXBC", 4) != 0)
            return false;

        std::memcpy(&out, bytes + 4, sizeof(out));
        return true;
    }

    bool same_hash(const DxbcHash &x, const DxbcHash &y)
    {
        return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d;
    }

    DxbcHash shader_hash(ID3D11VertexShader *shader)
    {
        auto it = g_vs_hashes.find(shader);
        return it != g_vs_hashes.end() ? it->second : DxbcHash{};
    }

    DxbcHash shader_hash(ID3D11PixelShader *shader)
    {
        auto it = g_ps_hashes.find(shader);
        return it != g_ps_hashes.end() ? it->second : DxbcHash{};
    }

    bool valid_hash(const DxbcHash &h)
    {
        return h.a != 0 || h.b != 0 || h.c != 0 || h.d != 0;
    }

    ShaderRole identify_vs(const void *code, size_t size)
    {
        DxbcHash h{};
        if (!read_dxbc_hash(code, size, h))
            return ShaderRole::none;

        if (same_hash(h, kVsSmaa))       return ShaderRole::vs_smaa;
        if (same_hash(h, kVsRgbmEncode)) return ShaderRole::vs_rgbm_encode;
        return ShaderRole::none;
    }

    ShaderRole identify_ps(const void *code, size_t size)
    {
        DxbcHash h{};
        if (!read_dxbc_hash(code, size, h))
            return ShaderRole::none;

        if (same_hash(h, kPsRgbmEncode))   return ShaderRole::ps_rgbm_encode;
        if (same_hash(h, kPsDofEncode))    return ShaderRole::ps_dof_encode;
        if (same_hash(h, kPsCameraMotion)) return ShaderRole::ps_camera_motion;
        return ShaderRole::none;
    }

    const char *role_name(ShaderRole r)
    {
        switch (r)
        {
        case ShaderRole::ps_rgbm_encode:   return "PS RGBM encode";
        case ShaderRole::ps_dof_encode:    return "PS DoF encode";
        case ShaderRole::ps_camera_motion: return "PS camera motion";
        case ShaderRole::vs_smaa:          return "VS SMAA";
        case ShaderRole::vs_rgbm_encode:   return "VS RGBM encode";
        default:                           return "none";
        }
    }


    const char *dxgi_format_name(DXGI_FORMAT fmt)
    {
        switch (fmt)
        {
        case DXGI_FORMAT_R16G16_FLOAT:       return "R16G16_FLOAT";
        case DXGI_FORMAT_R32_FLOAT:          return "R32_FLOAT";
        case DXGI_FORMAT_R11G11B10_FLOAT:    return "R11G11B10_FLOAT";
        case DXGI_FORMAT_R8G8B8A8_UNORM:     return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_B8G8R8A8_UNORM:     return "B8G8R8A8_UNORM";
        default:                             return "OTHER";
        }
    }

    bool get_texture2d_from_srv(ID3D11ShaderResourceView *srv, ID3D11Texture2D **out_tex)
    {
        if (out_tex == nullptr)
            return false;
        *out_tex = nullptr;

        if (srv == nullptr)
            return false;

        ID3D11Resource *res = nullptr;
        srv->GetResource(&res);
        if (res == nullptr)
            return false;

        HRESULT hr = res->QueryInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void **>(out_tex));
        res->Release();

        return SUCCEEDED(hr) && *out_tex != nullptr;
    }

    bool get_texture2d_from_rtv(ID3D11RenderTargetView *rtv, ID3D11Texture2D **out_tex)
    {
        if (out_tex == nullptr)
            return false;
        *out_tex = nullptr;

        if (rtv == nullptr)
            return false;

        ID3D11Resource *res = nullptr;
        rtv->GetResource(&res);
        if (res == nullptr)
            return false;

        HRESULT hr = res->QueryInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void **>(out_tex));
        res->Release();

        return SUCCEEDED(hr) && *out_tex != nullptr;
    }

    void log_texture(const char *label, ID3D11Texture2D *tex)
    {
        if (tex == nullptr)
        {
            log_line("  %s: null", label);
            return;
        }

        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);

        log_line(
            "  %s: tex=%p %ux%u fmt=%u(%s) bind=0x%X misc=0x%X samples=%u usage=%u",
            label,
            tex,
            d.Width,
            d.Height,
            static_cast<uint32_t>(d.Format),
            dxgi_format_name(d.Format),
            d.BindFlags,
            d.MiscFlags,
            d.SampleDesc.Count,
            static_cast<uint32_t>(d.Usage));
    }

    bool is_expected_velocity(ID3D11Texture2D *tex)
    {
        if (tex == nullptr)
            return false;

        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);

        return d.Width >= 640 &&
               d.Height >= 360 &&
               d.Format == DXGI_FORMAT_R16G16_FLOAT;
    }

    bool is_expected_depth(ID3D11Texture2D *tex)
    {
        if (tex == nullptr)
            return false;

        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);

        return d.Width >= 640 &&
               d.Height >= 360 &&
               d.Format == DXGI_FORMAT_R32_FLOAT;
    }

    bool is_expected_scene(ID3D11Texture2D *tex)
    {
        if (tex == nullptr)
            return false;

        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);

        return d.Width >= 640 &&
               d.Height >= 360 &&
               d.Format == DXGI_FORMAT_R11G11B10_FLOAT;
    }

    void log_known_texture_identity(const char *label, ID3D11Texture2D *tex);
    void start_resource_family_probe();
    void stop_resource_family_probe(const char *reason);
    void start_transition_probe();
    void stop_transition_probe(const char *reason);
    void log_transition_draw(ID3D11DeviceContext *ctx);
    void start_handoff42_probe();
    void stop_handoff42_probe(const char *reason);
    void log_handoff42_draw(ID3D11DeviceContext *ctx);
    bool ensure_primary_substitution_resources(ID3D11Device *device);
    void copyback_primary_substitution(ID3D11DeviceContext *ctx);
    void begin_resolution_architecture_capture();
    void stop_resolution_architecture_capture(const char *reason);
    void reset_geometry_cb_tracks();
    void log_geometry_cb_correlations(GeometryCbShaderTrack &track);
    void maybe_start_short_probe();
    void log_bound_resolution_state(ID3D11DeviceContext *ctx, const char *reason);

    void probe_camera_motion_resources(ID3D11DeviceContext *ctx)
    {
        if (ctx == nullptr)
            return;

        ID3D11RenderTargetView *rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);

        ID3D11ShaderResourceView *depth_srv = nullptr;
        ctx->PSGetShaderResources(8, 1, &depth_srv);

        ID3D11Texture2D *velocity = nullptr;
        ID3D11Texture2D *depth = nullptr;

        get_texture2d_from_rtv(rtv, &velocity);
        get_texture2d_from_srv(depth_srv, &depth);

        const bool velocity_ok = is_expected_velocity(velocity);
        const bool depth_ok = is_expected_depth(depth);

        const bool changed =
            velocity != g_last_velocity_tex ||
            depth != g_last_depth_tex;

        if (changed)
        {
            if (velocity)
            {
                if (g_probe_velocity) g_probe_velocity->Release();
                g_probe_velocity = velocity;
                g_probe_velocity->AddRef();
            }

            if (depth)
            {
                if (g_probe_depth) g_probe_depth->Release();
                g_probe_depth = depth;
                g_probe_depth->AddRef();
            }

            log_known_texture_identity("velocity", velocity);
            log_known_texture_identity("depth", depth);
            log_bound_resolution_state(ctx, "camera_motion");
            maybe_start_short_probe();
        }

        if (changed || g_render_frame <= 5 || (g_render_frame % 3600ull) == 0)
        {
            if (g_release_verbose_diagnostics)
            {
                log_line(
                "CAMERA_MOTION_RESOURCES frame=%llu velocity_ok=%d depth_ok=%d changed=%d",
                static_cast<unsigned long long>(g_render_frame),
                velocity_ok ? 1 : 0,
                depth_ok ? 1 : 0,
                changed ? 1 : 0);
            }

            if (g_release_verbose_diagnostics)
            {
                log_texture("velocity RTV0", velocity);
                log_texture("depth PS t8", depth);
            }

            g_last_camera_motion_logged = g_render_frame;
        }

        set_tracked_texture(g_last_velocity_tex, velocity);
        set_tracked_texture(g_last_depth_tex, depth);

        if (velocity)
            velocity->Release();
        if (depth)
            depth->Release();
        if (rtv)
            rtv->Release();
        if (dsv)
            dsv->Release();
        if (depth_srv)
            depth_srv->Release();
    }

    void probe_rgbm_scene_resource(ID3D11DeviceContext *ctx)
    {
        if (ctx == nullptr)
            return;

        ID3D11ShaderResourceView *scene_srv = nullptr;
        ctx->PSGetShaderResources(0, 1, &scene_srv);

        ID3D11Texture2D *scene = nullptr;
        get_texture2d_from_srv(scene_srv, &scene);

        const bool scene_ok = is_expected_scene(scene);
        const bool changed = scene != g_last_scene_tex;

        if (changed)
        {
            if (scene)
            {
                if (g_probe_scene_color) g_probe_scene_color->Release();
                g_probe_scene_color = scene;
                g_probe_scene_color->AddRef();
            }

            log_known_texture_identity("scene_color", scene);
            log_bound_resolution_state(ctx, "rgbm_scene");
            maybe_start_short_probe();
        }

        if (changed || g_render_frame <= 5 || (g_render_frame % 3600ull) == 0)
        {
            if (g_release_verbose_diagnostics)
            {
                log_line(
                "RGBM_SCENE_RESOURCE frame=%llu scene_ok=%d changed=%d",
                static_cast<unsigned long long>(g_render_frame),
                scene_ok ? 1 : 0,
                changed ? 1 : 0);
            }

            if (g_release_verbose_diagnostics)
                log_texture("main HDR scene PS t0", scene);

            g_last_rgbm_logged = g_render_frame;
        }

        g_last_scene_tex = scene;

        if (scene)
            scene->Release();
        if (scene_srv)
            scene_srv->Release();
    }


    bool short_probe_should_log()
    {
        return g_short_probe_active &&
               g_render_frame >= g_short_probe_start_frame &&
               g_render_frame <= g_short_probe_end_frame;
    }

    const char *probe_role_for_texture(ID3D11Texture2D *tex)
    {
        if (!tex) return nullptr;
        if (tex == g_probe_scene_color) return "scene_color";
        if (tex == g_probe_velocity) return "velocity";
        if (tex == g_probe_depth) return "depth";
        return nullptr;
    }

    ID3D11Texture2D *resource_to_texture2d(ID3D11Resource *resource)
    {
        if (!resource)
            return nullptr;

        ID3D11Texture2D *tex = nullptr;
        resource->QueryInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void **>(&tex));
        return tex;
    }

    ID3D11Texture2D *srv_to_texture2d(ID3D11ShaderResourceView *srv)
    {
        if (!srv)
            return nullptr;

        ID3D11Resource *res = nullptr;
        srv->GetResource(&res);

        ID3D11Texture2D *tex = resource_to_texture2d(res);
        if (res) res->Release();
        return tex;
    }

    ID3D11Texture2D *dsv_to_texture2d(ID3D11DepthStencilView *dsv)
    {
        if (!dsv)
            return nullptr;

        ID3D11Resource *res = nullptr;
        dsv->GetResource(&res);

        ID3D11Texture2D *tex = resource_to_texture2d(res);
        if (res) res->Release();
        return tex;
    }

    const TextureProbeInfo *lookup_texture_probe(ID3D11Texture2D *tex);

    uint64_t texture_create_serial(ID3D11Texture2D *tex)
    {
        const TextureProbeInfo *info = lookup_texture_probe(tex);
        return info ? info->serial : 0ull;
    }

    void set_probe_target_texture(
        ID3D11Texture2D *&slot,
        ID3D11Texture2D *tex)
    {
        if (slot == tex)
            return;

        if (slot)
            slot->Release();

        slot = tex;

        if (slot)
            slot->AddRef();
    }

    bool is_target_probe_texture(ID3D11Texture2D *tex)
    {
        return tex != nullptr &&
            (tex == g_target_half_color ||
             tex == g_target_half_depth);
    }

    const char *target_probe_name(ID3D11Texture2D *tex)
    {
        if (tex == g_target_half_color) return "half_color";
        if (tex == g_target_half_depth) return "half_depth";
        return "-";
    }

    void consider_halfres_generation_candidate(
        ID3D11Texture2D *tex,
        const D3D11_TEXTURE2D_DESC &d,
        uint64_t serial)
    {
        if (!tex)
            return;

        const bool half_color_candidate =
            d.Width == 1920 &&
            d.Height == 1080 &&
            d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
            d.BindFlags == (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) &&
            d.MiscFlags == 0 &&
            d.SampleDesc.Count == 1;

        const bool half_depth_candidate =
            d.Width == 1920 &&
            d.Height == 1080 &&
            d.Format == DXGI_FORMAT_R32_FLOAT &&
            d.BindFlags == (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) &&
            d.MiscFlags == 0 &&
            d.SampleDesc.Count == 1;

        if (half_color_candidate)
        {
            set_probe_target_texture(g_pending_half_color, tex);
            g_pending_half_color_serial = serial;

            if (g_release_verbose_diagnostics)
            {
                log_line(
                "PASS_ID CANDIDATE half_color serial=%llu tex=%p",
                static_cast<unsigned long long>(serial),
                tex);
            }
            return;
        }

        if (half_depth_candidate &&
            g_pending_half_color != nullptr &&
            g_pending_half_color_serial != 0 &&
            serial == g_pending_half_color_serial + 4ull)
        {
            set_probe_target_texture(g_target_half_color, g_pending_half_color);
            set_probe_target_texture(g_target_half_depth, tex);

            g_target_half_color_serial = g_pending_half_color_serial;
            g_target_half_depth_serial = serial;
            ++g_target_generation;

            if (g_release_verbose_diagnostics)
            {
                log_line(
                "PASS_ID GENERATION generation=%llu colorSerial=%llu colorTex=%p depthSerial=%llu depthTex=%p",
                static_cast<unsigned long long>(g_target_generation),
                static_cast<unsigned long long>(g_target_half_color_serial),
                g_target_half_color,
                static_cast<unsigned long long>(g_target_half_depth_serial),
                g_target_half_depth);
            }

            set_probe_target_texture(g_pending_half_color, nullptr);
            g_pending_half_color_serial = 0;
        }
        else if (half_depth_candidate &&
                 g_pending_half_color_serial != 0 &&
                 serial > g_pending_half_color_serial + 4ull)
        {
            set_probe_target_texture(g_pending_half_color, nullptr);
            g_pending_half_color_serial = 0;
        }
    }

    bool is_halfres_texture(ID3D11Texture2D *tex, D3D11_TEXTURE2D_DESC *out_desc = nullptr)
    {
        if (!tex)
            return false;

        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);

        if (out_desc)
            *out_desc = d;

        return d.Width == 1920 && d.Height == 1080;
    }

    void log_halfres_texture(
        const char *kind,
        ID3D11Texture2D *tex,
        uint64_t frame,
        uint64_t draw,
        int slot,
        ShaderRole vs_role,
        ShaderRole ps_role)
    {
        if (!tex || !short_probe_should_log())
            return;

        if (g_short_probe_events >= HALFRES_PROBE_EVENT_CAP)
            return;

        D3D11_TEXTURE2D_DESC d{};
        if (!is_halfres_texture(tex, &d))
            return;

        const TextureProbeInfo *info = lookup_texture_probe(tex);

        ++g_short_probe_events;

        log_line(
            "HALFRES_ROLE %s frame=%llu draw=%llu slot=%d tex=%p createSerial=%llu fmt=%u bind=0x%X misc=0x%X VSrole=%d PSrole=%d",
            kind,
            static_cast<unsigned long long>(frame),
            static_cast<unsigned long long>(draw),
            slot,
            tex,
            static_cast<unsigned long long>(info ? info->serial : 0ull),
            static_cast<uint32_t>(d.Format),
            d.BindFlags,
            d.MiscFlags,
            static_cast<int>(vs_role),
            static_cast<int>(ps_role));
    }

    void stop_short_probe_if_capped()
    {
        if (g_short_probe_active &&
            g_short_probe_events >= HALFRES_PROBE_EVENT_CAP)
        {
            g_short_probe_active = false;
            g_short_probe_completed = true;

            if (g_release_verbose_diagnostics)
            {
                log_line(
                "SHORT_PROBE STOP event-cap frame=%llu events=%llu sceneInputs=%llu halfTargets=%llu",
                static_cast<unsigned long long>(g_render_frame),
                static_cast<unsigned long long>(g_short_probe_events),
                static_cast<unsigned long long>(g_halfres_scene_input_events),
                static_cast<unsigned long long>(g_halfres_target_events));
            }
        }
    }

    ShaderRole current_vs_role();
    ShaderRole current_ps_role();

    void log_consumer_resource_slot(
        const char *pass_name,
        uint64_t frame,
        uint64_t draw,
        UINT slot,
        ID3D11ShaderResourceView *srv)
    {
        if (!srv)
            return;

        ID3D11Texture2D *tex = srv_to_texture2d(srv);
        if (!tex)
            return;

        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        const TextureProbeInfo *info = lookup_texture_probe(tex);

        log_line(
            "CONSUMER_SLOT pass=%s frame=%llu draw=%llu slot=%u tex=%p serial=%llu %ux%u fmt=%u bind=0x%X misc=0x%X target=%s",
            pass_name,
            static_cast<unsigned long long>(frame),
            static_cast<unsigned long long>(draw),
            slot,
            tex,
            static_cast<unsigned long long>(info ? info->serial : 0ull),
            d.Width,
            d.Height,
            static_cast<uint32_t>(d.Format),
            d.BindFlags,
            d.MiscFlags,
            target_probe_name(tex));

        tex->Release();
    }

    void log_consumer_pass(
        ID3D11DeviceContext *ctx,
        const char *pass_name,
        ID3D11ShaderResourceView *const *srvs,
        UINT srv_count)
    {
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ctx->IAGetPrimitiveTopology(&topology);

        D3D11_VIEWPORT vp{};
        UINT nvp = 1;
        ctx->RSGetViewports(&nvp, &vp);

        ID3D11RenderTargetView *rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);

        ID3D11Texture2D *rt = nullptr;
        ID3D11Texture2D *dt = nullptr;

        if (rtv)
            get_texture2d_from_rtv(rtv, &rt);

        if (dsv)
            dt = dsv_to_texture2d(dsv);

        D3D11_TEXTURE2D_DESC rd{}, dd{};
        if (rt) rt->GetDesc(&rd);
        if (dt) dt->GetDesc(&dd);

        const TextureProbeInfo *ri = lookup_texture_probe(rt);
        const TextureProbeInfo *di = lookup_texture_probe(dt);

        const DxbcHash vsh = shader_hash(g_current_vs);
        const DxbcHash psh = shader_hash(g_current_ps);

        log_line(
            "CONSUMER_PASS %s generation=%llu frame=%llu draw=%llu kind=%s count=%u start=%u base=%d topology=%u viewport=%.0fx%.0f VS=%p VSHASH=%08X-%08X-%08X-%08X VSrole=%d PS=%p PSHASH=%08X-%08X-%08X-%08X PSrole=%d RTV=%p serial=%llu %ux%u fmt=%u DSV=%p serial=%llu %ux%u fmt=%u",
            pass_name,
            static_cast<unsigned long long>(g_target_generation),
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_draw_counter),
            g_probe_draw_kind,
            g_probe_draw_count,
            g_probe_draw_start,
            g_probe_draw_base_vertex,
            static_cast<uint32_t>(topology),
            vp.Width,
            vp.Height,
            g_current_vs,
            vsh.a, vsh.b, vsh.c, vsh.d,
            static_cast<int>(current_vs_role()),
            g_current_ps,
            psh.a, psh.b, psh.c, psh.d,
            static_cast<int>(current_ps_role()),
            rt,
            static_cast<unsigned long long>(ri ? ri->serial : 0ull),
            rd.Width,
            rd.Height,
            static_cast<uint32_t>(rd.Format),
            dt,
            static_cast<unsigned long long>(di ? di->serial : 0ull),
            dd.Width,
            dd.Height,
            static_cast<uint32_t>(dd.Format));

        for (UINT slot = 0; slot < srv_count; ++slot)
            log_consumer_resource_slot(
                pass_name,
                g_render_frame,
                g_draw_counter,
                slot,
                srvs[slot]);

        if (rt) rt->Release();
        if (dt) dt->Release();
        if (rtv) rtv->Release();
        if (dsv) dsv->Release();
    }

    void log_half_color_producer_slot(
        uint64_t frame,
        uint64_t draw,
        UINT slot,
        ID3D11ShaderResourceView *srv)
    {
        if (!srv)
            return;

        ID3D11Texture2D *tex = srv_to_texture2d(srv);
        if (!tex)
            return;

        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        const TextureProbeInfo *info = lookup_texture_probe(tex);

        log_line(
            "PRODUCER_SLOT frame=%llu draw=%llu slot=%u tex=%p serial=%llu %ux%u fmt=%u bind=0x%X misc=0x%X target=%s",
            static_cast<unsigned long long>(frame),
            static_cast<unsigned long long>(draw),
            slot,
            tex,
            static_cast<unsigned long long>(info ? info->serial : 0ull),
            d.Width,
            d.Height,
            static_cast<uint32_t>(d.Format),
            d.BindFlags,
            d.MiscFlags,
            target_probe_name(tex));

        tex->Release();
    }

    void log_half_color_producer(ID3D11DeviceContext *ctx)
    {
        if (!ctx || !g_target_half_color)
            return;

        ID3D11RenderTargetView *rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);

        ID3D11Texture2D *rt = nullptr;
        ID3D11Texture2D *dt = nullptr;

        if (rtv)
            get_texture2d_from_rtv(rtv, &rt);

        if (dsv)
            dt = dsv_to_texture2d(dsv);

        if (rt != g_target_half_color)
        {
            if (rt) rt->Release();
            if (dt) dt->Release();
            if (rtv) rtv->Release();
            if (dsv) dsv->Release();
            return;
        }

        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ctx->IAGetPrimitiveTopology(&topology);

        D3D11_VIEWPORT vp{};
        UINT nvp = 1;
        ctx->RSGetViewports(&nvp, &vp);

        D3D11_TEXTURE2D_DESC rd{}, dd{};
        rt->GetDesc(&rd);
        if (dt) dt->GetDesc(&dd);

        const TextureProbeInfo *ri = lookup_texture_probe(rt);
        const TextureProbeInfo *di = lookup_texture_probe(dt);

        const DxbcHash vsh = shader_hash(g_current_vs);
        const DxbcHash psh = shader_hash(g_current_ps);

        const uint64_t ps_seen =
            g_current_ps ? ++g_half_color_ps_hits[g_current_ps] : 0ull;
        const uint64_t vs_seen =
            g_current_vs ? ++g_half_color_vs_hits[g_current_vs] : 0ull;

        ++g_half_color_producer_hits;
        ++g_short_probe_events;

        log_line(
            "PRODUCER_PASS generation=%llu frame=%llu draw=%llu hit=%llu kind=%s count=%u start=%u base=%d topology=%u viewport=%.0fx%.0f VS=%p VSHASH=%08X-%08X-%08X-%08X VSseen=%llu VSrole=%d PS=%p PSHASH=%08X-%08X-%08X-%08X PSseen=%llu PSrole=%d RTV=%p serial=%llu %ux%u fmt=%u DSV=%p serial=%llu %ux%u fmt=%u",
            static_cast<unsigned long long>(g_target_generation),
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_draw_counter),
            static_cast<unsigned long long>(g_half_color_producer_hits),
            g_probe_draw_kind,
            g_probe_draw_count,
            g_probe_draw_start,
            g_probe_draw_base_vertex,
            static_cast<uint32_t>(topology),
            vp.Width,
            vp.Height,
            g_current_vs,
            vsh.a, vsh.b, vsh.c, vsh.d,
            static_cast<unsigned long long>(vs_seen),
            static_cast<int>(current_vs_role()),
            g_current_ps,
            psh.a, psh.b, psh.c, psh.d,
            static_cast<unsigned long long>(ps_seen),
            static_cast<int>(current_ps_role()),
            rt,
            static_cast<unsigned long long>(ri ? ri->serial : 0ull),
            rd.Width,
            rd.Height,
            static_cast<uint32_t>(rd.Format),
            dt,
            static_cast<unsigned long long>(di ? di->serial : 0ull),
            dd.Width,
            dd.Height,
            static_cast<uint32_t>(dd.Format));

        ID3D11ShaderResourceView *srvs[16]{};
        ctx->PSGetShaderResources(0, 16, srvs);

        for (UINT slot = 0; slot < 16; ++slot)
        {
            if (srvs[slot])
                log_half_color_producer_slot(
                    g_render_frame,
                    g_draw_counter,
                    slot,
                    srvs[slot]);

            if (srvs[slot])
                srvs[slot]->Release();
        }

        if (rt) rt->Release();
        if (dt) dt->Release();
        if (rtv) rtv->Release();
        if (dsv) dsv->Release();
    }

    void reset_scene_boundary_probe_state()
    {
        for (auto &entry : g_scene_writer_ring)
            entry = {};

        g_scene_writer_ring_next = 0;
        g_scene_writer_ring_count = 0;
        g_scene_post_handoff_active = false;
        g_scene_post_handoff_frame = 0;
        g_scene_post_consumer_count = 0;
        g_scene_boundary_handoffs = 0;
    }

    void snapshot_bound_ps_inputs(
        ID3D11DeviceContext *ctx,
        SceneInputSnapshot (&inputs)[16])
    {
        ID3D11ShaderResourceView *srvs[16]{};
        ctx->PSGetShaderResources(0, 16, srvs);

        for (UINT slot = 0; slot < 16; ++slot)
        {
            if (srvs[slot])
            {
                ID3D11Texture2D *tex = srv_to_texture2d(srvs[slot]);

                if (tex)
                {
                    D3D11_TEXTURE2D_DESC d{};
                    tex->GetDesc(&d);
                    const TextureProbeInfo *info = lookup_texture_probe(tex);

                    inputs[slot].bound = true;
                    inputs[slot].serial = info ? info->serial : 0ull;
                    inputs[slot].width = d.Width;
                    inputs[slot].height = d.Height;
                    inputs[slot].format = static_cast<uint32_t>(d.Format);

                    tex->Release();
                }

                srvs[slot]->Release();
            }
        }
    }

    void capture_scene_color_writer(ID3D11DeviceContext *ctx)
    {
        if (!short_probe_should_log() ||
            !ctx ||
            !g_probe_scene_color)
        {
            return;
        }

        ID3D11RenderTargetView *rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);

        ID3D11Texture2D *rt = nullptr;
        ID3D11Texture2D *dt = nullptr;

        if (rtv)
            get_texture2d_from_rtv(rtv, &rt);

        if (dsv)
            dt = dsv_to_texture2d(dsv);

        const bool writes_scene =
            rt != nullptr &&
            rt == g_probe_scene_color;

        if (!writes_scene)
        {
            if (rt) rt->Release();
            if (dt) dt->Release();
            if (rtv) rtv->Release();
            if (dsv) dsv->Release();
            return;
        }

        SceneWriterSnapshot rec{};
        rec.valid = true;
        rec.frame = g_render_frame;
        rec.draw = g_draw_counter;
        rec.kind = g_probe_draw_kind;
        rec.count = g_probe_draw_count;
        rec.start = g_probe_draw_start;
        rec.base = g_probe_draw_base_vertex;

        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ctx->IAGetPrimitiveTopology(&topology);
        rec.topology = static_cast<uint32_t>(topology);

        D3D11_VIEWPORT vp{};
        UINT nvp = 1;
        ctx->RSGetViewports(&nvp, &vp);
        rec.viewport_w = vp.Width;
        rec.viewport_h = vp.Height;

        rec.vs = g_current_vs;
        rec.vs_hash = shader_hash(g_current_vs);
        rec.vs_role = static_cast<int>(current_vs_role());

        rec.ps = g_current_ps;
        rec.ps_hash = shader_hash(g_current_ps);
        rec.ps_role = static_cast<int>(current_ps_role());

        if (dt)
        {
            D3D11_TEXTURE2D_DESC dd{};
            dt->GetDesc(&dd);
            const TextureProbeInfo *di = lookup_texture_probe(dt);

            rec.dsv_serial = di ? di->serial : 0ull;
            rec.dsv_width = dd.Width;
            rec.dsv_height = dd.Height;
            rec.dsv_format = static_cast<uint32_t>(dd.Format);
        }

        snapshot_bound_ps_inputs(ctx, rec.inputs);

        g_scene_writer_ring[g_scene_writer_ring_next] = rec;
        g_scene_writer_ring_next =
            (g_scene_writer_ring_next + 1u) % SCENE_WRITER_RING_SIZE;

        if (g_scene_writer_ring_count < SCENE_WRITER_RING_SIZE)
            ++g_scene_writer_ring_count;

        if (rt) rt->Release();
        if (dt) dt->Release();
        if (rtv) rtv->Release();
        if (dsv) dsv->Release();
    }

    void log_scene_writer_ring()
    {
        if (!short_probe_should_log())
            return;

        const uint32_t count = g_scene_writer_ring_count;
        const uint32_t oldest =
            count < SCENE_WRITER_RING_SIZE
                ? 0u
                : g_scene_writer_ring_next;

        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t index =
                (oldest + i) % SCENE_WRITER_RING_SIZE;

            const SceneWriterSnapshot &rec =
                g_scene_writer_ring[index];

            if (!rec.valid)
                continue;

            if (g_short_probe_events >= SCENE_BOUNDARY_EVENT_CAP)
                return;

            ++g_short_probe_events;

            log_line(
                "SCENE_PRE_WRITER order=%u frame=%llu draw=%llu kind=%s count=%u start=%u base=%d topology=%u viewport=%.0fx%.0f VS=%p VSHASH=%08X-%08X-%08X-%08X VSrole=%d PS=%p PSHASH=%08X-%08X-%08X-%08X PSrole=%d DSVserial=%llu %ux%u fmt=%u",
                i,
                static_cast<unsigned long long>(rec.frame),
                static_cast<unsigned long long>(rec.draw),
                rec.kind,
                rec.count,
                rec.start,
                rec.base,
                rec.topology,
                rec.viewport_w,
                rec.viewport_h,
                rec.vs,
                rec.vs_hash.a, rec.vs_hash.b,
                rec.vs_hash.c, rec.vs_hash.d,
                rec.vs_role,
                rec.ps,
                rec.ps_hash.a, rec.ps_hash.b,
                rec.ps_hash.c, rec.ps_hash.d,
                rec.ps_role,
                static_cast<unsigned long long>(rec.dsv_serial),
                rec.dsv_width,
                rec.dsv_height,
                rec.dsv_format);

            for (UINT slot = 0; slot < 16; ++slot)
            {
                if (!rec.inputs[slot].bound)
                    continue;

                if (g_short_probe_events >= SCENE_BOUNDARY_EVENT_CAP)
                    return;

                ++g_short_probe_events;

                log_line(
                    "SCENE_PRE_INPUT order=%u draw=%llu slot=%u serial=%llu %ux%u fmt=%u",
                    i,
                    static_cast<unsigned long long>(rec.draw),
                    slot,
                    static_cast<unsigned long long>(rec.inputs[slot].serial),
                    rec.inputs[slot].width,
                    rec.inputs[slot].height,
                    rec.inputs[slot].format);
            }
        }
    }

    bool draw_samples_scene_color(
        ID3D11DeviceContext *ctx,
        UINT *first_slot = nullptr)
    {
        if (!ctx || !g_probe_scene_color)
            return false;

        ID3D11ShaderResourceView *srvs[16]{};
        ctx->PSGetShaderResources(0, 16, srvs);

        bool found = false;
        UINT slot_found = 0;

        for (UINT slot = 0; slot < 16; ++slot)
        {
            if (!srvs[slot])
                continue;

            ID3D11Texture2D *tex = srv_to_texture2d(srvs[slot]);

            if (tex)
            {
                if (!found && tex == g_probe_scene_color)
                {
                    found = true;
                    slot_found = slot;
                }

                tex->Release();
            }

            srvs[slot]->Release();
        }

        if (found && first_slot)
            *first_slot = slot_found;

        return found;
    }

    void log_scene_post_consumer(
        ID3D11DeviceContext *ctx,
        UINT first_scene_slot)
    {
        if (!short_probe_should_log() ||
            !ctx ||
            g_scene_post_consumer_count >= SCENE_POST_CONSUMER_LIMIT ||
            g_short_probe_events >= SCENE_BOUNDARY_EVENT_CAP)
        {
            return;
        }

        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ctx->IAGetPrimitiveTopology(&topology);

        D3D11_VIEWPORT vp{};
        UINT nvp = 1;
        ctx->RSGetViewports(&nvp, &vp);

        ID3D11RenderTargetView *rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);

        ID3D11Texture2D *rt = nullptr;
        ID3D11Texture2D *dt = nullptr;

        if (rtv)
            get_texture2d_from_rtv(rtv, &rt);

        if (dsv)
            dt = dsv_to_texture2d(dsv);

        D3D11_TEXTURE2D_DESC rd{}, dd{};
        if (rt) rt->GetDesc(&rd);
        if (dt) dt->GetDesc(&dd);

        const TextureProbeInfo *ri = lookup_texture_probe(rt);
        const TextureProbeInfo *di = lookup_texture_probe(dt);

        const DxbcHash vsh = shader_hash(g_current_vs);
        const DxbcHash psh = shader_hash(g_current_ps);

        ++g_scene_post_consumer_count;
        ++g_short_probe_events;

        log_line(
            "SCENE_POST_CONSUMER order=%u handoffFrame=%llu frame=%llu draw=%llu sceneSlot=%u kind=%s count=%u start=%u base=%d topology=%u viewport=%.0fx%.0f VS=%p VSHASH=%08X-%08X-%08X-%08X VSrole=%d PS=%p PSHASH=%08X-%08X-%08X-%08X PSrole=%d RTVserial=%llu %ux%u fmt=%u DSVserial=%llu %ux%u fmt=%u",
            g_scene_post_consumer_count,
            static_cast<unsigned long long>(g_scene_post_handoff_frame),
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_draw_counter),
            first_scene_slot,
            g_probe_draw_kind,
            g_probe_draw_count,
            g_probe_draw_start,
            g_probe_draw_base_vertex,
            static_cast<uint32_t>(topology),
            vp.Width,
            vp.Height,
            g_current_vs,
            vsh.a, vsh.b, vsh.c, vsh.d,
            static_cast<int>(current_vs_role()),
            g_current_ps,
            psh.a, psh.b, psh.c, psh.d,
            static_cast<int>(current_ps_role()),
            static_cast<unsigned long long>(ri ? ri->serial : 0ull),
            rd.Width,
            rd.Height,
            static_cast<uint32_t>(rd.Format),
            static_cast<unsigned long long>(di ? di->serial : 0ull),
            dd.Width,
            dd.Height,
            static_cast<uint32_t>(dd.Format));

        ID3D11ShaderResourceView *srvs[16]{};
        ctx->PSGetShaderResources(0, 16, srvs);

        for (UINT slot = 0; slot < 16; ++slot)
        {
            if (!srvs[slot])
                continue;

            ID3D11Texture2D *tex = srv_to_texture2d(srvs[slot]);

            if (tex)
            {
                D3D11_TEXTURE2D_DESC d{};
                tex->GetDesc(&d);
                const TextureProbeInfo *info = lookup_texture_probe(tex);

                if (g_short_probe_events < SCENE_BOUNDARY_EVENT_CAP)
                {
                    ++g_short_probe_events;

                    log_line(
                        "SCENE_POST_INPUT order=%u draw=%llu slot=%u isScene=%d serial=%llu %ux%u fmt=%u",
                        g_scene_post_consumer_count,
                        static_cast<unsigned long long>(g_draw_counter),
                        slot,
                        tex == g_probe_scene_color ? 1 : 0,
                        static_cast<unsigned long long>(info ? info->serial : 0ull),
                        d.Width,
                        d.Height,
                        static_cast<uint32_t>(d.Format));
                }

                tex->Release();
            }

            srvs[slot]->Release();
        }

        if (rt) rt->Release();
        if (dt) dt->Release();
        if (rtv) rtv->Release();
        if (dsv) dsv->Release();
    }

    void maybe_start_short_probe()
    {
        if (!g_short_probe_armed ||
            g_short_probe_active ||
            !g_probe_scene_color ||
            !g_probe_velocity ||
            !g_probe_depth)
        {
            return;
        }

        g_short_probe_armed = false;
        g_short_probe_active = true;
        g_short_probe_completed = false;
        g_short_probe_start_frame = g_render_frame + 1ull;
        g_short_probe_end_frame = g_short_probe_start_frame + 5ull;
        g_short_probe_events = 0;
        g_halfres_scene_input_events = 0;
        g_halfres_target_events = 0;
        g_half_color_producer_hits = 0;
        g_half_color_ps_hits.clear();
        g_half_color_vs_hits.clear();
        reset_scene_boundary_probe_state();
        g_geometry_scene_draws = 0;
        g_geometry_jitter_reads = 0;
        g_geometry_jitter_nonzero = 0;
        g_geometry_jitter_matches_frame = 0;
        g_geometry_jitter_mismatches_frame = 0;
        reset_geometry_cb_tracks();
        g_geometry_cb_candidates = 0;
        g_consumer_a_hits = 0;
        g_consumer_b_hits = 0;
        g_consumer_a_ps_hash = {};
        g_consumer_b_ps_hash = {};

        if (g_release_verbose_diagnostics)
        {
            log_line(
            "SHORT_PROBE START generation=%llu colorSerial=%llu depthSerial=%llu currentFrame=%llu captureFrames=%llu..%llu frameCount=6",
            static_cast<unsigned long long>(g_target_generation),
            static_cast<unsigned long long>(g_target_half_color_serial),
            static_cast<unsigned long long>(g_target_half_depth_serial),
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_short_probe_start_frame),
            static_cast<unsigned long long>(g_short_probe_end_frame));
        }
    }

    void update_short_probe_state()
    {
        if (g_short_probe_active && g_render_frame > g_short_probe_end_frame)
        {
            g_short_probe_active = false;
            g_short_probe_completed = true;

            for (auto &track : g_geometry_cb_tracks)
            {
                if (track.used && track.sample_count >= 4)
                    log_geometry_cb_correlations(track);
            }

            uint32_t tracked = 0;
            uint32_t complete = 0;

            for (const auto &track : g_geometry_cb_tracks)
            {
                if (track.used)
                {
                    ++tracked;
                    if (track.sample_count >= 6)
                        ++complete;
                }
            }

            if (g_release_verbose_diagnostics)
            {
                log_line(
                "GEOMETRY_CB STOP frame=%llu events=%llu samples=%llu candidates=%llu trackedVS=%u complete6=%u",
                static_cast<unsigned long long>(g_render_frame),
                static_cast<unsigned long long>(g_short_probe_events),
                static_cast<unsigned long long>(g_geometry_cb_samples),
                static_cast<unsigned long long>(g_geometry_cb_candidates),
                tracked,
                complete);
            }
        }
    }

    bool is_probe_resolution(uint32_t w, uint32_t h)
    {
        return
            (w == 3840 && h == 2160) ||
            (w == 2560 && h == 1440) ||
            (w == 2227 && h == 1253) ||
            (w == 1920 && h == 1080);
    }

    const TextureProbeInfo *lookup_texture_probe(ID3D11Texture2D *tex)
    {
        auto it = g_texture_probe_info.find(tex);
        return it != g_texture_probe_info.end() ? &it->second : nullptr;
    }

    void log_known_texture_identity(const char *label, ID3D11Texture2D *tex)
    {
        if (!tex)
            return;

        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        const TextureProbeInfo *info = lookup_texture_probe(tex);

        if (g_release_verbose_diagnostics)
        {
            log_line(
            "RESOLUTION_PROBE IDENTIFY %s tex=%p createSerial=%llu %ux%u fmt=%u bind=0x%X usage=%u misc=0x%X",
            label,
            tex,
            static_cast<unsigned long long>(info ? info->serial : 0ull),
            d.Width, d.Height,
            static_cast<uint32_t>(d.Format),
            d.BindFlags,
            static_cast<uint32_t>(d.Usage),
            d.MiscFlags);
        }
    }

    void log_bound_resolution_state(ID3D11DeviceContext *ctx, const char *reason)
    {
        if (!ctx)
            return;

        ID3D11RenderTargetView *rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);

        ID3D11Texture2D *rt = nullptr;
        ID3D11Texture2D *dt = nullptr;

        if (rtv)
            get_texture2d_from_rtv(rtv, &rt);

        if (dsv)
        {
            ID3D11Resource *res = nullptr;
            dsv->GetResource(&res);
            if (res)
            {
                res->QueryInterface(
                    __uuidof(ID3D11Texture2D),
                    reinterpret_cast<void **>(&dt));
                res->Release();
            }
        }

        D3D11_VIEWPORT vp{};
        UINT count = 1;
        ctx->RSGetViewports(&count, &vp);

        D3D11_TEXTURE2D_DESC rd{}, dd{};
        if (rt) rt->GetDesc(&rd);
        if (dt) dt->GetDesc(&dd);

        const TextureProbeInfo *ri = lookup_texture_probe(rt);
        const TextureProbeInfo *di = lookup_texture_probe(dt);

        if (g_release_verbose_diagnostics)
        {
            log_line(
            "RESOLUTION_PROBE BOUND reason=%s draw=%llu viewport=%.0fx%.0f RTV=%p serial=%llu %ux%u fmt=%u DSV=%p serial=%llu %ux%u fmt=%u",
            reason,
            static_cast<unsigned long long>(g_draw_counter),
            vp.Width, vp.Height,
            rt,
            static_cast<unsigned long long>(ri ? ri->serial : 0ull),
            rd.Width, rd.Height, static_cast<uint32_t>(rd.Format),
            dt,
            static_cast<unsigned long long>(di ? di->serial : 0ull),
            dd.Width, dd.Height, static_cast<uint32_t>(dd.Format));
        }

        if (rt) rt->Release();
        if (dt) dt->Release();
        if (rtv) rtv->Release();
        if (dsv) dsv->Release();
    }

    bool patch_vtable(void *object, size_t index, void *replacement, void **original)
    {
        if (object == nullptr || replacement == nullptr || original == nullptr)
            return false;

        auto ***obj = reinterpret_cast<void ***>(object);
        void **vt = *obj;
        if (vt == nullptr)
            return false;

        DWORD old_protect = 0;
        if (!VirtualProtect(&vt[index], sizeof(void *), PAGE_EXECUTE_READWRITE, &old_protect))
            return false;

        *original = vt[index];
        vt[index] = replacement;

        DWORD dummy = 0;
        VirtualProtect(&vt[index], sizeof(void *), old_protect, &dummy);
        FlushInstructionCache(GetCurrentProcess(), &vt[index], sizeof(void *));
        return true;
    }

    using PFN_CreateVertexShader = HRESULT (STDMETHODCALLTYPE *)(
        ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *, ID3D11VertexShader **);
    using PFN_CreatePixelShader = HRESULT (STDMETHODCALLTYPE *)(
        ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *, ID3D11PixelShader **);

    using PFN_CreateTexture2D = HRESULT (STDMETHODCALLTYPE *)(
        ID3D11Device *, const D3D11_TEXTURE2D_DESC *,
        const D3D11_SUBRESOURCE_DATA *, ID3D11Texture2D **);
    using PFN_CreateRenderTargetView = HRESULT (STDMETHODCALLTYPE *)(
        ID3D11Device *, ID3D11Resource *,
        const D3D11_RENDER_TARGET_VIEW_DESC *, ID3D11RenderTargetView **);
    using PFN_CreateDepthStencilView = HRESULT (STDMETHODCALLTYPE *)(
        ID3D11Device *, ID3D11Resource *,
        const D3D11_DEPTH_STENCIL_VIEW_DESC *, ID3D11DepthStencilView **);

    using PFN_VSSetShader = void (STDMETHODCALLTYPE *)(
        ID3D11DeviceContext *, ID3D11VertexShader *, ID3D11ClassInstance *const *, UINT);
    using PFN_PSSetShader = void (STDMETHODCALLTYPE *)(
        ID3D11DeviceContext *, ID3D11PixelShader *, ID3D11ClassInstance *const *, UINT);
    using PFN_Draw = void (STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT);
    using PFN_DrawIndexed = void (STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, INT);

    using PFN_RSSetViewports = void (STDMETHODCALLTYPE *)(
        ID3D11DeviceContext *, UINT, const D3D11_VIEWPORT *);

    using PFN_OMSetRenderTargets = void (STDMETHODCALLTYPE *)(
        ID3D11DeviceContext *, UINT,
        ID3D11RenderTargetView *const *, ID3D11DepthStencilView *);
    using PFN_PSSetShaderResources = void (STDMETHODCALLTYPE *)(
        ID3D11DeviceContext *, UINT, UINT, ID3D11ShaderResourceView *const *);
    using PFN_CopyResource = void (STDMETHODCALLTYPE *)(
        ID3D11DeviceContext *, ID3D11Resource *, ID3D11Resource *);

    PFN_CreateVertexShader g_orig_create_vs = nullptr;
    PFN_CreatePixelShader g_orig_create_ps = nullptr;
    PFN_CreateTexture2D g_orig_create_texture2d = nullptr;
    PFN_CreateRenderTargetView g_orig_create_rtv = nullptr;
    PFN_CreateDepthStencilView g_orig_create_dsv = nullptr;
    PFN_VSSetShader g_orig_vs_set_shader = nullptr;
    PFN_PSSetShader g_orig_ps_set_shader = nullptr;
    PFN_Draw g_orig_draw = nullptr;
    PFN_DrawIndexed g_orig_draw_indexed = nullptr;
    PFN_RSSetViewports g_orig_rs_set_viewports = nullptr;
    PFN_OMSetRenderTargets g_orig_om_set_render_targets = nullptr;
    PFN_PSSetShaderResources g_orig_ps_set_shader_resources = nullptr;
    PFN_CopyResource g_orig_copy_resource = nullptr;

    void release_primary_substitution_resources()
    {
        if (g_sub_primary_color_srv)
        {
            g_sub_primary_color_srv->Release();
            g_sub_primary_color_srv = nullptr;
        }

        if (g_sub_primary_dsv)
        {
            g_sub_primary_dsv->Release();
            g_sub_primary_dsv = nullptr;
        }

        if (g_sub_primary_depth)
        {
            g_sub_primary_depth->Release();
            g_sub_primary_depth = nullptr;
        }

        if (g_sub_primary_rtv)
        {
            g_sub_primary_rtv->Release();
            g_sub_primary_rtv = nullptr;
        }

        if (g_sub_primary_color)
        {
            g_sub_primary_color->Release();
            g_sub_primary_color = nullptr;
        }

        g_primary_substitution_ready = false;
        g_primary_substitution_bound = false;
    }

    bool ensure_primary_substitution_resources(ID3D11Device *device)
    {
        if (g_primary_substitution_ready)
            return true;

        if (!device ||
            !g_primary_geom_color ||
            !g_primary_geom_depth ||
            !g_primary_geom_rtv ||
            !g_primary_geom_dsv)
        {
            return false;
        }

        release_primary_substitution_resources();

        D3D11_TEXTURE2D_DESC color_desc{};
        D3D11_TEXTURE2D_DESC depth_desc{};

        g_primary_geom_color->GetDesc(&color_desc);
        g_primary_geom_depth->GetDesc(&depth_desc);

        color_desc.Width = PRIMARY_INTERVAL_W;
        color_desc.Height = PRIMARY_INTERVAL_H;
        color_desc.MipLevels = 1;
        color_desc.ArraySize = 1;
        color_desc.SampleDesc.Count = 1;
        color_desc.SampleDesc.Quality = 0;

        depth_desc.Width = PRIMARY_INTERVAL_W;
        depth_desc.Height = PRIMARY_INTERVAL_H;
        depth_desc.MipLevels = 1;
        depth_desc.ArraySize = 1;
        depth_desc.SampleDesc.Count = 1;
        depth_desc.SampleDesc.Quality = 0;

        HRESULT hr_color =
            g_orig_create_texture2d(
                device,
                &color_desc,
                nullptr,
                &g_sub_primary_color);

        HRESULT hr_depth =
            g_orig_create_texture2d(
                device,
                &depth_desc,
                nullptr,
                &g_sub_primary_depth);

        HRESULT hr_rtv = E_FAIL;
        HRESULT hr_dsv = E_FAIL;
        HRESULT hr_color_srv = E_FAIL;

        if (SUCCEEDED(hr_color) && g_sub_primary_color)
        {
            hr_rtv =
                g_orig_create_rtv(
                    device,
                    g_sub_primary_color,
                    nullptr,
                    &g_sub_primary_rtv);
        }

        if (SUCCEEDED(hr_depth) && g_sub_primary_depth)
        {
            D3D11_DEPTH_STENCIL_VIEW_DESC original_dsv_desc{};
            g_primary_geom_dsv->GetDesc(&original_dsv_desc);

            hr_dsv =
                g_orig_create_dsv(
                    device,
                    g_sub_primary_depth,
                    &original_dsv_desc,
                    &g_sub_primary_dsv);
        }

        if (SUCCEEDED(hr_color) && g_sub_primary_color)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
            srv_desc.Format = color_desc.Format;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MostDetailedMip = 0;
            srv_desc.Texture2D.MipLevels = 1;

            hr_color_srv =
                device->CreateShaderResourceView(
                    g_sub_primary_color,
                    &srv_desc,
                    &g_sub_primary_color_srv);
        }

        g_primary_substitution_ready =
            SUCCEEDED(hr_color) &&
            SUCCEEDED(hr_depth) &&
            SUCCEEDED(hr_rtv) &&
            SUCCEEDED(hr_dsv) &&
            SUCCEEDED(hr_color_srv) &&
            g_sub_primary_color &&
            g_sub_primary_depth &&
            g_sub_primary_rtv &&
            g_sub_primary_dsv &&
            g_sub_primary_color_srv;

        if (g_release_verbose_diagnostics)
        {
            log_line(
            "PRIMARY_SUBSTITUTE CREATE ready=%d colorHR=0x%08X depthHR=0x%08X rtvHR=0x%08X dsvHR=0x%08X colorSrvHR=0x%08X color=%p depth=%p rtv=%p dsv=%p colorSRV=%p size=%ux%u colorFmt=%u depthFmt=%u",
            g_primary_substitution_ready ? 1 : 0,
            static_cast<uint32_t>(hr_color),
            static_cast<uint32_t>(hr_depth),
            static_cast<uint32_t>(hr_rtv),
            static_cast<uint32_t>(hr_dsv),
            static_cast<uint32_t>(hr_color_srv),
            g_sub_primary_color,
            g_sub_primary_depth,
            g_sub_primary_rtv,
            g_sub_primary_dsv,
            g_sub_primary_color_srv,
            PRIMARY_INTERVAL_W,
            PRIMARY_INTERVAL_H,
            static_cast<uint32_t>(color_desc.Format),
            static_cast<uint32_t>(depth_desc.Format));
        }

        if (!g_primary_substitution_ready)
        {
            release_primary_substitution_resources();
            return false;
        }

        return true;
    }

    void copyback_primary_substitution(ID3D11DeviceContext *ctx)
    {
        (void)ctx;

        if (!g_primary_substitution_bound)
            return;

        g_primary_substitution_bound = false;
        ++g_primary_substitution_copybacks;

        if (g_primary_substitution_copybacks <= 12 ||
            (g_primary_substitution_copybacks % 600ull) == 0)
        {
            if (g_release_verbose_diagnostics)
            {
                log_line(
                "PRIMARY_SUBSTITUTE LEAVE count=%llu frame=%llu nativeCopyback=0",
                static_cast<unsigned long long>(g_primary_substitution_copybacks),
                static_cast<unsigned long long>(g_render_frame));
            }
        }
    }


    HRESULT STDMETHODCALLTYPE hook_create_texture2d(
        ID3D11Device *device,
        const D3D11_TEXTURE2D_DESC *desc,
        const D3D11_SUBRESOURCE_DATA *initial_data,
        ID3D11Texture2D **out_texture)
    {
        HRESULT hr = g_orig_create_texture2d(device, desc, initial_data, out_texture);

        if (SUCCEEDED(hr) && desc && out_texture && *out_texture)
        {
            const uint64_t serial = ++g_texture_create_serial;

            TextureProbeInfo info{};
            info.serial = serial;
            info.desc = *desc;
            g_texture_probe_info[*out_texture] = info;

            if (!g_primary_geom_depth &&
                serial == 1ull &&
                desc->Width == 3840 &&
                desc->Height == 2160)
            {
                g_primary_geom_depth = *out_texture;

                if (g_release_verbose_diagnostics)
                {
                    log_line(
                    "RESOURCE_FAMILY IDENTIFY primaryDepth tex=%p serial=%llu %ux%u fmt=%u bind=0x%X",
                    *out_texture,
                    static_cast<unsigned long long>(serial),
                    desc->Width,
                    desc->Height,
                    static_cast<uint32_t>(desc->Format),
                    desc->BindFlags);
                }
            }

            consider_halfres_generation_candidate(
                *out_texture,
                *desc,
                serial);

            if (is_probe_resolution(desc->Width, desc->Height))
            {
                ++g_candidate_texture_creates;

                if (g_release_verbose_diagnostics)
                {
                    log_line(
                    "RESOLUTION_PROBE TEX createSerial=%llu candidate=%llu tex=%p %ux%u fmt=%u bind=0x%X usage=%u cpu=0x%X misc=0x%X mips=%u array=%u samples=%u",
                    static_cast<unsigned long long>(serial),
                    static_cast<unsigned long long>(g_candidate_texture_creates),
                    *out_texture,
                    desc->Width, desc->Height,
                    static_cast<uint32_t>(desc->Format),
                    desc->BindFlags,
                    static_cast<uint32_t>(desc->Usage),
                    desc->CPUAccessFlags,
                    desc->MiscFlags,
                    desc->MipLevels,
                    desc->ArraySize,
                    desc->SampleDesc.Count);
                }
            }
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE hook_create_rtv(
        ID3D11Device *device,
        ID3D11Resource *resource,
        const D3D11_RENDER_TARGET_VIEW_DESC *desc,
        ID3D11RenderTargetView **out_view)
    {
        HRESULT hr = g_orig_create_rtv(device, resource, desc, out_view);

        if (SUCCEEDED(hr) && resource && out_view && *out_view)
        {
            ID3D11Texture2D *tex = nullptr;
            if (SUCCEEDED(resource->QueryInterface(
                    __uuidof(ID3D11Texture2D),
                    reinterpret_cast<void **>(&tex))) && tex)
            {
                D3D11_TEXTURE2D_DESC td{};
                tex->GetDesc(&td);

                if (is_probe_resolution(td.Width, td.Height))
                {
                    const TextureProbeInfo *info = lookup_texture_probe(tex);

                    if (!g_primary_geom_color &&
                        td.Width == 3840 &&
                        td.Height == 2160 &&
                        static_cast<uint32_t>(td.Format) == 24u &&
                        (!info || info->serial == 0ull))
                    {
                        g_primary_geom_color = tex;
                        g_primary_geom_color->AddRef();

                        g_primary_geom_rtv = *out_view;
                        g_primary_geom_rtv->AddRef();

                        if (g_release_verbose_diagnostics)
                        {
                            log_line(
                            "RESOURCE_FAMILY IDENTIFY primaryColor tex=%p serial=0 %ux%u fmt=%u bind=0x%X",
                            tex,
                            td.Width,
                            td.Height,
                            static_cast<uint32_t>(td.Format),
                            td.BindFlags);
                        }
                    }

                    if (g_release_verbose_diagnostics)
                    {
                        log_line(
                        "RESOLUTION_PROBE RTV viewSerial=%llu view=%p tex=%p createSerial=%llu %ux%u fmt=%u",
                        static_cast<unsigned long long>(++g_view_create_serial),
                        *out_view, tex,
                        static_cast<unsigned long long>(info ? info->serial : 0ull),
                        td.Width, td.Height,
                        static_cast<uint32_t>(td.Format));
                    }
                }

                tex->Release();
            }
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE hook_create_dsv(
        ID3D11Device *device,
        ID3D11Resource *resource,
        const D3D11_DEPTH_STENCIL_VIEW_DESC *desc,
        ID3D11DepthStencilView **out_view)
    {
        HRESULT hr = g_orig_create_dsv(device, resource, desc, out_view);

        if (SUCCEEDED(hr) && resource && out_view && *out_view)
        {
            ID3D11Texture2D *tex = nullptr;
            if (SUCCEEDED(resource->QueryInterface(
                    __uuidof(ID3D11Texture2D),
                    reinterpret_cast<void **>(&tex))) && tex)
            {
                D3D11_TEXTURE2D_DESC td{};
                tex->GetDesc(&td);

                if (is_probe_resolution(td.Width, td.Height))
                {
                    const TextureProbeInfo *info = lookup_texture_probe(tex);

                    if (!g_primary_geom_dsv &&
                        tex == g_primary_geom_depth)
                    {
                        g_primary_geom_dsv = *out_view;
                        g_primary_geom_dsv->AddRef();

                        if (g_release_verbose_diagnostics)
                        {
                            log_line(
                            "PRIMARY_SUBSTITUTE CAPTURE originalDSV=%p tex=%p serial=%llu %ux%u fmt=%u",
                            g_primary_geom_dsv,
                            tex,
                            static_cast<unsigned long long>(info ? info->serial : 0ull),
                            td.Width,
                            td.Height,
                            static_cast<uint32_t>(td.Format));
                        }
                    }

                    if (g_release_verbose_diagnostics)
                    {
                        log_line(
                        "RESOLUTION_PROBE DSV viewSerial=%llu view=%p tex=%p createSerial=%llu %ux%u fmt=%u",
                        static_cast<unsigned long long>(++g_view_create_serial),
                        *out_view, tex,
                        static_cast<unsigned long long>(info ? info->serial : 0ull),
                        td.Width, td.Height,
                        static_cast<uint32_t>(td.Format));
                    }
                }

                tex->Release();
            }
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE hook_create_vs(
        ID3D11Device *device,
        const void *bytecode,
        SIZE_T bytecode_length,
        ID3D11ClassLinkage *linkage,
        ID3D11VertexShader **out_shader)
    {
        HRESULT hr = g_orig_create_vs(device, bytecode, bytecode_length, linkage, out_shader);

        if (SUCCEEDED(hr) && out_shader != nullptr && *out_shader != nullptr)
        {
            DxbcHash hash{};
            if (read_dxbc_hash(bytecode, bytecode_length, hash))
                g_vs_hashes[*out_shader] = hash;

            ShaderRole role = identify_vs(bytecode, bytecode_length);
            if (role != ShaderRole::none)
            {
                g_vs_roles[*out_shader] = role;
                if (g_release_verbose_diagnostics)
                {
                    log_line("FOUND shader: %s object=%p", role_name(role), *out_shader);
                }
            }
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE hook_create_ps(
        ID3D11Device *device,
        const void *bytecode,
        SIZE_T bytecode_length,
        ID3D11ClassLinkage *linkage,
        ID3D11PixelShader **out_shader)
    {
        HRESULT hr = g_orig_create_ps(device, bytecode, bytecode_length, linkage, out_shader);

        if (SUCCEEDED(hr) && out_shader != nullptr && *out_shader != nullptr)
        {
            DxbcHash hash{};
            if (read_dxbc_hash(bytecode, bytecode_length, hash))
                g_ps_hashes[*out_shader] = hash;

            ShaderRole role = identify_ps(bytecode, bytecode_length);
            if (role != ShaderRole::none)
            {
                g_ps_roles[*out_shader] = role;
                if (g_release_verbose_diagnostics)
                {
                    log_line("FOUND shader: %s object=%p", role_name(role), *out_shader);
                }
            }
        }

        return hr;
    }

    void STDMETHODCALLTYPE hook_vs_set_shader(
        ID3D11DeviceContext *ctx,
        ID3D11VertexShader *shader,
        ID3D11ClassInstance *const *instances,
        UINT count)
    {
        g_current_vs = shader;
        g_orig_vs_set_shader(ctx, shader, instances, count);
    }

    void STDMETHODCALLTYPE hook_ps_set_shader(
        ID3D11DeviceContext *ctx,
        ID3D11PixelShader *shader,
        ID3D11ClassInstance *const *instances,
        UINT count)
    {
        g_current_ps = shader;
        g_orig_ps_set_shader(ctx, shader, instances, count);
    }

    ShaderRole current_vs_role()
    {
        auto it = g_vs_roles.find(g_current_vs);
        return it != g_vs_roles.end() ? it->second : ShaderRole::none;
    }

    ShaderRole current_ps_role()
    {
        auto it = g_ps_roles.find(g_current_ps);
        return it != g_ps_roles.end() ? it->second : ShaderRole::none;
    }

    bool try_native_smaa_passthrough(ID3D11DeviceContext *ctx)
    {
        if (!g_native_smaa_passthrough_enabled ||
            current_vs_role() != ShaderRole::vs_smaa ||
            ctx == nullptr)
        {
            return false;
        }

        ID3D11ShaderResourceView *src_srv = nullptr;
        ctx->PSGetShaderResources(0, 1, &src_srv);

        ID3D11RenderTargetView *dst_rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(1, &dst_rtv, &dsv);

        ID3D11Texture2D *src_tex = nullptr;
        ID3D11Texture2D *dst_tex = nullptr;

        get_texture2d_from_srv(src_srv, &src_tex);
        get_texture2d_from_rtv(dst_rtv, &dst_tex);

        bool copied = false;

        if (src_tex != nullptr && dst_tex != nullptr && src_tex != dst_tex)
        {
            D3D11_TEXTURE2D_DESC sdesc{};
            D3D11_TEXTURE2D_DESC ddesc{};
            src_tex->GetDesc(&sdesc);
            dst_tex->GetDesc(&ddesc);

            const bool candidate =
                sdesc.Width == 3840 &&
                sdesc.Height == 2160 &&
                ddesc.Width == 3840 &&
                ddesc.Height == 2160 &&
                sdesc.Format == ddesc.Format &&
                sdesc.SampleDesc.Count == 1 &&
                ddesc.SampleDesc.Count == 1;

            if (candidate)
            {
                ctx->CopyResource(dst_tex, src_tex);
                copied = true;
                ++g_smaa_passthrough_copies;

                if (g_smaa_passthrough_copies <= 16 ||
                    (g_smaa_passthrough_copies % 3600ull) == 0)
                {
                    if (g_release_verbose_diagnostics)
                    {
                        log_line(
                        "NATIVE_SMAA_BYPASS count=%llu draw=%llu src=%p dst=%p %ux%u fmt=%u",
                        static_cast<unsigned long long>(g_smaa_passthrough_copies),
                        static_cast<unsigned long long>(g_draw_counter),
                        src_tex,
                        dst_tex,
                        sdesc.Width,
                        sdesc.Height,
                        static_cast<uint32_t>(sdesc.Format));
                    }
                }
            }
            else
            {
                ++g_smaa_passthrough_rejected;

                if (g_smaa_passthrough_rejected <= 16)
                {
                    log_line(
                        "NATIVE_SMAA_REJECT count=%llu draw=%llu src=%p %ux%u fmt=%u dst=%p %ux%u fmt=%u",
                        static_cast<unsigned long long>(g_smaa_passthrough_rejected),
                        static_cast<unsigned long long>(g_draw_counter),
                        src_tex,
                        sdesc.Width,
                        sdesc.Height,
                        static_cast<uint32_t>(sdesc.Format),
                        dst_tex,
                        ddesc.Width,
                        ddesc.Height,
                        static_cast<uint32_t>(ddesc.Format));
                }
            }
        }
        else
        {
            ++g_smaa_passthrough_rejected;

            if (g_smaa_passthrough_rejected <= 16)
            {
                log_line(
                    "NATIVE_SMAA_REJECT count=%llu draw=%llu src=%p dst=%p",
                    static_cast<unsigned long long>(g_smaa_passthrough_rejected),
                    static_cast<unsigned long long>(g_draw_counter),
                    src_tex,
                    dst_tex);
            }
        }

        if (src_tex) src_tex->Release();
        if (dst_tex) dst_tex->Release();
        if (src_srv) src_srv->Release();
        if (dst_rtv) dst_rtv->Release();
        if (dsv) dsv->Release();

        return copied;
    }


    bool read_jitter_only(
        ID3D11DeviceContext *ctx,
        uint32_t width,
        uint32_t height,
        float &out_x_pixels,
        float &out_y_pixels)
    {
        out_x_pixels = 0.0f;
        out_y_pixels = 0.0f;

        if (ctx == nullptr)
            return false;

        ID3D11Buffer *cb = nullptr;
        ctx->VSGetConstantBuffers(1, 1, &cb);
        if (cb == nullptr)
            return false;

        D3D11_BUFFER_DESC desc{};
        cb->GetDesc(&desc);

        if (desc.ByteWidth != 608 ||
            desc.Usage != D3D11_USAGE_DYNAMIC ||
            (desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) == 0)
        {
            cb->Release();
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = ctx->Map(cb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);

        if (FAILED(hr) || mapped.pData == nullptr)
        {
            cb->Release();
            return false;
        }

        const float *f = reinterpret_cast<const float *>(mapped.pData);
        const float raw_x = f[136];
        const float raw_y = f[137];

        ctx->Unmap(cb, 0);
        cb->Release();

        out_x_pixels = raw_x * (static_cast<float>(width) * 0.5f);
        out_y_pixels = raw_y * (static_cast<float>(height) * 0.5f);

        return true;
    }

    bool read_and_optionally_zero_jitter(ID3D11DeviceContext *ctx, uint32_t width, uint32_t height)
    {
        if (ctx == nullptr)
            return false;

        ID3D11Buffer *cb = nullptr;
        ctx->VSGetConstantBuffers(1, 1, &cb);
        if (cb == nullptr)
            return false;

        D3D11_BUFFER_DESC desc{};
        cb->GetDesc(&desc);

        if (desc.ByteWidth != 608 ||
            desc.Usage != D3D11_USAGE_DYNAMIC ||
            (desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) == 0)
        {
            cb->Release();
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = ctx->Map(cb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);
        if (FAILED(hr) || mapped.pData == nullptr)
        {
            cb->Release();
            return false;
        }

        float *f = static_cast<float *>(mapped.pData);
        const float old_x = f[2];
        const float old_y = f[6];

        const float native_jitter_x_pixels =
            old_x * static_cast<float>(width);
        const float native_jitter_y_pixels =
            old_y * static_cast<float>(height);

        if (g_dlaa_injection_enabled)
        {
            dlaa_jitter_for_frame(
                g_render_frame,
                g_current_jitter_x_pixels,
                g_current_jitter_y_pixels);

            if (g_release_verbose_diagnostics && (g_render_frame <= 16 ||
                (g_render_frame % 1800ull)) == 0)
            {
                log_line(
                    "DLAA_JITTER frame=%llu index=%u native=(%.6f,%.6f) applied=(%.6f,%.6f) sequence=8",
                    static_cast<unsigned long long>(g_render_frame),
                    dlaa_jitter_index_for_frame(g_render_frame),
                    native_jitter_x_pixels,
                    native_jitter_y_pixels,
                    g_current_jitter_x_pixels,
                    g_current_jitter_y_pixels);
            }
        }
        else
        {
            g_current_jitter_x_pixels = native_jitter_x_pixels;
            g_current_jitter_y_pixels = native_jitter_y_pixels;
        }

        if(g_zero_jitter_enabled)
        {
            f[2] = 0.0f;
            f[6] = 0.0f;

            ++g_zero_jitter_writes;
            if (g_zero_jitter_writes <= 8 || (g_zero_jitter_writes % 1800ull) == 0)
            {
                log_line(
                    "JITTER_OVERRIDE write=%llu old=(%.9g,%.9g) pixels=(%.3f,%.3f) new=(0,0)",
                    static_cast<unsigned long long>(g_zero_jitter_writes),
                    old_x, old_y,
                    g_current_jitter_x_pixels,
                    g_current_jitter_y_pixels);
            }
        }

        ctx->Unmap(cb, 0);
        cb->Release();
        return true;
    }

    const char *dlss_mode_name(uint32_t mode)
    {
        switch (mode)
        {
        case 0: return "DLAA";
        case 1: return "Quality";
        case 2: return "Balanced";
        case 3: return "Performance";
        default: return "Unknown";
        }
    }

    void poll_hotkey(ID3D11DeviceContext *ctx)
    {
        if ((GetAsyncKeyState(VK_F5) & 1) != 0)
        {
            g_short_probe_armed = true;
            g_short_probe_active = false;
            g_short_probe_completed = false;

            if (g_release_verbose_diagnostics)
            {
                log_line(
                "SHORT_PROBE REARM requested draw=%llu frame=%llu",
                static_cast<unsigned long long>(g_draw_counter),
                static_cast<unsigned long long>(g_render_frame));
            }

            maybe_start_short_probe();
        }

    }

    enum class ConsumerPassKind : uint32_t
    {
        none = 0,
        consumer_a = 1,
        consumer_b = 2
    };

    ConsumerPassKind identify_consumer_pass(ID3D11DeviceContext *ctx)
    {
        if (!ctx ||
            g_probe_draw_count != 3 ||
            g_current_ps == nullptr)
        {
            return ConsumerPassKind::none;
        }

        const DxbcHash psh = shader_hash(g_current_ps);

        ID3D11RenderTargetView *rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);

        ID3D11Texture2D *rt = nullptr;
        if (rtv)
            get_texture2d_from_rtv(rtv, &rt);

        const bool scene_target =
            rt != nullptr &&
            rt == g_probe_scene_color;

        if (rt) rt->Release();
        if (rtv) rtv->Release();
        if (dsv) dsv->Release();

        if (!scene_target)
            return ConsumerPassKind::none;

        ID3D11ShaderResourceView *srvs[6]{};
        ctx->PSGetShaderResources(0, 6, srvs);

        ID3D11Texture2D *s0 = srv_to_texture2d(srvs[0]);
        ID3D11Texture2D *s1 = srv_to_texture2d(srvs[1]);
        ID3D11Texture2D *s2 = srv_to_texture2d(srvs[2]);
        ID3D11Texture2D *s5 = srv_to_texture2d(srvs[5]);

        const bool sig_a =
            same_hash(psh, CONSUMER_A_HASH) &&
            s0 == g_target_half_color &&
            s1 == g_target_half_color &&
            s2 == g_target_half_depth;

        const bool sig_b =
            same_hash(psh, CONSUMER_B_HASH) &&
            s5 == g_target_half_color;

        if (s0) s0->Release();
        if (s1) s1->Release();
        if (s2) s2->Release();
        if (s5) s5->Release();

        for (auto *srv : srvs)
            if (srv) srv->Release();

        if (sig_a)
            return ConsumerPassKind::consumer_a;

        if (sig_b)
            return ConsumerPassKind::consumer_b;

        return ConsumerPassKind::none;
    }

    bool is_scene_color_geometry_draw(ID3D11DeviceContext *ctx)
    {
        if (!ctx || !g_probe_scene_color)
            return false;

        if (g_probe_draw_kind != nullptr &&
            strcmp(g_probe_draw_kind, "Draw") == 0 &&
            g_probe_draw_count <= 3)
        {
            return false;
        }

        ID3D11RenderTargetView *rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);

        ID3D11Texture2D *rt = nullptr;
        if (rtv)
            get_texture2d_from_rtv(rtv, &rt);

        const bool writes_scene =
            rt != nullptr &&
            rt == g_probe_scene_color;

        if (rt) rt->Release();
        if (rtv) rtv->Release();
        if (dsv) dsv->Release();

        return writes_scene;
    }

    void log_scene_geometry_jitter(ID3D11DeviceContext *ctx)
    {
        if (!short_probe_should_log() ||
            !is_scene_color_geometry_draw(ctx) ||
            g_short_probe_events >= GEOMETRY_JITTER_EVENT_CAP)
        {
            return;
        }

        D3D11_VIEWPORT vp{};
        UINT nvp = 1;
        ctx->RSGetViewports(&nvp, &vp);

        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ctx->IAGetPrimitiveTopology(&topology);

        float jitter_x = 0.0f;
        float jitter_y = 0.0f;

        const bool jitter_read =
            read_jitter_only(
                ctx,
                static_cast<uint32_t>(vp.Width),
                static_cast<uint32_t>(vp.Height),
                jitter_x,
                jitter_y);

        const bool nonzero =
            jitter_read &&
            (fabsf(jitter_x) > 0.001f ||
             fabsf(jitter_y) > 0.001f);

        const bool matches_frame =
            jitter_read &&
            fabsf(jitter_x - g_current_jitter_x_pixels) < 0.01f &&
            fabsf(jitter_y - g_current_jitter_y_pixels) < 0.01f;

        const DxbcHash vsh = shader_hash(g_current_vs);
        const DxbcHash psh = shader_hash(g_current_ps);

        ++g_geometry_scene_draws;

        if (jitter_read)
            ++g_geometry_jitter_reads;

        if (nonzero)
            ++g_geometry_jitter_nonzero;

        if (jitter_read)
        {
            if (matches_frame)
                ++g_geometry_jitter_matches_frame;
            else
                ++g_geometry_jitter_mismatches_frame;
        }

        ++g_short_probe_events;

        log_line(
            "GEOMETRY_JITTER frame=%llu draw=%llu kind=%s count=%u start=%u base=%d topology=%u viewport=%.0fx%.0f read=%d jitterPx=(%.6f,%.6f) frameJitterPx=(%.6f,%.6f) nonzero=%d matchFrame=%d VS=%p VSHASH=%08X-%08X-%08X-%08X VSrole=%d PS=%p PSHASH=%08X-%08X-%08X-%08X PSrole=%d",
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_draw_counter),
            g_probe_draw_kind,
            g_probe_draw_count,
            g_probe_draw_start,
            g_probe_draw_base_vertex,
            static_cast<uint32_t>(topology),
            vp.Width,
            vp.Height,
            jitter_read ? 1 : 0,
            jitter_x,
            jitter_y,
            g_current_jitter_x_pixels,
            g_current_jitter_y_pixels,
            nonzero ? 1 : 0,
            matches_frame ? 1 : 0,
            g_current_vs,
            vsh.a, vsh.b, vsh.c, vsh.d,
            static_cast<int>(current_vs_role()),
            g_current_ps,
            psh.a, psh.b, psh.c, psh.d,
            static_cast<int>(current_ps_role()));
    }

    void reset_geometry_cb_tracks()
    {
        for (auto &track : g_geometry_cb_tracks)
            track = {};

        g_geometry_cb_samples = 0;
    }

    GeometryCbShaderTrack *find_or_assign_geometry_cb_track()
    {
        const DxbcHash hash = shader_hash(g_current_vs);

        for (auto &track : g_geometry_cb_tracks)
        {
            if (track.used && same_hash(track.vs_hash, hash))
                return &track;
        }

        for (auto &track : g_geometry_cb_tracks)
        {
            if (!track.used)
            {
                track.used = true;
                track.vs_hash = hash;
                track.vs = g_current_vs;
                return &track;
            }
        }

        return nullptr;
    }

    bool read_vs_constant_buffer_floats(
        ID3D11DeviceContext *ctx,
        UINT slot,
        float *out_values,
        uint32_t max_floats,
        uint32_t &out_count,
        uint32_t &out_bytes)
    {
        out_count = 0;
        out_bytes = 0;

        if (!ctx || !out_values || max_floats == 0)
            return false;

        ID3D11Buffer *cb = nullptr;
        ctx->VSGetConstantBuffers(slot, 1, &cb);

        if (!cb)
            return false;

        D3D11_BUFFER_DESC desc{};
        cb->GetDesc(&desc);
        out_bytes = desc.ByteWidth;

        if (desc.ByteWidth == 0 ||
            desc.ByteWidth > max_floats * sizeof(float) ||
            desc.Usage != D3D11_USAGE_DYNAMIC ||
            (desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) == 0)
        {
            cb->Release();
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr =
            ctx->Map(cb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);

        if (FAILED(hr) || !mapped.pData)
        {
            cb->Release();
            return false;
        }

        const uint32_t count = desc.ByteWidth / sizeof(float);
        memcpy(out_values, mapped.pData, count * sizeof(float));

        ctx->Unmap(cb, 0);
        cb->Release();

        out_count = count;
        return true;
    }

    void log_geometry_cb_correlations(GeometryCbShaderTrack &track)
    {
        if (track.sample_count < 4)
            return;

        const uint32_t samples = track.sample_count;
        const uint32_t floats =
            (track.cb_bytes / sizeof(float)) < GEOMETRY_CB_MAX_FLOATS
                ? (track.cb_bytes / sizeof(float))
                : GEOMETRY_CB_MAX_FLOATS;

        for (uint32_t fi = 0; fi < floats; ++fi)
        {
            bool all_valid = true;

            for (uint32_t si = 0; si < samples; ++si)
            {
                if (!track.value_valid[si][fi])
                {
                    all_valid = false;
                    break;
                }
            }

            if (!all_valid)
                continue;

            bool alt_xy = true;
            bool alt_negxy = true;

            for (uint32_t si = 0; si < samples; ++si)
            {
                const float v = track.values[si][fi];
                const float jx = track.frame_jitter_x[si];
                const float jy = track.frame_jitter_y[si];

                const bool match_x =
                    fabsf(v - jx) < 0.00001f ||
                    fabsf(v - (jx / 1920.0f)) < 0.00001f ||
                    fabsf(v - (jx / 3840.0f)) < 0.00001f;

                const bool match_y =
                    fabsf(v - jy) < 0.00001f ||
                    fabsf(v - (jy / 1080.0f)) < 0.00001f ||
                    fabsf(v - (jy / 2160.0f)) < 0.00001f;

                if (!(match_x || match_y))
                    alt_xy = false;

                const bool match_negx =
                    fabsf(v + jx) < 0.00001f ||
                    fabsf(v + (jx / 1920.0f)) < 0.00001f ||
                    fabsf(v + (jx / 3840.0f)) < 0.00001f;

                const bool match_negy =
                    fabsf(v + jy) < 0.00001f ||
                    fabsf(v + (jy / 1080.0f)) < 0.00001f ||
                    fabsf(v + (jy / 2160.0f)) < 0.00001f;

                if (!(match_negx || match_negy))
                    alt_negxy = false;
            }

            bool two_state = true;
            const float a = track.values[0][fi];
            const float b = track.values[1][fi];

            if (fabsf(a - b) < 1e-9f)
                two_state = false;

            if (two_state)
            {
                for (uint32_t si = 0; si < samples; ++si)
                {
                    const float expected = (si % 2u == 0u) ? a : b;
                    if (fabsf(track.values[si][fi] - expected) > 1e-7f)
                    {
                        two_state = false;
                        break;
                    }
                }
            }

            if (alt_xy || alt_negxy || two_state)
            {
                ++g_geometry_cb_candidates;
                ++g_short_probe_events;

                log_line(
                    "GEOMETRY_CB_CANDIDATE VS=%p VSHASH=%08X-%08X-%08X-%08X slot=%u byteOffset=%u floatIndex=%u samples=%u altXY=%d altNegXY=%d twoState=%d values=(%.9g,%.9g,%.9g,%.9g,%.9g,%.9g) jitterX=(%.3f,%.3f,%.3f,%.3f,%.3f,%.3f) jitterY=(%.3f,%.3f,%.3f,%.3f,%.3f,%.3f)",
                    track.vs,
                    track.vs_hash.a, track.vs_hash.b,
                    track.vs_hash.c, track.vs_hash.d,
                    track.cb_slot,
                    fi * 4u,
                    fi,
                    samples,
                    alt_xy ? 1 : 0,
                    alt_negxy ? 1 : 0,
                    two_state ? 1 : 0,
                    track.values[0][fi],
                    samples > 1 ? track.values[1][fi] : 0.0f,
                    samples > 2 ? track.values[2][fi] : 0.0f,
                    samples > 3 ? track.values[3][fi] : 0.0f,
                    samples > 4 ? track.values[4][fi] : 0.0f,
                    samples > 5 ? track.values[5][fi] : 0.0f,
                    track.frame_jitter_x[0],
                    samples > 1 ? track.frame_jitter_x[1] : 0.0f,
                    samples > 2 ? track.frame_jitter_x[2] : 0.0f,
                    samples > 3 ? track.frame_jitter_x[3] : 0.0f,
                    samples > 4 ? track.frame_jitter_x[4] : 0.0f,
                    samples > 5 ? track.frame_jitter_x[5] : 0.0f,
                    track.frame_jitter_y[0],
                    samples > 1 ? track.frame_jitter_y[1] : 0.0f,
                    samples > 2 ? track.frame_jitter_y[2] : 0.0f,
                    samples > 3 ? track.frame_jitter_y[3] : 0.0f,
                    samples > 4 ? track.frame_jitter_y[4] : 0.0f,
                    samples > 5 ? track.frame_jitter_y[5] : 0.0f);

                if (g_short_probe_events >= GEOMETRY_CB_EVENT_CAP)
                    return;
            }
        }
    }

    void sample_scene_geometry_constant_buffers(ID3D11DeviceContext *ctx)
    {
        if (!short_probe_should_log() ||
            !is_scene_color_geometry_draw(ctx) ||
            g_short_probe_events >= GEOMETRY_CB_EVENT_CAP)
        {
            return;
        }

        GeometryCbShaderTrack *track = find_or_assign_geometry_cb_track();
        if (!track)
            return;

        ++track->seen_draws;

        if (track->sample_count > 0 &&
            track->frame_ids[track->sample_count - 1] == g_render_frame)
        {
            return;
        }

        if (track->sample_count >= 6)
            return;

        float values[GEOMETRY_CB_MAX_FLOATS]{};
        uint32_t value_count = 0;
        uint32_t byte_count = 0;

        bool read_ok = false;
        UINT chosen_slot = 0;

        for (UINT slot = 0; slot < 4; ++slot)
        {
            if (read_vs_constant_buffer_floats(
                    ctx,
                    slot,
                    values,
                    GEOMETRY_CB_MAX_FLOATS,
                    value_count,
                    byte_count))
            {
                chosen_slot = slot;
                read_ok = true;
                break;
            }
        }

        if (!read_ok || value_count == 0)
            return;

        const uint32_t si = track->sample_count;
        track->cb_slot = chosen_slot;
        track->cb_bytes = byte_count;
        track->frame_ids[si] = g_render_frame;
        track->frame_jitter_x[si] = g_current_jitter_x_pixels;
        track->frame_jitter_y[si] = g_current_jitter_y_pixels;

        for (uint32_t i = 0; i < value_count && i < GEOMETRY_CB_MAX_FLOATS; ++i)
        {
            track->values[si][i] = values[i];
            track->value_valid[si][i] = true;
        }

        ++track->sample_count;
        ++g_geometry_cb_samples;
        ++g_short_probe_events;

        log_line(
            "GEOMETRY_CB_SAMPLE frame=%llu draw=%llu VS=%p VSHASH=%08X-%08X-%08X-%08X slot=%u bytes=%u floats=%u sample=%u jitterPx=(%.3f,%.3f)",
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_draw_counter),
            g_current_vs,
            track->vs_hash.a, track->vs_hash.b,
            track->vs_hash.c, track->vs_hash.d,
            chosen_slot,
            byte_count,
            value_count,
            track->sample_count,
            g_current_jitter_x_pixels,
            g_current_jitter_y_pixels);

        if (track->sample_count >= 4)
            log_geometry_cb_correlations(*track);
    }

    struct GeometryJitterPatchState
    {
        bool active = false;
        ID3D11Buffer *cb = nullptr;
        float original_x = 0.0f;
        float original_y = 0.0f;
    };

    bool map_patch_geometry_jitter(
        ID3D11DeviceContext *ctx,
        GeometryJitterPatchState &state)
    {
        state = {};

        if (!g_dlaa_injection_enabled ||
            g_zero_jitter_enabled ||
            !ctx ||
            !is_scene_color_geometry_draw(ctx))
        {
            return false;
        }

        ID3D11Buffer *cb = nullptr;
        ctx->VSGetConstantBuffers(0, 1, &cb);

        if (!cb)
            return false;

        D3D11_BUFFER_DESC desc{};
        cb->GetDesc(&desc);

        if (desc.ByteWidth < 156 ||
            desc.Usage != D3D11_USAGE_DYNAMIC ||
            (desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) == 0)
        {
            cb->Release();
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr =
            ctx->Map(cb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);

        if (FAILED(hr) || !mapped.pData)
        {
            ++g_dlaa_jitter_patch_failures;
            cb->Release();
            return false;
        }

        float *f = reinterpret_cast<float *>(mapped.pData);

        state.original_x = f[34];
        state.original_y = f[38];

        float jitter_x_pixels = 0.0f;
        float jitter_y_pixels = 0.0f;

        const uint64_t scene_frame = g_render_frame + 1ull;
        dlaa_jitter_for_frame(
            scene_frame,
            jitter_x_pixels,
            jitter_y_pixels);

        f[34] = jitter_x_pixels / 3840.0f;
        f[38] = jitter_y_pixels / 2160.0f;

        ctx->Unmap(cb, 0);

        state.active = true;
        state.cb = cb;

        ++g_dlaa_jitter_patch_draws;

        if (g_dlaa_jitter_patch_draws <= 12 ||
            (g_dlaa_jitter_patch_draws % 5000ull) == 0)
        {
            if (g_release_verbose_diagnostics)
            {
                log_line(
                "DLAA_JITTER_PATCH count=%llu sceneFrame=%llu index=%u draw=%llu original=(%.9g,%.9g) patchedPx=(%.6f,%.6f)",
                static_cast<unsigned long long>(g_dlaa_jitter_patch_draws),
                static_cast<unsigned long long>(scene_frame),
                dlaa_jitter_index_for_frame(scene_frame),
                static_cast<unsigned long long>(g_draw_counter),
                state.original_x,
                state.original_y,
                jitter_x_pixels,
                jitter_y_pixels);
            }
        }

        return true;
    }

    void restore_geometry_jitter(
        ID3D11DeviceContext *ctx,
        GeometryJitterPatchState &state)
    {
        if (!state.active || !ctx || !state.cb)
            return;

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr =
            ctx->Map(state.cb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);

        if (SUCCEEDED(hr) && mapped.pData)
        {
            float *f = reinterpret_cast<float *>(mapped.pData);
            f[34] = state.original_x;
            f[38] = state.original_y;
            ctx->Unmap(state.cb, 0);
        }
        else
        {
            ++g_dlaa_jitter_patch_failures;
        }

        state.cb->Release();
        state.cb = nullptr;
        state.active = false;
    }

    const char *primary_target_name(ID3D11Texture2D *tex)
    {
        if (!tex)
            return "none";

        if (tex == g_probe_scene_color || tex == g_last_scene_tex)
            return "scene_color";

        if (tex == g_last_velocity_tex)
            return "motion";

        if (tex == g_last_depth_tex)
            return "depth_srv";

        return "other";
    }

    void begin_resolution_architecture_capture()
    {
        g_res_arch_capture_active = true;
        g_res_arch_capture_end_frame = g_render_frame + 8ull;
        g_res_arch_events = 0;
        g_res_arch_scene_writes = 0;
        g_res_arch_depth_writes = 0;
        g_res_arch_motion_writes = 0;
        g_res_arch_other_primary_writes = 0;

        log_line(
            "RES_ARCH START frame=%llu captureThrough=%llu scene=%p motion=%p depth=%p",
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_res_arch_capture_end_frame),
            g_last_scene_tex ? g_last_scene_tex : g_probe_scene_color,
            g_last_velocity_tex,
            g_last_depth_tex);
    }

    void stop_resolution_architecture_capture(const char *reason)
    {
        if (!g_res_arch_capture_active)
            return;

        g_res_arch_capture_active = false;

        log_line(
            "RES_ARCH STOP reason=%s frame=%llu events=%llu sceneWrites=%llu motionWrites=%llu depthWrites=%llu otherPrimary=%llu",
            reason ? reason : "unknown",
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_res_arch_events),
            static_cast<unsigned long long>(g_res_arch_scene_writes),
            static_cast<unsigned long long>(g_res_arch_motion_writes),
            static_cast<unsigned long long>(g_res_arch_depth_writes),
            static_cast<unsigned long long>(g_res_arch_other_primary_writes));
    }

    void log_primary_render_target_write(ID3D11DeviceContext *ctx)
    {
        if (!g_res_arch_capture_active || !ctx)
            return;

        if (g_render_frame > g_res_arch_capture_end_frame)
        {
            stop_resolution_architecture_capture("frame-window");
            return;
        }

        if (g_res_arch_events >= RES_ARCH_EVENT_CAP)
        {
            stop_resolution_architecture_capture("event-cap");
            return;
        }

        ID3D11RenderTargetView *rtvs[8]{};
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(8, rtvs, &dsv);

        ID3D11Texture2D *dsv_tex = nullptr;
        if (dsv)
            dsv_tex = dsv_to_texture2d(dsv);

        bool interesting = false;
        const char *kind = "other";

        for (UINT i = 0; i < 8; ++i)
        {
            if (!rtvs[i])
                continue;

            ID3D11Texture2D *tex = nullptr;
            get_texture2d_from_rtv(rtvs[i], &tex);

            if (tex)
            {
                if (tex == g_probe_scene_color || tex == g_last_scene_tex)
                {
                    interesting = true;
                    kind = "scene_color";
                    ++g_res_arch_scene_writes;
                }
                else if (tex == g_last_velocity_tex)
                {
                    interesting = true;
                    kind = "motion";
                    ++g_res_arch_motion_writes;
                }

                tex->Release();
            }
        }

        if (!interesting && dsv_tex)
        {
            D3D11_TEXTURE2D_DESC dd{};
            dsv_tex->GetDesc(&dd);

            if (g_last_depth_tex &&
                dd.Width == 3840 &&
                dd.Height == 2160)
            {
                interesting = true;
                kind = "depth_dsv";
                ++g_res_arch_depth_writes;
            }
        }

        if (!interesting)
        {
            if (dsv_tex) dsv_tex->Release();
            for (auto *rtv : rtvs) if (rtv) rtv->Release();
            if (dsv) dsv->Release();
            return;
        }

        D3D11_VIEWPORT vps[8]{};
        UINT vp_count = 8;
        ctx->RSGetViewports(&vp_count, vps);

        D3D11_RECT scissors[8]{};
        UINT sc_count = 8;
        ctx->RSGetScissorRects(&sc_count, scissors);

        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ctx->IAGetPrimitiveTopology(&topology);

        const DxbcHash vsh = shader_hash(g_current_vs);
        const DxbcHash psh = shader_hash(g_current_ps);

        ++g_res_arch_events;

        log_line(
            "RES_ARCH DRAW event=%llu frame=%llu draw=%llu target=%s kind=%s count=%u start=%u base=%d topology=%u vpCount=%u vp0=(%.1f,%.1f %.1fx%.1f z=%.3f..%.3f) scCount=%u sc0=(%ld,%ld)-(%ld,%ld) VS=%p VSHASH=%08X-%08X-%08X-%08X PS=%p PSHASH=%08X-%08X-%08X-%08X DSV=%p",
            static_cast<unsigned long long>(g_res_arch_events),
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_draw_counter),
            kind,
            g_probe_draw_kind,
            g_probe_draw_count,
            g_probe_draw_start,
            g_probe_draw_base_vertex,
            static_cast<uint32_t>(topology),
            vp_count,
            vp_count ? vps[0].TopLeftX : 0.0f,
            vp_count ? vps[0].TopLeftY : 0.0f,
            vp_count ? vps[0].Width : 0.0f,
            vp_count ? vps[0].Height : 0.0f,
            vp_count ? vps[0].MinDepth : 0.0f,
            vp_count ? vps[0].MaxDepth : 0.0f,
            sc_count,
            sc_count ? scissors[0].left : 0L,
            sc_count ? scissors[0].top : 0L,
            sc_count ? scissors[0].right : 0L,
            sc_count ? scissors[0].bottom : 0L,
            g_current_vs,
            vsh.a, vsh.b, vsh.c, vsh.d,
            g_current_ps,
            psh.a, psh.b, psh.c, psh.d,
            dsv_tex);

        for (UINT i = 0; i < 8; ++i)
        {
            if (!rtvs[i])
                continue;

            ID3D11Texture2D *tex = nullptr;
            get_texture2d_from_rtv(rtvs[i], &tex);

            if (tex)
            {
                D3D11_TEXTURE2D_DESC d{};
                tex->GetDesc(&d);
                const TextureProbeInfo *info = lookup_texture_probe(tex);

                log_line(
                    "RES_ARCH RTV event=%llu slot=%u tex=%p serial=%llu target=%s %ux%u fmt=%u bind=0x%X misc=0x%X samples=%u",
                    static_cast<unsigned long long>(g_res_arch_events),
                    i,
                    tex,
                    static_cast<unsigned long long>(info ? info->serial : 0ull),
                    primary_target_name(tex),
                    d.Width,
                    d.Height,
                    static_cast<uint32_t>(d.Format),
                    d.BindFlags,
                    d.MiscFlags,
                    d.SampleDesc.Count);

                tex->Release();
            }
        }

        if (dsv_tex)
        {
            D3D11_TEXTURE2D_DESC d{};
            dsv_tex->GetDesc(&d);
            const TextureProbeInfo *info = lookup_texture_probe(dsv_tex);

            log_line(
                "RES_ARCH DSV event=%llu tex=%p serial=%llu %ux%u fmt=%u bind=0x%X misc=0x%X samples=%u",
                static_cast<unsigned long long>(g_res_arch_events),
                dsv_tex,
                static_cast<unsigned long long>(info ? info->serial : 0ull),
                d.Width,
                d.Height,
                static_cast<uint32_t>(d.Format),
                d.BindFlags,
                d.MiscFlags,
                d.SampleDesc.Count);

            dsv_tex->Release();
        }

        for (auto *rtv : rtvs)
            if (rtv) rtv->Release();

        if (dsv)
            dsv->Release();
    }

    struct NativeQualityDrawState
    {
        bool active = false;
        UINT old_vp_count = 0;
        D3D11_VIEWPORT old_vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT old_sc_count = 0;
        D3D11_RECT old_scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};

        ID3D11Buffer *jitter_cb = nullptr;
        bool jitter_scaled = false;
        float old_jitter_x = 0.0f;
        float old_jitter_y = 0.0f;
    };

    bool current_draw_is_native_quality_primary(ID3D11DeviceContext *ctx)
    {
        if (!g_native_quality_test_enabled || !ctx)
            return false;

        if (g_probe_draw_kind == nullptr ||
            strcmp(g_probe_draw_kind, "DrawIndexed") != 0 ||
            g_probe_draw_count < 6)
        {
            ++g_native_quality_rejected_fullscreen;
            return false;
        }

        D3D11_VIEWPORT vp{};
        UINT vp_count = 1;
        ctx->RSGetViewports(&vp_count, &vp);

        if (vp_count == 0 ||
            fabsf(vp.Width - 3840.0f) > 0.5f ||
            fabsf(vp.Height - 2160.0f) > 0.5f)
        {
            return false;
        }

        ID3D11RenderTargetView *rtvs[8]{};
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(8, rtvs, &dsv);

        bool primary_depth = false;
        bool primary_color = false;

        if (dsv)
        {
            ID3D11Texture2D *depth_tex = dsv_to_texture2d(dsv);

            if (depth_tex)
            {
                const TextureProbeInfo *info = lookup_texture_probe(depth_tex);

                primary_depth = info && info->serial == 1ull;

                depth_tex->Release();
            }
        }

        for (UINT i = 0; i < 8; ++i)
        {
            if (!rtvs[i])
                continue;

            ID3D11Texture2D *tex = nullptr;
            get_texture2d_from_rtv(rtvs[i], &tex);

            if (tex)
            {
                D3D11_TEXTURE2D_DESC d{};
                tex->GetDesc(&d);
                const TextureProbeInfo *info = lookup_texture_probe(tex);

                if (d.Width == 3840 &&
                    d.Height == 2160 &&
                    static_cast<uint32_t>(d.Format) == 24u &&
                    (!info || info->serial == 0ull))
                {
                    primary_color = true;
                }

                tex->Release();
            }
        }

        for (auto *rtv : rtvs)
            if (rtv) rtv->Release();

        if (dsv)
            dsv->Release();

        const bool selected = primary_depth && primary_color;

        if (!selected)
            ++g_native_quality_rejected_target;

        return selected;
    }

    void maybe_scale_native_quality_jitter(
        ID3D11DeviceContext *ctx,
        NativeQualityDrawState &state)
    {
        if (!ctx)
            return;

        ID3D11Buffer *cb = nullptr;
        ctx->VSGetConstantBuffers(0, 1, &cb);

        if (!cb)
            return;

        D3D11_BUFFER_DESC desc{};
        cb->GetDesc(&desc);

        if (desc.ByteWidth < 156 ||
            desc.Usage != D3D11_USAGE_DYNAMIC ||
            (desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) == 0)
        {
            cb->Release();
            return;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr =
            ctx->Map(cb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);

        if (FAILED(hr) || !mapped.pData)
        {
            cb->Release();
            return;
        }

        float *f = reinterpret_cast<float *>(mapped.pData);

        const float x = f[34];
        const float y = f[38];

        const float expected_x = 0.5f / 3840.0f;
        const float expected_y = 0.5f / 2160.0f;

        if (fabsf(fabsf(x) - expected_x) < 0.0000005f &&
            fabsf(fabsf(y) - expected_y) < 0.0000005f)
        {
            state.old_jitter_x = x;
            state.old_jitter_y = y;

            f[34] = x * 1.5f;
            f[38] = y * 1.5f;

            state.jitter_cb = cb;
            state.jitter_scaled = true;
            ++g_native_quality_jitter_scales;

            ctx->Unmap(cb, 0);
            return;
        }

        ctx->Unmap(cb, 0);
        cb->Release();
    }

    bool apply_native_quality_draw_state(
        ID3D11DeviceContext *ctx,
        NativeQualityDrawState &state)
    {
        state = {};

        if (!current_draw_is_native_quality_primary(ctx))
            return false;

        state.old_vp_count =
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ctx->RSGetViewports(&state.old_vp_count, state.old_vps);

        state.old_sc_count =
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ctx->RSGetScissorRects(&state.old_sc_count, state.old_scissors);

        if (state.old_vp_count == 0)
            return false;

        D3D11_VIEWPORT vp = state.old_vps[0];
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(NATIVE_QUALITY_W);
        vp.Height = static_cast<float>(NATIVE_QUALITY_H);

        g_orig_rs_set_viewports(ctx, 1, &vp);

        D3D11_RECT sc{};
        sc.left = 0;
        sc.top = 0;
        sc.right = static_cast<LONG>(NATIVE_QUALITY_W);
        sc.bottom = static_cast<LONG>(NATIVE_QUALITY_H);
        ctx->RSSetScissorRects(1, &sc);

        state.active = true;
        ++g_native_quality_draws;

        maybe_scale_native_quality_jitter(ctx, state);

        if (g_native_quality_draws <= 12 ||
            (g_native_quality_draws % 2000ull) == 0)
        {
            log_line(
                "NATIVE_QUALITY_DRAW count=%llu frame=%llu draw=%llu kind=%s raster=%ux%u jitterScaled=%d",
                static_cast<unsigned long long>(g_native_quality_draws),
                static_cast<unsigned long long>(g_render_frame),
                static_cast<unsigned long long>(g_draw_counter),
                g_probe_draw_kind,
                NATIVE_QUALITY_W,
                NATIVE_QUALITY_H,
                state.jitter_scaled ? 1 : 0);
        }

        return true;
    }

    void restore_native_quality_draw_state(
        ID3D11DeviceContext *ctx,
        NativeQualityDrawState &state)
    {
        if (!state.active || !ctx)
            return;

        if (state.jitter_scaled && state.jitter_cb)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};

            if (SUCCEEDED(ctx->Map(
                    state.jitter_cb,
                    0,
                    D3D11_MAP_WRITE_NO_OVERWRITE,
                    0,
                    &mapped)) &&
                mapped.pData)
            {
                float *f = reinterpret_cast<float *>(mapped.pData);
                f[34] = state.old_jitter_x;
                f[38] = state.old_jitter_y;
                ctx->Unmap(state.jitter_cb, 0);
            }

            state.jitter_cb->Release();
            state.jitter_cb = nullptr;
        }

        if (state.old_vp_count > 0)
            g_orig_rs_set_viewports(
                ctx,
                state.old_vp_count,
                state.old_vps);

        if (state.old_sc_count > 0)
            ctx->RSSetScissorRects(
                state.old_sc_count,
                state.old_scissors);

        state.active = false;
    }

    void refresh_primary_geometry_resources()
    {

    }

    void start_resource_family_probe()
    {
        g_resource_family_probe_active = true;
        g_resource_family_probe_end_frame = g_render_frame + 10ull;
        g_resource_family_events = 0;
        g_resource_family_reads = 0;
        g_resource_family_writes = 0;
        g_resource_family_scene_hits = 0;
        g_resource_family_depth_hits = 0;

        if (g_release_verbose_diagnostics)
        {
            log_line(
            "RESOURCE_FAMILY START_SAFE frame=%llu through=%llu primaryColor=%p primaryDepth=%p scene=%p motion=%p depth=%p",
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_resource_family_probe_end_frame),
            g_primary_geom_color,
            g_primary_geom_depth,
            g_last_scene_tex,
            g_last_velocity_tex,
            g_last_depth_tex);
        }
    }

    void stop_resource_family_probe(const char *reason)
    {
        if (!g_resource_family_probe_active)
            return;

        g_resource_family_probe_active = false;

        if (g_release_verbose_diagnostics)
        {
            log_line(
            "RESOURCE_FAMILY STOP reason=%s frame=%llu events=%llu reads=%llu writes=%llu sceneHits=%llu depthHits=%llu",
            reason ? reason : "unknown",
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_resource_family_events),
            static_cast<unsigned long long>(g_resource_family_reads),
            static_cast<unsigned long long>(g_resource_family_writes),
            static_cast<unsigned long long>(g_resource_family_scene_hits),
            static_cast<unsigned long long>(g_resource_family_depth_hits));
        }
    }

    bool texture_matches_family(ID3D11Texture2D *tex, const char **name)
    {
        if (name)
            *name = "other";

        if (!tex)
            return false;

        if (tex == g_primary_geom_color)
        {
            if (name) *name = "primaryColor";
            return true;
        }

        if (tex == g_primary_geom_depth)
        {
            if (name) *name = "primaryDepth";
            return true;
        }

        if (tex == g_last_scene_tex || tex == g_probe_scene_color)
        {
            if (name) *name = "sceneColor";
            return true;
        }

        if (tex == g_last_velocity_tex)
        {
            if (name) *name = "motion";
            return true;
        }

        if (tex == g_last_depth_tex)
        {
            if (name) *name = "linearDepth";
            return true;
        }

        return false;
    }

    void log_resource_family_usage(ID3D11DeviceContext *ctx)
    {
        if (!g_resource_family_probe_active || !ctx)
            return;

        if (g_render_frame > g_resource_family_probe_end_frame)
        {
            stop_resource_family_probe("frame-window");
            return;
        }

        if (g_resource_family_events >= RESOURCE_FAMILY_EVENT_CAP)
        {
            stop_resource_family_probe("event-cap");
            return;
        }

        bool logged = false;

        ID3D11RenderTargetView *rtvs[8]{};
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(8, rtvs, &dsv);

        for (UINT slot = 0; slot < 8; ++slot)
        {
            if (!rtvs[slot])
                continue;

            ID3D11Texture2D *tex = nullptr;
            get_texture2d_from_rtv(rtvs[slot], &tex);

            if (tex)
            {
                const char *name = nullptr;

                if (texture_matches_family(tex, &name))
                {
                    D3D11_TEXTURE2D_DESC d{};
                    tex->GetDesc(&d);
                    const TextureProbeInfo *info = lookup_texture_probe(tex);
                    const DxbcHash vsh = shader_hash(g_current_vs);
                    const DxbcHash psh = shader_hash(g_current_ps);

                    ++g_resource_family_events;
                    ++g_resource_family_writes;

                    if (tex == g_last_scene_tex || tex == g_probe_scene_color)
                        ++g_resource_family_scene_hits;

                    if (g_release_verbose_diagnostics)
                    {
                        log_line(
                        "RESOURCE_FAMILY WRITE event=%llu frame=%llu draw=%llu target=%s slot=%u tex=%p serial=%llu %ux%u fmt=%u kind=%s count=%u VS=%08X-%08X-%08X-%08X PS=%08X-%08X-%08X-%08X",
                        static_cast<unsigned long long>(g_resource_family_events),
                        static_cast<unsigned long long>(g_render_frame),
                        static_cast<unsigned long long>(g_draw_counter),
                        name,
                        slot,
                        tex,
                        static_cast<unsigned long long>(info ? info->serial : 0ull),
                        d.Width,
                        d.Height,
                        static_cast<uint32_t>(d.Format),
                        g_probe_draw_kind,
                        g_probe_draw_count,
                        vsh.a, vsh.b, vsh.c, vsh.d,
                        psh.a, psh.b, psh.c, psh.d);
                    }

                    logged = true;
                }

                tex->Release();
            }
        }

        if (dsv)
        {
            ID3D11Texture2D *tex = dsv_to_texture2d(dsv);

            if (tex)
            {
                const char *name = nullptr;

                if (texture_matches_family(tex, &name))
                {
                    D3D11_TEXTURE2D_DESC d{};
                    tex->GetDesc(&d);
                    const TextureProbeInfo *info = lookup_texture_probe(tex);
                    const DxbcHash vsh = shader_hash(g_current_vs);
                    const DxbcHash psh = shader_hash(g_current_ps);

                    ++g_resource_family_events;
                    ++g_resource_family_writes;
                    ++g_resource_family_depth_hits;

                    if (g_release_verbose_diagnostics)
                    {
                        log_line(
                        "RESOURCE_FAMILY WRITE_DSV event=%llu frame=%llu draw=%llu target=%s tex=%p serial=%llu %ux%u fmt=%u kind=%s count=%u VS=%08X-%08X-%08X-%08X PS=%08X-%08X-%08X-%08X",
                        static_cast<unsigned long long>(g_resource_family_events),
                        static_cast<unsigned long long>(g_render_frame),
                        static_cast<unsigned long long>(g_draw_counter),
                        name,
                        tex,
                        static_cast<unsigned long long>(info ? info->serial : 0ull),
                        d.Width,
                        d.Height,
                        static_cast<uint32_t>(d.Format),
                        g_probe_draw_kind,
                        g_probe_draw_count,
                        vsh.a, vsh.b, vsh.c, vsh.d,
                        psh.a, psh.b, psh.c, psh.d);
                    }

                    logged = true;
                }

                tex->Release();
            }
        }

        for (auto *rtv : rtvs)
            if (rtv) rtv->Release();
        if (dsv)
            dsv->Release();

        ID3D11ShaderResourceView *srvs[16]{};
        ctx->PSGetShaderResources(0, 16, srvs);

        for (UINT slot = 0; slot < 16; ++slot)
        {
            if (!srvs[slot])
                continue;

            ID3D11Resource *res = nullptr;
            srvs[slot]->GetResource(&res);

            ID3D11Texture2D *tex = nullptr;
            if (res)
                res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&tex));

            if (tex)
            {
                const char *name = nullptr;

                if (texture_matches_family(tex, &name))
                {
                    D3D11_TEXTURE2D_DESC d{};
                    tex->GetDesc(&d);
                    const TextureProbeInfo *info = lookup_texture_probe(tex);
                    const DxbcHash vsh = shader_hash(g_current_vs);
                    const DxbcHash psh = shader_hash(g_current_ps);

                    ++g_resource_family_events;
                    ++g_resource_family_reads;

                    if (tex == g_last_scene_tex || tex == g_probe_scene_color)
                        ++g_resource_family_scene_hits;

                    if (tex == g_last_depth_tex || tex == g_primary_geom_depth)
                        ++g_resource_family_depth_hits;

                    if (g_release_verbose_diagnostics)
                    {
                        log_line(
                        "RESOURCE_FAMILY READ event=%llu frame=%llu draw=%llu source=%s slot=%u tex=%p serial=%llu %ux%u fmt=%u kind=%s count=%u VS=%08X-%08X-%08X-%08X PS=%08X-%08X-%08X-%08X",
                        static_cast<unsigned long long>(g_resource_family_events),
                        static_cast<unsigned long long>(g_render_frame),
                        static_cast<unsigned long long>(g_draw_counter),
                        name,
                        slot,
                        tex,
                        static_cast<unsigned long long>(info ? info->serial : 0ull),
                        d.Width,
                        d.Height,
                        static_cast<uint32_t>(d.Format),
                        g_probe_draw_kind,
                        g_probe_draw_count,
                        vsh.a, vsh.b, vsh.c, vsh.d,
                        psh.a, psh.b, psh.c, psh.d);
                    }

                    logged = true;
                }

                tex->Release();
            }

            if (res)
                res->Release();

            srvs[slot]->Release();

            if (g_resource_family_events >= RESOURCE_FAMILY_EVENT_CAP)
                break;
        }

        if (g_resource_family_events >= RESOURCE_FAMILY_EVENT_CAP)
            stop_resource_family_probe("event-cap");
    }

    void recompute_transition_read_flags()
    {
        g_transition_reads_primary_color = false;
        g_transition_reads_primary_depth = false;
        g_transition_reads_linear_depth = false;
        g_transition_reads_motion = false;
        g_transition_reads_scene = false;

        for (const auto role : g_transition_ps_srv_roles)
        {
            switch (role)
            {
            case TransitionSrvRole::primary_color:
                g_transition_reads_primary_color = true;
                break;
            case TransitionSrvRole::primary_depth:
                g_transition_reads_primary_depth = true;
                break;
            case TransitionSrvRole::linear_depth:
                g_transition_reads_linear_depth = true;
                break;
            case TransitionSrvRole::motion:
                g_transition_reads_motion = true;
                break;
            case TransitionSrvRole::scene:
                g_transition_reads_scene = true;
                break;
            default:
                break;
            }
        }
    }

    void clear_transition_bind_flags()
    {
        g_transition_bound_primary_color = false;
        g_transition_bound_primary_depth = false;
        g_transition_bound_linear_depth_rtv = false;
        g_transition_bound_motion_rtv = false;
        g_transition_bound_scene_rtv = false;

        for (auto &role : g_transition_ps_srv_roles)
            role = TransitionSrvRole::none;

        recompute_transition_read_flags();
    }

    void start_transition_probe()
    {
        clear_transition_bind_flags();

        g_transition_probe_active = true;
        g_transition_probe_end_frame = g_render_frame + 12ull;
        g_transition_probe_events = 0;
        g_transition_primary_writes = 0;
        g_transition_linear_depth_writes = 0;
        g_transition_motion_writes = 0;
        g_transition_scene_writes = 0;
        g_transition_last_logged_stage = TransitionStage::none;
        g_transition_seen_pair_count = 0;
        for (auto &pair : g_transition_seen_pairs)
            pair = {};

        log_line(
            "TRANSITION_PROBE START frame=%llu through=%llu primaryColor=%p primaryDepth=%p linearDepth=%p motion=%p scene=%p",
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_transition_probe_end_frame),
            g_primary_geom_color,
            g_primary_geom_depth,
            g_last_depth_tex,
            g_last_velocity_tex,
            g_last_scene_tex);
    }

    void stop_transition_probe(const char *reason)
    {
        if (!g_transition_probe_active)
            return;

        g_transition_probe_active = false;

        log_line(
            "TRANSITION_PROBE STOP reason=%s frame=%llu events=%llu primaryWrites=%llu linearDepthWrites=%llu motionWrites=%llu sceneWrites=%llu",
            reason ? reason : "unknown",
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_transition_probe_events),
            static_cast<unsigned long long>(g_transition_primary_writes),
            static_cast<unsigned long long>(g_transition_linear_depth_writes),
            static_cast<unsigned long long>(g_transition_motion_writes),
            static_cast<unsigned long long>(g_transition_scene_writes));
    }

    void refresh_transition_read_flags(
        UINT start_slot,
        UINT num_views,
        ID3D11ShaderResourceView *const *views)
    {
        if (!g_transition_probe_active)
            return;

        for (UINT i = 0; i < num_views; ++i)
        {
            const UINT slot = start_slot + i;
            if (slot >= 16)
                continue;

            TransitionSrvRole role = TransitionSrvRole::none;
            ID3D11Texture2D *tex = nullptr;

            if (views && views[i])
                get_texture2d_from_srv(views[i], &tex);

            if (tex)
            {
                if (tex == g_primary_geom_color)
                    role = TransitionSrvRole::primary_color;
                else if (tex == g_primary_geom_depth)
                    role = TransitionSrvRole::primary_depth;
                else if (tex == g_last_depth_tex)
                    role = TransitionSrvRole::linear_depth;
                else if (tex == g_last_velocity_tex)
                    role = TransitionSrvRole::motion;
                else if (tex == g_last_scene_tex || tex == g_probe_scene_color)
                    role = TransitionSrvRole::scene;

                tex->Release();
            }

            g_transition_ps_srv_roles[slot] = role;
        }

        recompute_transition_read_flags();
    }

    void refresh_transition_output_flags(
        UINT num_views,
        ID3D11RenderTargetView *const *views,
        ID3D11DepthStencilView *dsv)
    {
        if (!g_transition_probe_active &&
            !g_primary_interval_test_enabled)
        {
            return;
        }

        g_transition_bound_primary_color = false;
        g_transition_bound_primary_depth = false;
        g_transition_bound_linear_depth_rtv = false;
        g_transition_bound_motion_rtv = false;
        g_transition_bound_scene_rtv = false;

        for (UINT i = 0; i < num_views; ++i)
        {
            if (!views || !views[i])
                continue;

            ID3D11Texture2D *tex = nullptr;
            get_texture2d_from_rtv(views[i], &tex);

            if (tex)
            {
                if (tex == g_primary_geom_color)
                    g_transition_bound_primary_color = true;
                if (tex == g_last_depth_tex)
                    g_transition_bound_linear_depth_rtv = true;
                if (tex == g_last_velocity_tex)
                    g_transition_bound_motion_rtv = true;
                if (tex == g_last_scene_tex || tex == g_probe_scene_color)
                    g_transition_bound_scene_rtv = true;

                tex->Release();
            }
        }

        if (dsv)
        {
            ID3D11Texture2D *tex = dsv_to_texture2d(dsv);

            if (tex)
            {
                if (tex == g_primary_geom_depth)
                    g_transition_bound_primary_depth = true;

                tex->Release();
            }
        }
    }

    TransitionStage current_transition_stage()
    {
        if (!g_transition_probe_active)
            return TransitionStage::none;

        if (g_transition_bound_primary_color &&
            g_transition_bound_primary_depth)
            return TransitionStage::primary_geometry;

        if (g_transition_bound_linear_depth_rtv)
            return TransitionStage::linear_depth;

        if (g_transition_bound_motion_rtv)
            return TransitionStage::motion;

        if (g_transition_bound_scene_rtv &&
            (g_transition_reads_primary_color ||
             g_transition_reads_primary_depth ||
             g_transition_reads_linear_depth ||
             g_transition_reads_motion))
            return TransitionStage::scene_color;

        return TransitionStage::none;
    }

    const char *transition_stage_name(TransitionStage stage)
    {
        switch (stage)
        {
        case TransitionStage::primary_geometry: return "PRIMARY_GEOMETRY";
        case TransitionStage::linear_depth:     return "LINEAR_DEPTH";
        case TransitionStage::motion:           return "MOTION";
        case TransitionStage::scene_color:      return "SCENE_COLOR";
        default:                                return "NONE";
        }
    }

    bool transition_hash_equal(const DxbcHash &a, const DxbcHash &b)
    {
        return a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d;
    }

    bool transition_pair_seen(
        TransitionStage stage,
        const DxbcHash &vs,
        const DxbcHash &ps)
    {
        for (uint32_t i = 0; i < g_transition_seen_pair_count; ++i)
        {
            const auto &pair = g_transition_seen_pairs[i];
            if (pair.used &&
                pair.stage == stage &&
                transition_hash_equal(pair.vs, vs) &&
                transition_hash_equal(pair.ps, ps))
                return true;
        }

        return false;
    }

    void remember_transition_pair(
        TransitionStage stage,
        const DxbcHash &vs,
        const DxbcHash &ps)
    {
        if (g_transition_seen_pair_count >=
            static_cast<uint32_t>(std::size(g_transition_seen_pairs)))
            return;

        auto &pair = g_transition_seen_pairs[g_transition_seen_pair_count++];
        pair.used = true;
        pair.stage = stage;
        pair.vs = vs;
        pair.ps = ps;
    }

    void log_transition_draw(ID3D11DeviceContext *ctx)
    {
        if (!g_transition_probe_active || !ctx)
            return;

        if (g_render_frame > g_transition_probe_end_frame)
        {
            stop_transition_probe("frame-window");
            return;
        }

        if (g_transition_probe_events >= TRANSITION_PROBE_EVENT_CAP)
        {
            stop_transition_probe("event-cap");
            return;
        }

        const TransitionStage stage = current_transition_stage();
        if (stage == TransitionStage::none)
            return;

        const DxbcHash vsh = shader_hash(g_current_vs);
        const DxbcHash psh = shader_hash(g_current_ps);

        const bool stage_changed =
            stage != g_transition_last_logged_stage;

        const bool new_pair =
            !transition_pair_seen(stage, vsh, psh);

        if (!stage_changed && !new_pair)
            return;

        if (new_pair)
            remember_transition_pair(stage, vsh, psh);

        g_transition_last_logged_stage = stage;

        D3D11_VIEWPORT vps[4]{};
        UINT vp_count = 4;
        ctx->RSGetViewports(&vp_count, vps);

        D3D11_RECT sc[4]{};
        UINT sc_count = 4;
        ctx->RSGetScissorRects(&sc_count, sc);

        D3D11_PRIMITIVE_TOPOLOGY topology =
            D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ctx->IAGetPrimitiveTopology(&topology);

        ++g_transition_probe_events;

        switch (stage)
        {
        case TransitionStage::primary_geometry:
            ++g_transition_primary_writes;
            break;
        case TransitionStage::linear_depth:
            ++g_transition_linear_depth_writes;
            break;
        case TransitionStage::motion:
            ++g_transition_motion_writes;
            break;
        case TransitionStage::scene_color:
            ++g_transition_scene_writes;
            break;
        default:
            break;
        }

        log_line(
            "HANDOFF event=%llu frame=%llu draw=%llu stage=%s stageChange=%d newPair=%d kind=%s count=%u start=%u base=%d topology=%u vp0=%.1fx%.1f sc0=(%ld,%ld)-(%ld,%ld) OUT[pColor=%d pDepth=%d linDepth=%d motion=%d scene=%d] IN[pColor=%d pDepth=%d linDepth=%d motion=%d scene=%d] VS=%08X-%08X-%08X-%08X PS=%08X-%08X-%08X-%08X",
            static_cast<unsigned long long>(g_transition_probe_events),
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_draw_counter),
            transition_stage_name(stage),
            stage_changed ? 1 : 0,
            new_pair ? 1 : 0,
            g_probe_draw_kind,
            g_probe_draw_count,
            g_probe_draw_start,
            g_probe_draw_base_vertex,
            static_cast<uint32_t>(topology),
            vp_count ? vps[0].Width : 0.0f,
            vp_count ? vps[0].Height : 0.0f,
            sc_count ? sc[0].left : 0L,
            sc_count ? sc[0].top : 0L,
            sc_count ? sc[0].right : 0L,
            sc_count ? sc[0].bottom : 0L,
            g_transition_bound_primary_color ? 1 : 0,
            g_transition_bound_primary_depth ? 1 : 0,
            g_transition_bound_linear_depth_rtv ? 1 : 0,
            g_transition_bound_motion_rtv ? 1 : 0,
            g_transition_bound_scene_rtv ? 1 : 0,
            g_transition_reads_primary_color ? 1 : 0,
            g_transition_reads_primary_depth ? 1 : 0,
            g_transition_reads_linear_depth ? 1 : 0,
            g_transition_reads_motion ? 1 : 0,
            g_transition_reads_scene ? 1 : 0,
            vsh.a, vsh.b, vsh.c, vsh.d,
            psh.a, psh.b, psh.c, psh.d);

        if (g_transition_probe_events >= TRANSITION_PROBE_EVENT_CAP)
            stop_transition_probe("event-cap");
    }

    struct PrimaryIntervalDrawState
    {
        bool active = false;
        UINT old_vp_count = 0;
        D3D11_VIEWPORT old_vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT old_sc_count = 0;
        D3D11_RECT old_scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    };

    bool current_draw_is_primary_interval()
    {
        return
            g_primary_interval_test_enabled &&
            g_transition_bound_primary_color &&
            g_transition_bound_primary_depth;
    }

    bool apply_primary_interval_state(
        ID3D11DeviceContext *ctx,
        PrimaryIntervalDrawState &state)
    {
        state = {};

        if (!ctx || !current_draw_is_primary_interval())
            return false;

        state.old_vp_count =
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ctx->RSGetViewports(&state.old_vp_count, state.old_vps);

        state.old_sc_count =
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ctx->RSGetScissorRects(&state.old_sc_count, state.old_scissors);

        if (state.old_vp_count == 0)
            return false;

        if (fabsf(state.old_vps[0].Width - 3840.0f) > 0.5f ||
            fabsf(state.old_vps[0].Height - 2160.0f) > 0.5f)
        {
            return false;
        }

        D3D11_VIEWPORT vp = state.old_vps[0];
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(PRIMARY_INTERVAL_W);
        vp.Height = static_cast<float>(PRIMARY_INTERVAL_H);

        g_orig_rs_set_viewports(ctx, 1, &vp);

        D3D11_RECT sc{};
        sc.left = 0;
        sc.top = 0;
        sc.right = static_cast<LONG>(PRIMARY_INTERVAL_W);
        sc.bottom = static_cast<LONG>(PRIMARY_INTERVAL_H);
        ctx->RSSetScissorRects(1, &sc);

        state.active = true;
        ++g_primary_interval_draws;

        if (g_primary_interval_last_frame != g_render_frame)
        {
            g_primary_interval_last_frame = g_render_frame;
            ++g_primary_interval_frames;
        }

        if (g_primary_interval_draws <= 12 ||
            (g_primary_interval_draws % 5000ull) == 0)
        {
            const DxbcHash vsh = shader_hash(g_current_vs);
            const DxbcHash psh = shader_hash(g_current_ps);

            log_line(
                "PRIMARY_INTERVAL_DRAW count=%llu frame=%llu draw=%llu kind=%s vertices=%u raster=%ux%u VS=%08X-%08X-%08X-%08X PS=%08X-%08X-%08X-%08X",
                static_cast<unsigned long long>(g_primary_interval_draws),
                static_cast<unsigned long long>(g_render_frame),
                static_cast<unsigned long long>(g_draw_counter),
                g_probe_draw_kind,
                g_probe_draw_count,
                PRIMARY_INTERVAL_W,
                PRIMARY_INTERVAL_H,
                vsh.a, vsh.b, vsh.c, vsh.d,
                psh.a, psh.b, psh.c, psh.d);
        }

        return true;
    }

    void restore_primary_interval_state(
        ID3D11DeviceContext *ctx,
        PrimaryIntervalDrawState &state)
    {
        if (!ctx || !state.active)
            return;

        if (state.old_vp_count > 0)
            g_orig_rs_set_viewports(
                ctx,
                state.old_vp_count,
                state.old_vps);

        if (state.old_sc_count > 0)
            ctx->RSSetScissorRects(
                state.old_sc_count,
                state.old_scissors);

        state.active = false;
    }

    void start_handoff42_probe()
    {
        g_handoff42_active = true;
        g_handoff42_end_frame = g_render_frame + 10ull;
        g_handoff42_events = 0;
        g_handoff42_draws = 0;
        g_handoff42_copies = 0;
        g_handoff42_resolves = 0;
        g_handoff42_om_binds = 0;
        g_handoff42_ps_binds = 0;

        log_line(
            "HANDOFF42 START frame=%llu through=%llu primaryColor=%p primaryDepth=%p sceneColor=%p linearDepth=%p motion=%p",
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_handoff42_end_frame),
            g_primary_geom_color,
            g_primary_geom_depth,
            g_last_scene_tex,
            g_last_depth_tex,
            g_last_velocity_tex);
    }

    void stop_handoff42_probe(const char *reason)
    {
        if (!g_handoff42_active)
            return;

        g_handoff42_active = false;

        log_line(
            "HANDOFF42 STOP reason=%s frame=%llu events=%llu draws=%llu copies=%llu resolves=%llu om=%llu ps=%llu",
            reason ? reason : "unknown",
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_handoff42_events),
            static_cast<unsigned long long>(g_handoff42_draws),
            static_cast<unsigned long long>(g_handoff42_copies),
            static_cast<unsigned long long>(g_handoff42_resolves),
            static_cast<unsigned long long>(g_handoff42_om_binds),
            static_cast<unsigned long long>(g_handoff42_ps_binds));
    }

    bool handoff42_texture_is_tracked(ID3D11Texture2D *tex, const char **role)
    {
        if (role)
            *role = "other";

        if (!tex)
            return false;

        if (tex == g_primary_geom_color)
        {
            if (role) *role = "primaryColor";
            return true;
        }

        if (tex == g_primary_geom_depth)
        {
            if (role) *role = "primaryDepth";
            return true;
        }

        if (tex == g_last_scene_tex || tex == g_probe_scene_color)
        {
            if (role) *role = "sceneColor";
            return true;
        }

        if (tex == g_last_depth_tex)
        {
            if (role) *role = "linearDepth";
            return true;
        }

        if (tex == g_last_velocity_tex)
        {
            if (role) *role = "motion";
            return true;
        }

        return false;
    }

    void log_handoff42_draw(ID3D11DeviceContext *ctx)
    {
        if (!g_handoff42_active || !ctx)
            return;

        if (g_render_frame > g_handoff42_end_frame)
        {
            stop_handoff42_probe("frame-window");
            return;
        }

        if (g_handoff42_events >= HANDOFF42_EVENT_CAP)
        {
            stop_handoff42_probe("event-cap");
            return;
        }

        ID3D11RenderTargetView *rtvs[8]{};
        ID3D11DepthStencilView *dsv = nullptr;
        ctx->OMGetRenderTargets(8, rtvs, &dsv);

        bool interesting = false;
        char out_roles[160]{};
        size_t used = 0;

        for (UINT i = 0; i < 8; ++i)
        {
            if (!rtvs[i])
                continue;

            ID3D11Texture2D *tex = nullptr;
            get_texture2d_from_rtv(rtvs[i], &tex);

            if (tex)
            {
                const char *role = nullptr;

                if (handoff42_texture_is_tracked(tex, &role))
                {
                    interesting = true;
                    const int n = snprintf(
                        out_roles + used,
                        sizeof(out_roles) - used,
                        "%sRTV%u=%s",
                        used ? "," : "",
                        i,
                        role);

                    if (n > 0)
                        used += static_cast<size_t>(n);
                }

                tex->Release();
            }
        }

        if (dsv)
        {
            ID3D11Texture2D *tex = dsv_to_texture2d(dsv);

            if (tex)
            {
                const char *role = nullptr;

                if (handoff42_texture_is_tracked(tex, &role))
                {
                    interesting = true;
                    const int n = snprintf(
                        out_roles + used,
                        sizeof(out_roles) - used,
                        "%sDSV=%s",
                        used ? "," : "",
                        role);

                    if (n > 0)
                        used += static_cast<size_t>(n);
                }

                tex->Release();
            }
        }

        for (auto *rtv : rtvs)
            if (rtv) rtv->Release();

        if (dsv)
            dsv->Release();

        if (!interesting)
            return;

        ID3D11ShaderResourceView *srvs[16]{};
        ctx->PSGetShaderResources(0, 16, srvs);

        char in_roles[220]{};
        used = 0;

        for (UINT i = 0; i < 16; ++i)
        {
            if (!srvs[i])
                continue;

            ID3D11Resource *res = nullptr;
            srvs[i]->GetResource(&res);

            ID3D11Texture2D *tex = nullptr;
            if (res)
            {
                res->QueryInterface(
                    __uuidof(ID3D11Texture2D),
                    reinterpret_cast<void **>(&tex));
            }

            if (tex)
            {
                const char *role = nullptr;

                if (handoff42_texture_is_tracked(tex, &role))
                {
                    const int n = snprintf(
                        in_roles + used,
                        sizeof(in_roles) - used,
                        "%sPS%u=%s",
                        used ? "," : "",
                        i,
                        role);

                    if (n > 0)
                        used += static_cast<size_t>(n);
                }

                tex->Release();
            }

            if (res)
                res->Release();

            srvs[i]->Release();
        }

        D3D11_VIEWPORT vp{};
        UINT vp_count = 1;
        ctx->RSGetViewports(&vp_count, &vp);

        const DxbcHash vsh = shader_hash(g_current_vs);
        const DxbcHash psh = shader_hash(g_current_ps);

        ++g_handoff42_events;
        ++g_handoff42_draws;

        log_line(
            "HANDOFF42 DRAW event=%llu frame=%llu draw=%llu kind=%s count=%u start=%u base=%d vp=%.0fx%.0f OUT[%s] IN[%s] VS=%08X-%08X-%08X-%08X PS=%08X-%08X-%08X-%08X",
            static_cast<unsigned long long>(g_handoff42_events),
            static_cast<unsigned long long>(g_render_frame),
            static_cast<unsigned long long>(g_draw_counter),
            g_probe_draw_kind,
            g_probe_draw_count,
            g_probe_draw_start,
            g_probe_draw_base_vertex,
            vp_count ? vp.Width : 0.0f,
            vp_count ? vp.Height : 0.0f,
            out_roles[0] ? out_roles : "-",
            in_roles[0] ? in_roles : "-",
            vsh.a, vsh.b, vsh.c, vsh.d,
            psh.a, psh.b, psh.c, psh.d);
    }

    void before_draw(ID3D11DeviceContext *ctx)
    {
        ++g_draw_counter;
        update_short_probe_state();



        if (g_res_arch_capture_active)
            log_primary_render_target_write(ctx);




        if (short_probe_should_log())
        {
            sample_scene_geometry_constant_buffers(ctx);

            if (g_short_probe_events >= GEOMETRY_CB_EVENT_CAP)
            {
                g_short_probe_active = false;
                g_short_probe_completed = true;

                uint32_t tracked = 0;
                uint32_t complete = 0;

                for (const auto &track : g_geometry_cb_tracks)
                {
                    if (track.used)
                    {
                        ++tracked;
                        if (track.sample_count >= 6)
                            ++complete;
                    }
                }

                if (g_release_verbose_diagnostics)
                {
                    log_line(
                    "GEOMETRY_CB STOP event-cap frame=%llu events=%llu samples=%llu candidates=%llu trackedVS=%u complete6=%u",
                    static_cast<unsigned long long>(g_render_frame),
                    static_cast<unsigned long long>(g_short_probe_events),
                    static_cast<unsigned long long>(g_geometry_cb_samples),
                    static_cast<unsigned long long>(g_geometry_cb_candidates),
                    tracked,
                    complete);
                }
            }
        }




        poll_hotkey(ctx);

        const ShaderRole vs_role = current_vs_role();
        const ShaderRole ps_role = current_ps_role();

        if (ps_role == ShaderRole::ps_camera_motion)
        {
            probe_camera_motion_resources(ctx);
        }

        if (vs_role == ShaderRole::vs_rgbm_encode &&
            ps_role == ShaderRole::ps_rgbm_encode)
        {
            ++g_render_frame;

            probe_rgbm_scene_resource(ctx);

            ID3D11ShaderResourceView *scene_srv=nullptr;
            ctx->PSGetShaderResources(0,1,&scene_srv);
            ID3D11Texture2D *scene=nullptr;
            get_texture2d_from_srv(scene_srv,&scene);

            if(scene)
            {
                D3D11_TEXTURE2D_DESC sd{};
                scene->GetDesc(&sd);

                if(is_expected_scene(scene))
                {
                    set_tracked_texture(g_last_scene_tex,scene);

                    read_and_optionally_zero_jitter(ctx,sd.Width,sd.Height);

                    if(g_last_velocity_tex && g_last_depth_tex && g_dlaa_injection_enabled)
                    {
                        const bool published = asi_bridge_v100::publish(
                            ctx,
                            scene,
                            g_last_velocity_tex,
                            g_last_depth_tex,
                            g_current_jitter_x_pixels,
                            g_current_jitter_y_pixels,
                            g_render_frame,
                            true);

                        if (published)
                        {
                            asi_bridge_v100::inject_matching_output(
                                ctx, scene, g_render_frame, 50);
                        }
                    }
                }
            }

            if(scene)scene->Release();
            if(scene_srv)scene_srv->Release();

            if (g_release_keep_periodic_status && (g_render_frame <= 5 || (g_render_frame % 18000ull) == 0))
            {
                const bool all_found =
                    g_last_scene_tex != nullptr &&
                    g_last_velocity_tex != nullptr &&
                    g_last_depth_tex != nullptr;

                if (all_found && !g_release_ready_logged)
                {
                    g_release_ready_logged = true;
                    log_line(
                        "DLAA_READY scene=%p motion=%p depth=%p resolution=%ux%u",
                        g_last_scene_tex,
                        g_last_velocity_tex,
                        g_last_depth_tex,
                        3840u,
                        2160u);
                }

                if (g_release_verbose_diagnostics)
                {
                    log_line(
                    "RESOURCE_STATUS frame=%llu all_found=%d scene=%p velocity=%p depth=%p jitterPx=(%.3f,%.3f) releaseBranch=DLAA dlaa=%s toggles=%llu nativeSMAA=OFF smaaBypass=PERMANENT mvDirection=INVERTED_PERMANENT",
                    static_cast<unsigned long long>(g_render_frame),
                    all_found ? 1 : 0,
                    g_last_scene_tex,
                    g_last_velocity_tex,
                    g_last_depth_tex,
                    g_current_jitter_x_pixels,
                    g_current_jitter_y_pixels,
                    g_dlaa_injection_enabled ? "ON" : "OFF",
                    static_cast<unsigned long long>(g_dlaa_toggle_count));
                }

            }
        }
    }

    void STDMETHODCALLTYPE hook_om_set_render_targets(
        ID3D11DeviceContext *ctx,
        UINT num_views,
        ID3D11RenderTargetView *const *views,
        ID3D11DepthStencilView *dsv)
    {
        g_orig_om_set_render_targets(ctx, num_views, views, dsv);
    }

    void STDMETHODCALLTYPE hook_ps_set_shader_resources(
        ID3D11DeviceContext *ctx,
        UINT start_slot,
        UINT num_views,
        ID3D11ShaderResourceView *const *views)
    {
        g_orig_ps_set_shader_resources(ctx, start_slot, num_views, views);
    }

    void STDMETHODCALLTYPE hook_copy_resource(
        ID3D11DeviceContext *ctx,
        ID3D11Resource *dst,
        ID3D11Resource *src)
    {
        g_orig_copy_resource(ctx, dst, src);
    }


    void STDMETHODCALLTYPE hook_rs_set_viewports(
        ID3D11DeviceContext *ctx,
        UINT count,
        const D3D11_VIEWPORT *viewports)
    {
        g_orig_rs_set_viewports(ctx, count, viewports);
    }

    void STDMETHODCALLTYPE hook_draw(ID3D11DeviceContext *ctx, UINT vertex_count, UINT start_vertex)
    {
        g_probe_draw_kind = "Draw";
        g_probe_draw_count = vertex_count;
        g_probe_draw_start = start_vertex;
        g_probe_draw_base_vertex = 0;

        before_draw(ctx);

        GeometryJitterPatchState jitter_state{};
        map_patch_geometry_jitter(ctx, jitter_state);

        if (try_native_smaa_passthrough(ctx))
        {
            restore_geometry_jitter(ctx, jitter_state);
            return;
        }

        g_orig_draw(ctx, vertex_count, start_vertex);
        restore_geometry_jitter(ctx, jitter_state);
    }

    void STDMETHODCALLTYPE hook_draw_indexed(
        ID3D11DeviceContext *ctx, UINT index_count, UINT start_index, INT base_vertex)
    {
        g_probe_draw_kind = "DrawIndexed";
        g_probe_draw_count = index_count;
        g_probe_draw_start = start_index;
        g_probe_draw_base_vertex = base_vertex;

        before_draw(ctx);

        GeometryJitterPatchState jitter_state{};
        map_patch_geometry_jitter(ctx, jitter_state);

        if (try_native_smaa_passthrough(ctx))
        {
            restore_geometry_jitter(ctx, jitter_state);
            return;
        }

        g_orig_draw_indexed(ctx, index_count, start_index, base_vertex);
        restore_geometry_jitter(ctx, jitter_state);
    }

    bool install_device_hooks(ID3D11Device *device, ID3D11DeviceContext *ctx)
    {
        if (device == nullptr || ctx == nullptr)
            return false;

        if (!patch_vtable(device, 5, reinterpret_cast<void *>(&hook_create_texture2d),
                          reinterpret_cast<void **>(&g_orig_create_texture2d)))
            return false;

        if (!patch_vtable(device, 9, reinterpret_cast<void *>(&hook_create_rtv),
                          reinterpret_cast<void **>(&g_orig_create_rtv)))
            return false;

        if (!patch_vtable(device, 10, reinterpret_cast<void *>(&hook_create_dsv),
                          reinterpret_cast<void **>(&g_orig_create_dsv)))
            return false;

        if (!patch_vtable(device, 12, reinterpret_cast<void *>(&hook_create_vs),
                          reinterpret_cast<void **>(&g_orig_create_vs)))
            return false;

        if (!patch_vtable(device, 15, reinterpret_cast<void *>(&hook_create_ps),
                          reinterpret_cast<void **>(&g_orig_create_ps)))
            return false;

        if (!patch_vtable(ctx, 9, reinterpret_cast<void *>(&hook_ps_set_shader),
                          reinterpret_cast<void **>(&g_orig_ps_set_shader)))
            return false;

        if (!patch_vtable(ctx, 11, reinterpret_cast<void *>(&hook_vs_set_shader),
                          reinterpret_cast<void **>(&g_orig_vs_set_shader)))
            return false;

        if (!patch_vtable(ctx, 12, reinterpret_cast<void *>(&hook_draw_indexed),
                          reinterpret_cast<void **>(&g_orig_draw_indexed)))
            return false;

        if (!patch_vtable(ctx, 13, reinterpret_cast<void *>(&hook_draw),
                          reinterpret_cast<void **>(&g_orig_draw)))
            return false;

        if (!patch_vtable(ctx, 8, reinterpret_cast<void *>(&hook_ps_set_shader_resources),
                          reinterpret_cast<void **>(&g_orig_ps_set_shader_resources)))
            return false;

        if (!patch_vtable(ctx, 33, reinterpret_cast<void *>(&hook_om_set_render_targets),
                          reinterpret_cast<void **>(&g_orig_om_set_render_targets)))
            return false;

        if (!patch_vtable(ctx, 44, reinterpret_cast<void *>(&hook_rs_set_viewports),
                          reinterpret_cast<void **>(&g_orig_rs_set_viewports)))
            return false;

        if (!patch_vtable(ctx, 47, reinterpret_cast<void *>(&hook_copy_resource),
                          reinterpret_cast<void **>(&g_orig_copy_resource)))
            return false;

        log_line("Direct D3D11 device/context vtable hooks installed.");
        log_line(
            "DLAA_RELEASE baseline: DLAA=%s SMAA=OFF jitter=8 MV=INVERTED",
            g_dlaa_injection_enabled ? "ON" : "OFF");
        return true;
    }

    using PFN_D3D11CreateDevice = HRESULT (WINAPI *)(
        IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL *, UINT, UINT,
        ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

    using PFN_D3D11CreateDeviceAndSwapChain = HRESULT (WINAPI *)(
        IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL *, UINT, UINT,
        const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
        ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

    PFN_D3D11CreateDevice g_orig_create_device = nullptr;
    PFN_D3D11CreateDeviceAndSwapChain g_orig_create_device_sc = nullptr;

    bool g_hooks_installed = false;

    void capture_created_device(ID3D11Device *device, ID3D11DeviceContext *ctx)
    {
        if (g_hooks_installed || device == nullptr || ctx == nullptr)
            return;

        if (install_device_hooks(device, ctx))
        {
            g_hooks_installed = true;
            log_line("Captured Alien Isolation D3D11 device=%p context=%p", device, ctx);
        }
        else
        {
            log_line("ERROR: failed to install D3D11 vtable hooks.");
        }
    }

    HRESULT WINAPI hook_d3d11_create_device(
        IDXGIAdapter *adapter,
        D3D_DRIVER_TYPE driver_type,
        HMODULE software,
        UINT flags,
        const D3D_FEATURE_LEVEL *levels,
        UINT level_count,
        UINT sdk_version,
        ID3D11Device **device,
        D3D_FEATURE_LEVEL *feature_level,
        ID3D11DeviceContext **context)
    {
        HRESULT hr = g_orig_create_device(
            adapter, driver_type, software, flags, levels, level_count, sdk_version,
            device, feature_level, context);

        if (SUCCEEDED(hr) && device != nullptr && context != nullptr)
            capture_created_device(*device, *context);

        return hr;
    }

    HRESULT WINAPI hook_d3d11_create_device_sc(
        IDXGIAdapter *adapter,
        D3D_DRIVER_TYPE driver_type,
        HMODULE software,
        UINT flags,
        const D3D_FEATURE_LEVEL *levels,
        UINT level_count,
        UINT sdk_version,
        const DXGI_SWAP_CHAIN_DESC *desc,
        IDXGISwapChain **swap_chain,
        ID3D11Device **device,
        D3D_FEATURE_LEVEL *feature_level,
        ID3D11DeviceContext **context)
    {
        HRESULT hr = g_orig_create_device_sc(
            adapter, driver_type, software, flags, levels, level_count, sdk_version,
            desc, swap_chain, device, feature_level, context);

        if (SUCCEEDED(hr) && device != nullptr && context != nullptr)
            capture_created_device(*device, *context);

        if (SUCCEEDED(hr) && swap_chain != nullptr && *swap_chain != nullptr)
            hook_swap_chain(*swap_chain);

        return hr;
    }

    bool patch_iat_entry(
        HMODULE module,
        const char *import_module,
        const char *function_name,
        void *replacement,
        void **original)
    {
        if (module == nullptr)
            return false;

        auto *base = reinterpret_cast<uint8_t *>(module);
        auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;

        const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (dir.VirtualAddress == 0)
            return false;

        auto *imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);

        for (; imp->Name != 0; ++imp)
        {
            const char *dll_name = reinterpret_cast<const char *>(base + imp->Name);
            if (_stricmp(dll_name, import_module) != 0)
                continue;

            auto *first = reinterpret_cast<IMAGE_THUNK_DATA *>(base + imp->FirstThunk);
            auto *orig_thunk = imp->OriginalFirstThunk != 0
                ? reinterpret_cast<IMAGE_THUNK_DATA *>(base + imp->OriginalFirstThunk)
                : first;

            for (; orig_thunk->u1.AddressOfData != 0; ++orig_thunk, ++first)
            {
                if (IMAGE_SNAP_BY_ORDINAL(orig_thunk->u1.Ordinal))
                    continue;

                auto *name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(
                    base + orig_thunk->u1.AddressOfData);

                if (std::strcmp(reinterpret_cast<const char *>(name->Name), function_name) != 0)
                    continue;

                DWORD old_protect = 0;
                if (!VirtualProtect(&first->u1.Function, sizeof(void *),
                                    PAGE_EXECUTE_READWRITE, &old_protect))
                    return false;

                *original = reinterpret_cast<void *>(
                    static_cast<uintptr_t>(first->u1.Function));

#ifdef _WIN64
                first->u1.Function = reinterpret_cast<ULONGLONG>(replacement);
#else
                first->u1.Function = reinterpret_cast<DWORD>(replacement);
#endif

                DWORD dummy = 0;
                VirtualProtect(&first->u1.Function, sizeof(void *), old_protect, &dummy);
                FlushInstructionCache(
                    GetCurrentProcess(), &first->u1.Function, sizeof(void *));
                return true;
            }
        }

        return false;
    }

    DWORD WINAPI initialize(LPVOID)
    {
        init_log_path();
        reset_log();
        load_config();

        log_line(
            "CONFIG DLAA=%s menuHotkey=%ls toggleHotkey=%ls CreatedBy=Gametism",
            g_dlaa_injection_enabled ? "ON" : "OFF",
            hotkey_name(g_menu_hotkey).c_str(),
            hotkey_name(g_toggle_dlaa_hotkey).c_str());

        log_line("INPUT hotkeys=CONFIGURABLE debounce=ON");
        log_line("MENU mouse=RAW_INPUT keyboardPassthrough=1 controllerPassthrough=1");

        HMODULE exe = GetModuleHandleW(nullptr);
        bool any = false;

        any |= patch_iat_entry(
            exe, "d3d11.dll", "D3D11CreateDeviceAndSwapChain",
            reinterpret_cast<void *>(&hook_d3d11_create_device_sc),
            reinterpret_cast<void **>(&g_orig_create_device_sc));

        any |= patch_iat_entry(
            exe, "d3d11.dll", "D3D11CreateDevice",
            reinterpret_cast<void *>(&hook_d3d11_create_device),
            reinterpret_cast<void **>(&g_orig_create_device));

        if (any)
            log_line("AI.exe D3D11 creation IAT hook installed.");
        else
            log_line("ERROR: AI.exe does not import a hookable D3D11CreateDevice* entry.");

        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = hModule;
        DisableThreadLibraryCalls(hModule);
        asi_bridge_v100::init(hModule);
        asi_bridge_v100::set_dlss_mode(g_dlss_mode);
        asi_bridge_v100::set_auto_exposure(g_ngx_auto_exposure);

        HANDLE thread = CreateThread(nullptr, 0, initialize, nullptr, 0, nullptr);
        if (thread != nullptr)
            CloseHandle(thread);
    }
    else if(reason == DLL_PROCESS_DETACH)
    {
        set_tracked_texture(g_last_scene_tex,nullptr);
        set_tracked_texture(g_last_velocity_tex,nullptr);
        set_tracked_texture(g_last_depth_tex,nullptr);

        if (g_probe_scene_color) { g_probe_scene_color->Release(); g_probe_scene_color=nullptr; }
        if (g_probe_velocity) { g_probe_velocity->Release(); g_probe_velocity=nullptr; }
        if (g_probe_depth) { g_probe_depth->Release(); g_probe_depth=nullptr; }

        set_probe_target_texture(g_target_half_color, nullptr);
        set_probe_target_texture(g_target_half_depth, nullptr);
        set_probe_target_texture(g_pending_half_color, nullptr);

        if (g_game_window &&
            g_original_wndproc &&
            g_window_input_hooked)
        {
            SetWindowLongPtrW(
                g_game_window,
                GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(g_original_wndproc));

            g_window_input_hooked = false;
            g_original_wndproc = nullptr;
            g_game_window = nullptr;
        }

        release_overlay_resources();
        asi_bridge_v100::shutdown();
    }

    return TRUE;
}
