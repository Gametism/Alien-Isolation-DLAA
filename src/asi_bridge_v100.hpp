#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace asi_bridge_v100
{
    constexpr uint32_t MAGIC = 0x41314942u;
    constexpr uint32_t VERSION = 100u;
    constexpr wchar_t MAPNAME[] = L"Local\\AlienIsolation_DLAA_Bridge_v100";

#pragma pack(push,1)
    struct State
    {
        uint32_t magic,version,producer_pid,helper_pid;
        int32_t luid_hi;
        uint32_t luid_lo;

        uint64_t color_handle,motion_handle,depth_handle;
        uint32_t cw,ch,cf,mw,mh,mf,dw,dh,df;

        volatile LONG epoch,produced,consumed,status;
        float jitter_x,jitter_y;
        uint64_t source_frame;

        float mvx,mvy;
        uint64_t checksum,dlss_checksum;
        volatile LONG dlss_eval_count,dlss_eval_success,dlss_last_result;

        uint64_t output_handle;
        uint32_t output_width,output_height,output_format;
        volatile LONG output_epoch,output_frames;
        uint64_t output_source_frame;
        uint32_t last_error;
        volatile LONG reset_serial;
        volatile LONG requested_mode;
        volatile LONG mode_serial;
        volatile LONG active_mode;
        uint32_t active_render_width;
        uint32_t active_render_height;
        volatile LONG auto_exposure;
    };
#pragma pack(pop)

    inline HANDLE mapping = nullptr;
    inline State *st = nullptr;
    inline HMODULE module = nullptr;
    inline PROCESS_INFORMATION helper_pi{};
    inline bool launch_attempted = false;

    inline ID3D11Texture2D *shared_color = nullptr;
    inline ID3D11Texture2D *shared_motion = nullptr;
    inline ID3D11Texture2D *shared_depth = nullptr;

    inline IDXGIKeyedMutex *color_mutex = nullptr;
    inline IDXGIKeyedMutex *motion_mutex = nullptr;
    inline IDXGIKeyedMutex *depth_mutex = nullptr;

    inline HANDLE color_handle = nullptr;
    inline HANDLE motion_handle = nullptr;
    inline HANDLE depth_handle = nullptr;

    inline D3D11_TEXTURE2D_DESC color_desc{};
    inline D3D11_TEXTURE2D_DESC motion_desc{};
    inline D3D11_TEXTURE2D_DESC depth_desc{};

    inline uint64_t published = 0;

    inline ID3D11Texture2D *shared_output = nullptr;
    inline IDXGIKeyedMutex *output_mutex = nullptr;
    inline ID3D11ShaderResourceView *output_srv = nullptr;
    inline LONG opened_output_epoch = -1;

    inline ID3D11VertexShader *inject_vs = nullptr;
    inline ID3D11PixelShader *inject_ps = nullptr;
    inline ID3D11SamplerState *inject_sampler = nullptr;

    inline ID3D11Texture2D *target_scene = nullptr;
    inline ID3D11RenderTargetView *target_rtv = nullptr;

    inline uint64_t injected = 0;
    inline uint64_t sync_misses = 0;
    inline uint64_t stale_outputs = 0;
    inline volatile LONG history_reset_serial = 0;
    inline LONG history_reset_sent = 0;


    inline void logline(const char *fmt,...)
    {
        if (!module)
            return;

        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module,path,MAX_PATH);
        wchar_t *slash=wcsrchr(path,L'\\');
        if(slash) *(slash+1)=L'\0';

        wchar_t logpath[MAX_PATH]{};
        _snwprintf_s(logpath,_TRUNCATE,L"%sAlienIsolationDLAA.log",path);

        FILE *f=nullptr;
        if(_wfopen_s(&f,logpath,L"a")!=0 || !f)
            return;

        va_list a;
        va_start(a,fmt);
        vfprintf(f,fmt,a);
        va_end(a);
        fputc('\n',f);
        fclose(f);
    }

    inline void request_history_reset()
    {
        InterlockedIncrement(&history_reset_serial);
        logline("BRIDGE history reset requested serial=%ld", history_reset_serial);
    }


    inline volatile LONG requested_mode = 0;
    inline volatile LONG requested_mode_serial = 0;
    inline volatile LONG requested_auto_exposure = 1;

    inline void set_dlss_mode(uint32_t mode)
    {
        if (mode > 3u)
            mode = 0u;

        InterlockedExchange(&requested_mode, static_cast<LONG>(mode));
        const LONG serial = InterlockedIncrement(&requested_mode_serial);

        if (st)
        {
            st->requested_mode = static_cast<LONG>(mode);
            st->mode_serial = serial;
        }

        request_history_reset();
        logline("BRIDGE DLSS mode request mode=%u serial=%ld", mode, serial);
    }

    inline void set_auto_exposure(bool enabled)
    {
        InterlockedExchange(&requested_auto_exposure, enabled ? 1 : 0);

        // Feature-create flags cannot be changed in-place. Bump the same
        // recreation serial used by DLSS mode changes so the helper rebuilds
        // the feature with the requested exposure mode.
        const LONG serial = InterlockedIncrement(&requested_mode_serial);

        if (st)
        {
            st->auto_exposure = enabled ? 1 : 0;
            st->mode_serial = serial;
        }

        request_history_reset();
        logline(
            "BRIDGE NGX Auto Exposure request enabled=%d serial=%ld",
            enabled ? 1 : 0,
            serial);
    }

    inline void release_output()
    {
        if(target_rtv){target_rtv->Release();target_rtv=nullptr;}
        if(target_scene){target_scene->Release();target_scene=nullptr;}
        if(output_srv){output_srv->Release();output_srv=nullptr;}
        if(output_mutex){output_mutex->Release();output_mutex=nullptr;}
        if(shared_output){shared_output->Release();shared_output=nullptr;}
        opened_output_epoch=-1;
    }

    inline void release_inject_shaders()
    {
        if(inject_sampler){inject_sampler->Release();inject_sampler=nullptr;}
        if(inject_ps){inject_ps->Release();inject_ps=nullptr;}
        if(inject_vs){inject_vs->Release();inject_vs=nullptr;}
    }

    inline bool ensure_inject_shaders(ID3D11Device *device)
    {
        if(inject_vs&&inject_ps&&inject_sampler)
            return true;

        static const char *vs_src =
            "struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
            "O main(uint id:SV_VertexID){O o;float2 p=float2((id<<1)&2,id&2);"
            "o.uv=p;o.p=float4(p*float2(2,-2)+float2(-1,1),0,1);return o;}";

        static const char *ps_src =
            "Texture2D<float4>T:register(t0);SamplerState S:register(s0);"
            "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
            "return float4(T.SampleLevel(S,uv,0).rgb,1.0);}";

        ID3DBlob *blob=nullptr,*err=nullptr;
        HRESULT hr=D3DCompile(vs_src,strlen(vs_src),nullptr,nullptr,nullptr,
                              "main","vs_5_0",0,0,&blob,&err);
        if(FAILED(hr)){
            logline("INJECT VS compile failed hr=0x%08X %s",
                    (uint32_t)hr,err?(char*)err->GetBufferPointer():"");
            if(err)err->Release();
            return false;
        }
        hr=device->CreateVertexShader(blob->GetBufferPointer(),blob->GetBufferSize(),
                                      nullptr,&inject_vs);
        blob->Release(); if(err){err->Release();err=nullptr;}
        if(FAILED(hr))return false;

        hr=D3DCompile(ps_src,strlen(ps_src),nullptr,nullptr,nullptr,
                      "main","ps_5_0",0,0,&blob,&err);
        if(FAILED(hr)){
            logline("INJECT PS compile failed hr=0x%08X %s",
                    (uint32_t)hr,err?(char*)err->GetBufferPointer():"");
            if(err)err->Release();
            return false;
        }
        hr=device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),
                                     nullptr,&inject_ps);
        blob->Release(); if(err)err->Release();
        if(FAILED(hr))return false;

        D3D11_SAMPLER_DESC sd{};
        sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD=D3D11_FLOAT32_MAX;
        hr=device->CreateSamplerState(&sd,&inject_sampler);
        if(SUCCEEDED(hr))logline("INJECT shaders ready");
        return SUCCEEDED(hr);
    }

    inline bool stretch_copy_texture(
        ID3D11DeviceContext *ctx,
        ID3D11Texture2D *src,
        ID3D11Texture2D *dst)
    {
        if(!ctx||!src||!dst)
            return false;

        ID3D11Device *device=nullptr;
        ctx->GetDevice(&device);
        if(!device)
            return false;

        if(!ensure_inject_shaders(device))
        {
            device->Release();
            return false;
        }

        D3D11_TEXTURE2D_DESC sd{},dd{};
        src->GetDesc(&sd);
        dst->GetDesc(&dd);

        ID3D11ShaderResourceView *src_srv=nullptr;
        ID3D11RenderTargetView *dst_rtv=nullptr;
        HRESULT hr=device->CreateShaderResourceView(src,nullptr,&src_srv);
        if(SUCCEEDED(hr))
            hr=device->CreateRenderTargetView(dst,nullptr,&dst_rtv);

        if(FAILED(hr)||!src_srv||!dst_rtv)
        {
            if(src_srv)src_srv->Release();
            if(dst_rtv)dst_rtv->Release();
            device->Release();
            logline("STRETCH_COPY view create failed hr=0x%08X",(uint32_t)hr);
            return false;
        }

        ID3D11VertexShader *old_vs=nullptr;
        ID3D11PixelShader *old_ps=nullptr;
        ID3D11InputLayout *old_layout=nullptr;
        D3D11_PRIMITIVE_TOPOLOGY old_topology=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ID3D11RenderTargetView *old_rtv=nullptr;
        ID3D11DepthStencilView *old_dsv=nullptr;
        ID3D11ShaderResourceView *old_srv=nullptr;
        ID3D11SamplerState *old_sampler=nullptr;
        UINT old_vp_count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        D3D11_VIEWPORT old_vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};

        ctx->VSGetShader(&old_vs,nullptr,nullptr);
        ctx->PSGetShader(&old_ps,nullptr,nullptr);
        ctx->IAGetInputLayout(&old_layout);
        ctx->IAGetPrimitiveTopology(&old_topology);
        ctx->OMGetRenderTargets(1,&old_rtv,&old_dsv);
        ctx->PSGetShaderResources(0,1,&old_srv);
        ctx->PSGetSamplers(0,1,&old_sampler);
        ctx->RSGetViewports(&old_vp_count,old_vps);

        D3D11_VIEWPORT vp{};
        vp.Width=(float)dd.Width; vp.Height=(float)dd.Height;
        vp.MinDepth=0.0f; vp.MaxDepth=1.0f;

        ctx->OMSetRenderTargets(1,&dst_rtv,nullptr);
        ctx->RSSetViewports(1,&vp);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(inject_vs,nullptr,0);
        ctx->PSSetShader(inject_ps,nullptr,0);
        ctx->PSSetShaderResources(0,1,&src_srv);
        ctx->PSSetSamplers(0,1,&inject_sampler);
        ctx->Draw(3,0);

        ID3D11ShaderResourceView *null_srv=nullptr;
        ctx->PSSetShaderResources(0,1,&null_srv);

        ctx->VSSetShader(old_vs,nullptr,0);
        ctx->PSSetShader(old_ps,nullptr,0);
        ctx->IASetInputLayout(old_layout);
        ctx->IASetPrimitiveTopology(old_topology);
        if(old_vp_count>0)ctx->RSSetViewports(old_vp_count,old_vps);
        ctx->OMSetRenderTargets(1,&old_rtv,old_dsv);
        ctx->PSSetShaderResources(0,1,&old_srv);
        ctx->PSSetSamplers(0,1,&old_sampler);

        static uint64_t stretch_count=0;
        ++stretch_count;
        if(stretch_count<=8||(stretch_count%600ull)==0)
            logline("STRETCH_COPY count=%llu src=%ux%u fmt=%u dst=%ux%u fmt=%u",
                (unsigned long long)stretch_count,
                sd.Width,sd.Height,(uint32_t)sd.Format,
                dd.Width,dd.Height,(uint32_t)dd.Format);

        if(old_sampler)old_sampler->Release();
        if(old_srv)old_srv->Release();
        if(old_dsv)old_dsv->Release();
        if(old_rtv)old_rtv->Release();
        if(old_layout)old_layout->Release();
        if(old_ps)old_ps->Release();
        if(old_vs)old_vs->Release();
        dst_rtv->Release();
        src_srv->Release();
        device->Release();
        return true;
    }

    inline bool open_output_if_ready(ID3D11Device *device)
    {
        if(!st||!device||st->output_epoch<=0||st->output_handle==0)
            return false;

        const LONG epoch=st->output_epoch;
        if(shared_output&&opened_output_epoch==epoch)
            return true;

        if(output_srv){output_srv->Release();output_srv=nullptr;}
        if(output_mutex){output_mutex->Release();output_mutex=nullptr;}
        if(shared_output){shared_output->Release();shared_output=nullptr;}

        HRESULT hr=device->OpenSharedResource(
            (HANDLE)(uintptr_t)st->output_handle,
            __uuidof(ID3D11Texture2D),(void**)&shared_output);
        if(FAILED(hr)||!shared_output){
            logline("OUTPUT OpenSharedResource failed epoch=%ld hr=0x%08X",
                    epoch,(uint32_t)hr);
            return false;
        }

        hr=shared_output->QueryInterface(__uuidof(IDXGIKeyedMutex),(void**)&output_mutex);
        if(SUCCEEDED(hr))
            hr=device->CreateShaderResourceView(shared_output,nullptr,&output_srv);

        if(FAILED(hr)||!output_mutex||!output_srv){
            logline("OUTPUT prepare failed hr=0x%08X",(uint32_t)hr);
            if(output_srv){output_srv->Release();output_srv=nullptr;}
            if(output_mutex){output_mutex->Release();output_mutex=nullptr;}
            if(shared_output){shared_output->Release();shared_output=nullptr;}
            return false;
        }

        opened_output_epoch=epoch;
        D3D11_TEXTURE2D_DESC d{}; shared_output->GetDesc(&d);
        logline("OUTPUT opened epoch=%ld %ux%u fmt=%u handle=0x%llX",
                epoch,d.Width,d.Height,(uint32_t)d.Format,
                (unsigned long long)st->output_handle);
        return true;
    }

    inline bool ensure_target_rtv(ID3D11Device *device,ID3D11Texture2D *scene)
    {
        if(!device||!scene)return false;
        if(target_scene==scene&&target_rtv)return true;

        if(target_rtv){target_rtv->Release();target_rtv=nullptr;}
        if(target_scene){target_scene->Release();target_scene=nullptr;}

        D3D11_TEXTURE2D_DESC d{}; scene->GetDesc(&d);
        if((d.BindFlags&D3D11_BIND_RENDER_TARGET)==0){
            logline("INJECT target lacks RTV bind flags=0x%X",d.BindFlags);
            return false;
        }

        HRESULT hr=device->CreateRenderTargetView(scene,nullptr,&target_rtv);
        if(FAILED(hr)||!target_rtv){
            logline("INJECT target RTV create failed hr=0x%08X",(uint32_t)hr);
            return false;
        }

        target_scene=scene; target_scene->AddRef();
        return true;
    }

    inline void release_shared()
    {
        if(color_mutex){color_mutex->Release();color_mutex=nullptr;}
        if(motion_mutex){motion_mutex->Release();motion_mutex=nullptr;}
        if(depth_mutex){depth_mutex->Release();depth_mutex=nullptr;}

        if(shared_color){shared_color->Release();shared_color=nullptr;}
        if(shared_motion){shared_motion->Release();shared_motion=nullptr;}
        if(shared_depth){shared_depth->Release();shared_depth=nullptr;}

        color_handle=motion_handle=depth_handle=nullptr;
        std::memset(&color_desc,0,sizeof(color_desc));
        std::memset(&motion_desc,0,sizeof(motion_desc));
        std::memset(&depth_desc,0,sizeof(depth_desc));
    }

    inline bool make_shared(
        ID3D11Device *device,
        const D3D11_TEXTURE2D_DESC &src,
        ID3D11Texture2D **texture,
        IDXGIKeyedMutex **mutex,
        HANDLE *handle)
    {
        D3D11_TEXTURE2D_DESC d=src;
        d.Usage=D3D11_USAGE_DEFAULT;
        d.CPUAccessFlags=0;
        d.MipLevels=1;
        d.ArraySize=1;
        d.SampleDesc.Count=1;
        d.SampleDesc.Quality=0;
        d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

        HRESULT hr=device->CreateTexture2D(&d,nullptr,texture);
        if(FAILED(hr)||!*texture)
            return false;

        IDXGIResource *dxgi=nullptr;
        hr=(*texture)->QueryInterface(__uuidof(IDXGIKeyedMutex),(void**)mutex);
        if(SUCCEEDED(hr))
            hr=(*texture)->QueryInterface(__uuidof(IDXGIResource),(void**)&dxgi);
        if(SUCCEEDED(hr)&&dxgi)
            hr=dxgi->GetSharedHandle(handle);
        if(dxgi)dxgi->Release();

        return SUCCEEDED(hr)&&*mutex&&*handle;
    }

    inline bool ensure_mapping(ID3D11Device *device)
    {
        if(st)
            return true;

        mapping=CreateFileMappingW(
            INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,
            0,sizeof(State),MAPNAME);

        if(!mapping)
            return false;

        st=(State*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(State));
        if(!st)
            return false;

        std::memset(st,0,sizeof(State));
        st->magic=MAGIC;
        st->version=VERSION;
        st->producer_pid=GetCurrentProcessId();
        st->requested_mode=requested_mode;
        st->mode_serial=requested_mode_serial;
        st->active_mode=0;
        st->active_render_width=0;
        st->active_render_height=0;
        st->auto_exposure=requested_auto_exposure;

        IDXGIDevice *dxgi_device=nullptr;
        IDXGIAdapter *adapter=nullptr;
        if(SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),(void**)&dxgi_device)) &&
           SUCCEEDED(dxgi_device->GetAdapter(&adapter)))
        {
            DXGI_ADAPTER_DESC desc{};
            if(SUCCEEDED(adapter->GetDesc(&desc)))
            {
                st->luid_hi=desc.AdapterLuid.HighPart;
                st->luid_lo=desc.AdapterLuid.LowPart;
            }
        }
        if(adapter)adapter->Release();
        if(dxgi_device)dxgi_device->Release();

        logline("BRIDGE mapping ready name=%ls",MAPNAME);
        return true;
    }

    inline bool launch_helper()
    {
        if(launch_attempted)
            return helper_pi.hProcess!=nullptr;

        launch_attempted=true;

        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module,path,MAX_PATH);
        wchar_t *slash=wcsrchr(path,L'\\');
        if(!slash)
            return false;
        *(slash+1)=L'\0';

        wchar_t helper[MAX_PATH]{};
        _snwprintf_s(
            helper,_TRUNCATE,
            L"%sAlienIsolationDLAA-Bridge64.exe",path);

        wchar_t cmd[MAX_PATH*2]{};
        _snwprintf_s(cmd,_TRUNCATE,L"\"%s\"",helper);

        STARTUPINFOW si{};
        si.cb=sizeof(si);

        if(!CreateProcessW(
            helper,cmd,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
            nullptr,path,&si,&helper_pi))
        {
            logline("BRIDGE CreateProcess failed error=%lu path=%ls",
                GetLastError(),helper);
            return false;
        }

        if(st)
            st->helper_pid=helper_pi.dwProcessId;

        logline("BRIDGE launched helper pid=%lu",helper_pi.dwProcessId);
        return true;
    }

    inline bool same_desc(
        const D3D11_TEXTURE2D_DESC &a,
        const D3D11_TEXTURE2D_DESC &b)
    {
        return a.Width==b.Width &&
               a.Height==b.Height &&
               a.Format==b.Format;
    }

    inline void init(HMODULE m)
    {
        module=m;
    }

    inline void shutdown()
    {
        release_inject_shaders();
        release_output();
        release_shared();

        if(st)
        {
            UnmapViewOfFile(st);
            st=nullptr;
        }

        if(mapping)
        {
            CloseHandle(mapping);
            mapping=nullptr;
        }

        if(helper_pi.hThread)
        {
            CloseHandle(helper_pi.hThread);
            helper_pi.hThread=nullptr;
        }

        if(helper_pi.hProcess)
        {
            CloseHandle(helper_pi.hProcess);
            helper_pi.hProcess=nullptr;
        }
    }

    inline bool publish(
        ID3D11DeviceContext *ctx,
        ID3D11Texture2D *scene,
        ID3D11Texture2D *motion,
        ID3D11Texture2D *depth,
        float jitter_x,
        float jitter_y,
        uint64_t source_frame,
        bool synchronized)
    {
        if(!ctx||!scene||!motion||!depth)
            return false;

        D3D11_TEXTURE2D_DESC c{},m{},d{};
        scene->GetDesc(&c); motion->GetDesc(&m); depth->GetDesc(&d);

        ID3D11Device *device=nullptr;
        ctx->GetDevice(&device);
        if(!device)return false;

        if(!ensure_mapping(device)){
            device->Release();
            return false;
        }

        const bool recreate=
            !shared_color||!shared_motion||!shared_depth||
            !same_desc(color_desc,c)||!same_desc(motion_desc,m)||!same_desc(depth_desc,d);

        if(recreate)
        {
            release_output();
            release_shared();

            if(!make_shared(device,c,&shared_color,&color_mutex,&color_handle)||
               !make_shared(device,m,&shared_motion,&motion_mutex,&motion_handle)||
               !make_shared(device,d,&shared_depth,&depth_mutex,&depth_handle))
            {
                logline("BRIDGE shared-resource creation failed");
                release_shared();
                device->Release();
                return false;
            }

            color_desc=c; motion_desc=m; depth_desc=d;
            st->color_handle=(uint64_t)(uintptr_t)color_handle;
            st->motion_handle=(uint64_t)(uintptr_t)motion_handle;
            st->depth_handle=(uint64_t)(uintptr_t)depth_handle;
            st->cw=c.Width;st->ch=c.Height;st->cf=(uint32_t)c.Format;
            st->mw=m.Width;st->mh=m.Height;st->mf=(uint32_t)m.Format;
            st->dw=d.Width;st->dh=d.Height;st->df=(uint32_t)d.Format;

            MemoryBarrier();
            LONG epoch=InterlockedIncrement(&st->epoch);
            logline(
                "BRIDGE resources epoch=%ld color=%ux%u fmt=%u motion=%ux%u fmt=%u depth=%ux%u fmt=%u",
                epoch,c.Width,c.Height,(uint32_t)c.Format,
                m.Width,m.Height,(uint32_t)m.Format,d.Width,d.Height,(uint32_t)d.Format);
            launch_helper();
        }

        const DWORD wait=synchronized?50u:0u;
        bool lc=SUCCEEDED(color_mutex->AcquireSync(0,wait));
        bool lm=false,ld=false;
        if(lc)lm=SUCCEEDED(motion_mutex->AcquireSync(0,wait));
        if(lc&&lm)ld=SUCCEEDED(depth_mutex->AcquireSync(0,wait));

        if(!(lc&&lm&&ld))
        {
            if(ld)depth_mutex->ReleaseSync(0);
            if(lm)motion_mutex->ReleaseSync(0);
            if(lc)color_mutex->ReleaseSync(0);
            device->Release();
            return false;
        }

        ctx->CopyResource(shared_color,scene);
        ctx->CopyResource(shared_motion,motion);
        ctx->CopyResource(shared_depth,depth);

        st->jitter_x=jitter_x;st->jitter_y=jitter_y;st->source_frame=source_frame;
        st->mvx = -1.0f;
        st->mvy = -1.0f;

        const LONG serial=history_reset_serial;
        if(serial!=history_reset_sent)
        {
            st->reset_serial=serial;
            history_reset_sent=serial;
        }

        st->requested_mode=requested_mode;
        st->mode_serial=requested_mode_serial;
        st->auto_exposure=requested_auto_exposure;

        MemoryBarrier();

        depth_mutex->ReleaseSync(1);
        motion_mutex->ReleaseSync(1);
        color_mutex->ReleaseSync(1);

        ++published;
        InterlockedIncrement(&st->produced);

        if(published==1)
            logline(
                "BRIDGE published=%llu consumed=%ld status=%ld frame=%llu jitter=(%.3f,%.3f) mvDirection=INVERTED_PERMANENT dlss=%ld/%ld result=0x%08X outputFrames=%ld sync=%d",
                (unsigned long long)published,st->consumed,st->status,
                (unsigned long long)source_frame,jitter_x,jitter_y,
                st->dlss_eval_success,st->dlss_eval_count,
                (uint32_t)st->dlss_last_result,st->output_frames,synchronized?1:0);

        device->Release();
        return true;
    }
    inline bool inject_matching_output(
        ID3D11DeviceContext *ctx,
        ID3D11Texture2D *scene,
        uint64_t source_frame,
        DWORD timeout_ms)
    {
        if(!ctx||!scene||!st)return false;

        ID3D11Device *device=nullptr;
        ctx->GetDevice(&device);
        if(!device)return false;

        if(!open_output_if_ready(device)||
           !ensure_inject_shaders(device)||
           !ensure_target_rtv(device,scene))
        {
            device->Release();
            return false;
        }

        const ULONGLONG deadline=GetTickCount64()+timeout_ms;
        bool locked=false;

        while(GetTickCount64()<=deadline)
        {
            DWORD remaining=(DWORD)(deadline>GetTickCount64()?deadline-GetTickCount64():0);
            HRESULT hr=output_mutex->AcquireSync(1,remaining);
            if(hr==WAIT_TIMEOUT||hr==HRESULT_FROM_WIN32(WAIT_TIMEOUT))
                break;
            if(FAILED(hr))
                break;

            if(st->output_source_frame==source_frame)
            {
                locked=true;
                break;
            }

            ++stale_outputs;
            output_mutex->ReleaseSync(0);

            if(st->output_source_frame>source_frame)
                break;

            Sleep(0);
        }

        if(!locked)
        {
            ++sync_misses;
            device->Release();
            return false;
        }

        ID3D11RenderTargetView *old_rtv=nullptr;
        ID3D11DepthStencilView *old_dsv=nullptr;
        ctx->OMGetRenderTargets(1,&old_rtv,&old_dsv);

        D3D11_VIEWPORT old_vp{};UINT nvp=1;
        ctx->RSGetViewports(&nvp,&old_vp);

        ID3D11VertexShader *old_vs=nullptr;
        ID3D11PixelShader *old_ps=nullptr;
        ctx->VSGetShader(&old_vs,nullptr,nullptr);
        ctx->PSGetShader(&old_ps,nullptr,nullptr);

        ID3D11ShaderResourceView *old_srv=nullptr;
        ID3D11SamplerState *old_sampler=nullptr;
        ctx->PSGetShaderResources(0,1,&old_srv);
        ctx->PSGetSamplers(0,1,&old_sampler);

        ID3D11InputLayout *old_layout=nullptr;
        ctx->IAGetInputLayout(&old_layout);

        D3D11_PRIMITIVE_TOPOLOGY old_topology=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ctx->IAGetPrimitiveTopology(&old_topology);

        D3D11_VIEWPORT vp{};
        vp.Width=(float)st->output_width;vp.Height=(float)st->output_height;vp.MaxDepth=1.0f;

        ctx->OMSetRenderTargets(1,&target_rtv,nullptr);
        ctx->RSSetViewports(1,&vp);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(inject_vs,nullptr,0);
        ctx->PSSetShader(inject_ps,nullptr,0);
        ctx->PSSetShaderResources(0,1,&output_srv);
        ctx->PSSetSamplers(0,1,&inject_sampler);
        ctx->Draw(3,0);

        ID3D11ShaderResourceView *null_srv=nullptr;
        ctx->PSSetShaderResources(0,1,&null_srv);

        ctx->OMSetRenderTargets(1,&old_rtv,old_dsv);
        if(nvp)ctx->RSSetViewports(1,&old_vp);
        ctx->IASetInputLayout(old_layout);
        ctx->IASetPrimitiveTopology(old_topology);
        ctx->VSSetShader(old_vs,nullptr,0);
        ctx->PSSetShader(old_ps,nullptr,0);
        ctx->PSSetShaderResources(0,1,&old_srv);
        ctx->PSSetSamplers(0,1,&old_sampler);

        if(old_rtv)old_rtv->Release();if(old_dsv)old_dsv->Release();
        if(old_vs)old_vs->Release();if(old_ps)old_ps->Release();
        if(old_srv)old_srv->Release();if(old_sampler)old_sampler->Release();
        if(old_layout)old_layout->Release();

        output_mutex->ReleaseSync(0);

        ++injected;

        device->Release();
        return true;
    }

}
