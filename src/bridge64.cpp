#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")

namespace {
constexpr uint32_t MAGIC=0x41314942u,VERSION=101u;
constexpr wchar_t MAPNAME[]=L"Local\\AlienIsolation_DLAA_Bridge_v101";
#pragma pack(push,1)
struct State {
 uint32_t magic,version,producer_pid,helper_pid; int32_t luid_hi; uint32_t luid_lo;
 uint64_t color_handle,motion_handle,depth_handle;
 uint32_t cw,ch,cf,mw,mh,mf,dw,dh,df;
 volatile LONG epoch,produced,consumed,status;
 float jitter_x,jitter_y; uint64_t source_frame;
 float mvx,mvy; uint64_t checksum,dlss_checksum;
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
 volatile LONG sharpness_percent;
 volatile LONG sharpness_serial;
 volatile LONG render_preset;
 volatile LONG render_preset_serial;
};
#pragma pack(pop)

FILE*f=nullptr;
void logl(const char*fmt,...){
 if(!f){
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr,path,MAX_PATH);
  wchar_t*s=wcsrchr(path,L'\\');if(s)*(s+1)=L'\0';
  wchar_t lp[MAX_PATH]{};
  _snwprintf_s(lp,_TRUNCATE,L"%sAlienIsolationDLAA-Bridge64.log",path);
  _wfopen_s(&f,lp,L"a");
 }
 if(!f)return;
 va_list a;va_start(a,fmt);vfprintf(f,fmt,a);va_end(a);
 fputc('\n',f);fflush(f);
}
float h2f(uint16_t h){uint32_t s=(h&0x8000u)<<16,e=(h>>10)&31u,m=h&1023u,v=0;if(!e){if(!m)v=s;else{int x=1;while(!(m&0x400u)){m<<=1;--x;}m&=1023u;v=s|((uint32_t)(x+112)<<23)|(m<<13);}}else if(e==31)v=s|0x7F800000u|(m<<13);else v=s|((e+112)<<23)|(m<<13);float o;memcpy(&o,&v,4);return o;}
uint64_t fnv(const uint8_t*p,size_t n){uint64_t h=1469598103934665603ull;for(size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;}
bool same(const LUID&a,const State*s){return a.HighPart==s->luid_hi&&a.LowPart==s->luid_lo;}

ID3D11Device*mkdev(State*s,ID3D11DeviceContext**ctx){
 IDXGIFactory1*fac=nullptr;if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),(void**)&fac)))return nullptr;
 ID3D11Device*d=nullptr;
 for(UINT i=0;;++i){IDXGIAdapter1*a=nullptr;if(fac->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 ad{};a->GetDesc1(&ad);
  if(same(ad.AdapterLuid,s)){D3D_FEATURE_LEVEL fl{};HRESULT hr=D3D11CreateDevice(a,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d,&fl,ctx);logl("adapter=%ls hr=0x%08X",ad.Description,(uint32_t)hr);a->Release();break;}a->Release();}
 fac->Release();return d;
}
ID3D11Texture2D*stage(ID3D11Device*d,ID3D11Texture2D*s){
 D3D11_TEXTURE2D_DESC x{};s->GetDesc(&x);x.Usage=D3D11_USAGE_STAGING;x.BindFlags=0;x.CPUAccessFlags=D3D11_CPU_ACCESS_READ;x.MiscFlags=0;x.MipLevels=1;x.ArraySize=1;x.SampleDesc.Count=1;x.SampleDesc.Quality=0;
 ID3D11Texture2D*o=nullptr;if(FAILED(d->CreateTexture2D(&x,nullptr,&o)))return nullptr;return o;
}

const char*PROJECT="d8238c51-1f2f-438d-a309-38c16e33c716";
NVSDK_NGX_Parameter* caps=nullptr; NVSDK_NGX_Parameter* runtime=nullptr; NVSDK_NGX_Handle* dlss=nullptr; bool ngxinit=false;
const char*ok(NVSDK_NGX_Result r){return NVSDK_NGX_SUCCEED(r)?"SUCCESS":"FAIL";}
const char*result_name(NVSDK_NGX_Result r){
 switch(r){
  case NVSDK_NGX_Result_Success:return "Success";
  case NVSDK_NGX_Result_Fail:return "Fail";
  case NVSDK_NGX_Result_FAIL_FeatureNotSupported:return "FeatureNotSupported";
  case NVSDK_NGX_Result_FAIL_PlatformError:return "PlatformError";
  case NVSDK_NGX_Result_FAIL_InvalidParameter:return "InvalidParameter";
  case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat:return "UnsupportedInputFormat";
  case NVSDK_NGX_Result_FAIL_RWFlagMissing:return "RWFlagMissing";
  case NVSDK_NGX_Result_FAIL_MissingInput:return "MissingInput";
  case NVSDK_NGX_Result_FAIL_UnsupportedFormat:return "UnsupportedFormat";
  default:return "OtherFailure";
 }
}

bool ngx_init(ID3D11Device*d,const wchar_t*path){
 NVSDK_NGX_Result r=NVSDK_NGX_D3D11_Init_with_ProjectID(PROJECT,NVSDK_NGX_ENGINE_TYPE_CUSTOM,"1.0",path,d);
 logl("NGX Init_with_ProjectID: %s result=0x%08X",ok(r),(uint32_t)r);if(NVSDK_NGX_FAILED(r))return false;ngxinit=true;
 r=NVSDK_NGX_D3D11_GetCapabilityParameters(&caps);logl("NGX GetCapabilityParameters: %s result=0x%08X params=%p",ok(r),(uint32_t)r,caps);if(NVSDK_NGX_FAILED(r)||!caps)return false;
 int avail=0,upd=0;caps->Get(NVSDK_NGX_EParameter_SuperSampling_Available,&avail);caps->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,&upd);
 logl("NGX DLSS capability: available=%d needsUpdatedDriver=%d",avail,upd);return avail>0;
}
bool ngx_create(
 ID3D11DeviceContext*ctx,
 uint32_t inW,uint32_t inH,
 uint32_t outW,uint32_t outH,
 NVSDK_NGX_PerfQuality_Value pq,
 const char*modeName,
 bool autoExposure,
 LONG requestedRenderPreset)
{
 if(dlss)return true;
 NVSDK_NGX_Result r=NVSDK_NGX_D3D11_AllocateParameters(&runtime);
 logl("NGX AllocateParameters: %s result=0x%08X params=%p",ok(r),(uint32_t)r,runtime);
 if(NVSDK_NGX_FAILED(r)||!runtime)return false;

 int renderPreset=NVSDK_NGX_DLSS_Hint_Render_Preset_K;
 if(requestedRenderPreset==10)
  renderPreset=NVSDK_NGX_DLSS_Hint_Render_Preset_J;
 else if(requestedRenderPreset==12)
  renderPreset=NVSDK_NGX_DLSS_Hint_Render_Preset_L;
 else if(requestedRenderPreset==13)
  renderPreset=NVSDK_NGX_DLSS_Hint_Render_Preset_M;

 NVSDK_NGX_Parameter_SetI(
  runtime,
  NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
  renderPreset);

 logl("DLAA render preset=%c (%d)",
  renderPreset==NVSDK_NGX_DLSS_Hint_Render_Preset_J?'J':
  renderPreset==NVSDK_NGX_DLSS_Hint_Render_Preset_L?'L':
  renderPreset==NVSDK_NGX_DLSS_Hint_Render_Preset_M?'M':'K',
  renderPreset);

 NVSDK_NGX_DLSS_Create_Params cp{};
 cp.Feature.InWidth=inW;cp.Feature.InHeight=inH;
 cp.Feature.InTargetWidth=outW;cp.Feature.InTargetHeight=outH;
 cp.Feature.InPerfQualityValue=pq;
 cp.InFeatureCreateFlags=
  autoExposure ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure :
                 NVSDK_NGX_DLSS_Feature_Flags_None;

 r=NGX_D3D11_CREATE_DLSS_EXT(ctx,&dlss,runtime,&cp);
 logl("NGX CREATE %s input=%ux%u output=%ux%u autoExposure=%d: %s result=0x%08X handle=%p",
  modeName,inW,inH,outW,outH,autoExposure?1:0,ok(r),(uint32_t)r,dlss);
 return NVSDK_NGX_SUCCEED(r)&&dlss;
}

void ngx_release_feature(){
 if(dlss){
  auto r=NVSDK_NGX_D3D11_ReleaseFeature(dlss);
  logl("NGX ReleaseFeature(mode switch): %s 0x%08X",ok(r),(uint32_t)r);
  dlss=nullptr;
 }
 if(runtime){
  auto r=NVSDK_NGX_D3D11_DestroyParameters(runtime);
  logl("NGX DestroyParameters(mode switch): %s 0x%08X",ok(r),(uint32_t)r);
  runtime=nullptr;
 }
}

void ngx_shutdown(ID3D11Device*d){
 if(dlss){auto r=NVSDK_NGX_D3D11_ReleaseFeature(dlss);logl("NGX ReleaseFeature: %s 0x%08X",ok(r),(uint32_t)r);dlss=nullptr;}
 if(runtime){auto r=NVSDK_NGX_D3D11_DestroyParameters(runtime);logl("NGX DestroyParameters: %s 0x%08X",ok(r),(uint32_t)r);runtime=nullptr;}
 if(ngxinit){auto r=NVSDK_NGX_D3D11_Shutdown1(d);logl("NGX Shutdown1: %s 0x%08X",ok(r),(uint32_t)r);ngxinit=false;}
}


void log_tex(ID3D11Device*d,const char*name,ID3D11Texture2D*t){
 if(!t){logl("RESOURCE %s = null",name);return;}
 D3D11_TEXTURE2D_DESC x{};t->GetDesc(&x);
 UINT fs=0;HRESULT hr=d->CheckFormatSupport(x.Format,&fs);
 ID3D11Device*owner=nullptr;t->GetDevice(&owner);bool sameDev=owner==d;if(owner)owner->Release();
 logl("RESOURCE %s tex=%p %ux%u fmt=%u bind=0x%X misc=0x%X usage=%u cpu=0x%X samples=%u formatSupportHR=0x%08X support=0x%X sameDevice=%d",
  name,t,x.Width,x.Height,(uint32_t)x.Format,x.BindFlags,x.MiscFlags,(uint32_t)x.Usage,x.CPUAccessFlags,x.SampleDesc.Count,(uint32_t)hr,fs,sameDev?1:0);
}
ID3D11Texture2D*make_private_copy(ID3D11Device*d,ID3D11Texture2D*src,const char*name){
 if(!d||!src)return nullptr;D3D11_TEXTURE2D_DESC x{};src->GetDesc(&x);
 x.MiscFlags &= ~(D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX|D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
 x.Usage=D3D11_USAGE_DEFAULT;x.CPUAccessFlags=0;x.MipLevels=1;x.ArraySize=1;x.SampleDesc.Count=1;x.SampleDesc.Quality=0;
 ID3D11Texture2D*t=nullptr;HRESULT hr=d->CreateTexture2D(&x,nullptr,&t);
 logl("PRIVATE COPY create %s hr=0x%08X tex=%p",name,(uint32_t)hr,t);
 if(t)log_tex(d,name,t);return SUCCEEDED(hr)?t:nullptr;
}


struct FadeProbe {
 ID3D11VertexShader*vs=nullptr;
 ID3D11PixelShader*ps=nullptr;
 ID3D11SamplerState*sampler=nullptr;
 ID3D11Texture2D*gpu=nullptr;
 ID3D11RenderTargetView*rtv=nullptr;
 ID3D11Texture2D*staging[3]{};
 uint32_t ring=0;
 uint64_t submitted=0;
 float last_luma=-1.0f;
 float baseline_luma=-1.0f;
 uint32_t fade_down_frames=0;
 uint32_t fade_up_frames=0;
 uint32_t cooldown_frames=0;
 bool was_black=false;

 void release(){
  for(auto &x:staging){if(x){x->Release();x=nullptr;}}
  if(rtv){rtv->Release();rtv=nullptr;}
  if(gpu){gpu->Release();gpu=nullptr;}
  if(sampler){sampler->Release();sampler=nullptr;}
  if(ps){ps->Release();ps=nullptr;}
  if(vs){vs->Release();vs=nullptr;}
  ring=0;submitted=0;last_luma=-1.0f;baseline_luma=-1.0f;
  fade_down_frames=0;fade_up_frames=0;cooldown_frames=0;was_black=false;
 }

 bool init(ID3D11Device*d){
  if(vs&&ps&&sampler&&gpu&&rtv&&staging[0]&&staging[1]&&staging[2])return true;
  static const char*vsSrc=
   "struct O{float4 p:SV_Position;};"
   "O main(uint id:SV_VertexID){O o;float2 p=float2((id<<1)&2,id&2);"
   "o.p=float4(p*float2(2,-2)+float2(-1,1),0,1);return o;}";
  static const char*psSrc=
   "Texture2D<float4>T:register(t0);SamplerState S:register(s0);"
   "float lum(float3 c){return dot(max(c,0.0),float3(0.2126,0.7152,0.0722));}"
   "float4 main():SV_Target{"
   "float2 p[16]={"
   "float2(.125,.125),float2(.375,.125),float2(.625,.125),float2(.875,.125),"
   "float2(.125,.375),float2(.375,.375),float2(.625,.375),float2(.875,.375),"
   "float2(.125,.625),float2(.375,.625),float2(.625,.625),float2(.875,.625),"
   "float2(.125,.875),float2(.375,.875),float2(.625,.875),float2(.875,.875)};"
   "float v=0;[unroll]for(int i=0;i<16;i++)v+=lum(T.SampleLevel(S,p[i],0).rgb);"
   "v*=0.0625;return float4(v,v,v,v);}";

  ID3DBlob*b=nullptr,*e=nullptr;
  HRESULT hr=D3DCompile(vsSrc,strlen(vsSrc),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&b,&e);
  if(FAILED(hr)){logl("FADE VS compile failed 0x%08X %s",(uint32_t)hr,e?(char*)e->GetBufferPointer():"");if(e)e->Release();return false;}
  hr=d->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&vs);
  b->Release();if(e){e->Release();e=nullptr;} if(FAILED(hr))return false;

  hr=D3DCompile(psSrc,strlen(psSrc),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&b,&e);
  if(FAILED(hr)){logl("FADE PS compile failed 0x%08X %s",(uint32_t)hr,e?(char*)e->GetBufferPointer():"");if(e)e->Release();return false;}
  hr=d->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&ps);
  b->Release();if(e)e->Release(); if(FAILED(hr))return false;

  D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
  hr=d->CreateSamplerState(&sd,&sampler);if(FAILED(hr))return false;

  D3D11_TEXTURE2D_DESC td{};td.Width=1;td.Height=1;td.MipLevels=1;td.ArraySize=1;
  td.Format=DXGI_FORMAT_R32_FLOAT;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;
  td.BindFlags=D3D11_BIND_RENDER_TARGET;
  hr=d->CreateTexture2D(&td,nullptr,&gpu);if(FAILED(hr))return false;
  hr=d->CreateRenderTargetView(gpu,nullptr,&rtv);if(FAILED(hr))return false;

  td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  for(auto &x:staging){hr=d->CreateTexture2D(&td,nullptr,&x);if(FAILED(hr))return false;}

  logl("FADE probe initialized: 16 taps -> 1x1 R32_FLOAT, triple-buffered DO_NOT_WAIT readback");
  return true;
 }

 bool sample(ID3D11Device*d,ID3D11DeviceContext*ctx,ID3D11Texture2D*color,float &lumaOut,bool &transitionOut){
  transitionOut=false;
  if(!d||!ctx||!color||!init(d))return false;

  ID3D11ShaderResourceView*colorSrv=nullptr;
  HRESULT hr=d->CreateShaderResourceView(color,nullptr,&colorSrv);
  if(FAILED(hr)||!colorSrv)return false;

  ID3D11RenderTargetView*oldRTV=nullptr;ID3D11DepthStencilView*oldDSV=nullptr;ctx->OMGetRenderTargets(1,&oldRTV,&oldDSV);
  D3D11_VIEWPORT oldVP{};UINT nvp=1;ctx->RSGetViewports(&nvp,&oldVP);
  ID3D11VertexShader*oldVS=nullptr;ID3D11PixelShader*oldPS=nullptr;ctx->VSGetShader(&oldVS,nullptr,nullptr);ctx->PSGetShader(&oldPS,nullptr,nullptr);
  ID3D11ShaderResourceView*oldSrv=nullptr;ctx->PSGetShaderResources(0,1,&oldSrv);
  ID3D11SamplerState*oldSampler=nullptr;ctx->PSGetSamplers(0,1,&oldSampler);
  ID3D11InputLayout*oldIL=nullptr;ctx->IAGetInputLayout(&oldIL);
  D3D11_PRIMITIVE_TOPOLOGY oldTopo=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;ctx->IAGetPrimitiveTopology(&oldTopo);

  D3D11_VIEWPORT vp{};vp.Width=1;vp.Height=1;vp.MaxDepth=1.0f;
  ctx->OMSetRenderTargets(1,&rtv,nullptr);ctx->RSSetViewports(1,&vp);
  ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->VSSetShader(vs,nullptr,0);ctx->PSSetShader(ps,nullptr,0);
  ctx->PSSetShaderResources(0,1,&colorSrv);ctx->PSSetSamplers(0,1,&sampler);ctx->Draw(3,0);
  ID3D11ShaderResourceView*nullSrv=nullptr;ctx->PSSetShaderResources(0,1,&nullSrv);

  const uint32_t writeIndex=ring%3;
  ctx->CopyResource(staging[writeIndex],gpu);
  ++ring;++submitted;

  ctx->OMSetRenderTargets(1,&oldRTV,oldDSV);if(nvp)ctx->RSSetViewports(1,&oldVP);
  ctx->IASetInputLayout(oldIL);ctx->IASetPrimitiveTopology(oldTopo);
  ctx->VSSetShader(oldVS,nullptr,0);ctx->PSSetShader(oldPS,nullptr,0);
  ctx->PSSetShaderResources(0,1,&oldSrv);ctx->PSSetSamplers(0,1,&oldSampler);

  if(oldRTV)oldRTV->Release();if(oldDSV)oldDSV->Release();if(oldVS)oldVS->Release();if(oldPS)oldPS->Release();
  if(oldSrv)oldSrv->Release();if(oldSampler)oldSampler->Release();if(oldIL)oldIL->Release();colorSrv->Release();

  if(submitted<3)return false;
  const uint32_t readIndex=(ring+1)%3; // two submissions behind
  D3D11_MAPPED_SUBRESOURCE map{};
  hr=ctx->Map(staging[readIndex],0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
  if(hr==DXGI_ERROR_WAS_STILL_DRAWING)return false;
  if(FAILED(hr))return false;

  const float luma=*(const float*)map.pData;ctx->Unmap(staging[readIndex],0);lumaOut=luma;
  if(baseline_luma<0.0f)baseline_luma=luma;

  const bool black=luma<0.020f;
  const bool rapidDrop=last_luma>0.07f && luma<last_luma*0.60f;
  const bool rapidRise=last_luma>=0.0f && last_luma<0.030f && luma>0.075f;

  // ASI v0.3 gradual-fade detection:
  // Track a slowly adapting luminance baseline and require a sustained directional
  // trend. This catches fades spread across many frames while ignoring ordinary
  // one-frame lighting changes in Alien Isolation's dark environments.
  const float relToBaseline = baseline_luma>0.0001f ? (luma/baseline_luma) : 1.0f;
  const bool falling = last_luma>=0.0f && luma < last_luma*0.985f;
  const bool rising  = last_luma>=0.0f && luma > last_luma*1.018f;

  if(falling && baseline_luma>0.055f && relToBaseline<0.78f)
   ++fade_down_frames;
  else if(fade_down_frames>0)
   --fade_down_frames;

  if(rising && luma>0.035f && baseline_luma<0.060f)
   ++fade_up_frames;
  else if(fade_up_frames>0)
   --fade_up_frames;

  // Adapt slowly during normal gameplay, but largely freeze the reference while
  // an actual fade trend is developing.
  if(fade_down_frames<2 && fade_up_frames<2)
   baseline_luma = baseline_luma*0.985f + luma*0.015f;

  const bool gradualFadeDown =
   fade_down_frames>=4 &&
   baseline_luma>0.055f &&
   luma<baseline_luma*0.62f;

  const bool gradualFadeUp =
   fade_up_frames>=4 &&
   baseline_luma<0.060f &&
   luma>std::max(0.080f,baseline_luma*2.25f);

  if(cooldown_frames>0)--cooldown_frames;

  const bool blackCross=(last_luma>=0.0f && black!=was_black);
  const bool rawTransition=rapidDrop||rapidRise||blackCross||gradualFadeDown||gradualFadeUp;
  transitionOut=rawTransition && cooldown_frames==0;

  if(transitionOut){
   cooldown_frames=12;
   logl("FADE transition v0.32 prev=%.6f cur=%.6f base=%.6f rel=%.3f black=%d->%d rapidDrop=%d rapidRise=%d gradualDown=%d(%u) gradualUp=%d(%u)",
    last_luma,luma,baseline_luma,relToBaseline,was_black?1:0,black?1:0,
    rapidDrop?1:0,rapidRise?1:0,gradualFadeDown?1:0,fade_down_frames,
    gradualFadeUp?1:0,fade_up_frames);
   fade_down_frames=0;
   fade_up_frames=0;

   // Once fully black, re-anchor the baseline near black so the reverse fade can
   // be detected cleanly. On a large fade-in, re-anchor to the new scene.
   if(black)baseline_luma=luma;
   else if(rapidRise||gradualFadeUp)baseline_luma=luma;
  }

  last_luma=luma;was_black=black;
  return true;
 }
};

struct DepthConverter {
 ID3D11VertexShader*vs=nullptr;
 ID3D11PixelShader*ps=nullptr;
 ID3D11SamplerState*sampler=nullptr;
 ID3D11Texture2D*tex=nullptr;
 ID3D11RenderTargetView*rtv=nullptr;
 ID3D11ShaderResourceView*srv=nullptr;
 uint32_t w=0,h=0;

 void release(){
  if(srv){srv->Release();srv=nullptr;} if(rtv){rtv->Release();rtv=nullptr;} if(tex){tex->Release();tex=nullptr;}
  if(sampler){sampler->Release();sampler=nullptr;} if(ps){ps->Release();ps=nullptr;} if(vs){vs->Release();vs=nullptr;}
  w=h=0;
 }

 bool ensure_shaders(ID3D11Device*d){
  if(vs&&ps&&sampler)return true;
  static const char*vsSrc=
   "struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
   "O main(uint id:SV_VertexID){O o;float2 p=float2((id<<1)&2,id&2);"
   "o.uv=p;o.p=float4(p*float2(2,-2)+float2(-1,1),0,1);return o;}";
  static const char*psSrc=
   "Texture2D<float> DepthTex:register(t0);SamplerState S:register(s0);"
   "float main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
   "return DepthTex.SampleLevel(S,uv,0);}";
  ID3DBlob*b=nullptr,*e=nullptr;
  HRESULT hr=D3DCompile(vsSrc,strlen(vsSrc),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&b,&e);
  if(FAILED(hr)){logl("DEPTH VS compile failed hr=0x%08X msg=%s",(uint32_t)hr,e?(char*)e->GetBufferPointer():"");if(e)e->Release();return false;}
  hr=d->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&vs);b->Release();if(e){e->Release();e=nullptr;}
  if(FAILED(hr)){logl("DEPTH CreateVertexShader failed 0x%08X",(uint32_t)hr);return false;}
  hr=D3DCompile(psSrc,strlen(psSrc),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&b,&e);
  if(FAILED(hr)){logl("DEPTH PS compile failed hr=0x%08X msg=%s",(uint32_t)hr,e?(char*)e->GetBufferPointer():"");if(e)e->Release();return false;}
  hr=d->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&ps);b->Release();if(e)e->Release();
  if(FAILED(hr)){logl("DEPTH CreatePixelShader failed 0x%08X",(uint32_t)hr);return false;}
  D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
  hr=d->CreateSamplerState(&sd,&sampler);logl("DEPTH sampler create hr=0x%08X",(uint32_t)hr);return SUCCEEDED(hr);
 }

 bool ensure_target(ID3D11Device*d,uint32_t W,uint32_t H){
  if(tex&&W==w&&H==h)return true;
  if(srv){srv->Release();srv=nullptr;}if(rtv){rtv->Release();rtv=nullptr;}if(tex){tex->Release();tex=nullptr;}
  D3D11_TEXTURE2D_DESC td{};td.Width=W;td.Height=H;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R32_FLOAT;td.SampleDesc.Count=1;
  td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  HRESULT hr=d->CreateTexture2D(&td,nullptr,&tex);if(FAILED(hr)){logl("DEPTH R32 texture create failed 0x%08X",(uint32_t)hr);return false;}
  hr=d->CreateRenderTargetView(tex,nullptr,&rtv);if(FAILED(hr)){logl("DEPTH RTV create failed 0x%08X",(uint32_t)hr);return false;}
  hr=d->CreateShaderResourceView(tex,nullptr,&srv);if(FAILED(hr)){logl("DEPTH SRV create failed 0x%08X",(uint32_t)hr);return false;}
  w=W;h=H;logl("DEPTH target R32_FLOAT created %ux%u tex=%p",W,H,tex);return true;
 }

 bool convert(ID3D11Device*d,ID3D11DeviceContext*ctx,ID3D11Texture2D*src){
  if(!d||!ctx||!src)return false;D3D11_TEXTURE2D_DESC sd{};src->GetDesc(&sd);
  if(!ensure_shaders(d)||!ensure_target(d,sd.Width,sd.Height))return false;

  // D24S8 needs a typeless-compatible shader view. Opened shared texture is D24S8;
  // CreateShaderResourceView with R24_UNORM_X8_TYPELESS works on typeless-created depth resources.
  D3D11_SHADER_RESOURCE_VIEW_DESC vd{};vd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;vd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;vd.Texture2D.MipLevels=1;
  ID3D11ShaderResourceView*srcSrv=nullptr;HRESULT hr=d->CreateShaderResourceView(src,&vd,&srcSrv);
  if(FAILED(hr)||!srcSrv){logl("DEPTH source SRV create failed fmt=R24_UNORM_X8_TYPELESS hr=0x%08X",(uint32_t)hr);return false;}

  ID3D11RenderTargetView*oldRTV=nullptr;ID3D11DepthStencilView*oldDSV=nullptr;ctx->OMGetRenderTargets(1,&oldRTV,&oldDSV);
  D3D11_VIEWPORT oldVP{};UINT nvp=1;ctx->RSGetViewports(&nvp,&oldVP);
  ID3D11VertexShader*oldVS=nullptr;ID3D11PixelShader*oldPS=nullptr;ctx->VSGetShader(&oldVS,nullptr,nullptr);ctx->PSGetShader(&oldPS,nullptr,nullptr);

  D3D11_VIEWPORT vp{};vp.Width=(float)sd.Width;vp.Height=(float)sd.Height;vp.MaxDepth=1.0f;
  ctx->OMSetRenderTargets(1,&rtv,nullptr);ctx->RSSetViewports(1,&vp);ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->VSSetShader(vs,nullptr,0);ctx->PSSetShader(ps,nullptr,0);ctx->PSSetShaderResources(0,1,&srcSrv);ctx->PSSetSamplers(0,1,&sampler);
  ctx->Draw(3,0);
  ID3D11ShaderResourceView*nullSrv=nullptr;ctx->PSSetShaderResources(0,1,&nullSrv);

  ctx->OMSetRenderTargets(1,&oldRTV,oldDSV);if(nvp)ctx->RSSetViewports(1,&oldVP);ctx->VSSetShader(oldVS,nullptr,0);ctx->PSSetShader(oldPS,nullptr,0);
  if(oldRTV)oldRTV->Release();if(oldDSV)oldDSV->Release();if(oldVS)oldVS->Release();if(oldPS)oldPS->Release();srcSrv->Release();
  return true;
 }
};
DepthConverter depthConv;
FadeProbe fadeProbe;
ID3D11Texture2D*make_dlss_output(ID3D11Device*d,uint32_t w,uint32_t h){
 D3D11_TEXTURE2D_DESC x{};x.Width=w;x.Height=h;x.MipLevels=1;x.ArraySize=1;
 x.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;x.SampleDesc.Count=1;x.Usage=D3D11_USAGE_DEFAULT;
 x.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
 ID3D11Texture2D*t=nullptr;HRESULT hr=d->CreateTexture2D(&x,nullptr,&t);
 logl("DLSS output create %ux%u fmt=R16G16B16A16_FLOAT bind=SRV|UAV hr=0x%08X tex=%p",w,h,(uint32_t)hr,t);return SUCCEEDED(hr)?t:nullptr;
}

bool make_shared_output(ID3D11Device*d,uint32_t w,uint32_t h,ID3D11Texture2D**tex,IDXGIKeyedMutex**mutex,HANDLE*handle){
 D3D11_TEXTURE2D_DESC x{};x.Width=w;x.Height=h;x.MipLevels=1;x.ArraySize=1;
 x.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;x.SampleDesc.Count=1;x.Usage=D3D11_USAGE_DEFAULT;
 x.BindFlags=D3D11_BIND_SHADER_RESOURCE;x.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
 HRESULT hr=d->CreateTexture2D(&x,nullptr,tex);
 if(FAILED(hr)||!*tex){logl("OUTPUT shared texture create failed 0x%08X",(uint32_t)hr);return false;}
 IDXGIResource*r=nullptr;hr=(*tex)->QueryInterface(__uuidof(IDXGIKeyedMutex),(void**)mutex);
 if(SUCCEEDED(hr))hr=(*tex)->QueryInterface(__uuidof(IDXGIResource),(void**)&r);
 if(SUCCEEDED(hr)&&r)hr=r->GetSharedHandle(handle);if(r)r->Release();
 logl("OUTPUT shared texture %ux%u hr=0x%08X tex=%p handle=0x%llX",w,h,(uint32_t)hr,*tex,(unsigned long long)(uintptr_t)*handle);
 return SUCCEEDED(hr)&&*mutex&&*handle;
}
}


struct DlssModeInfo{
 uint32_t mode=0,inW=0,inH=0;
 NVSDK_NGX_PerfQuality_Value pq=NVSDK_NGX_PerfQuality_Value_DLAA;
 const char*name="DLAA";
};

DlssModeInfo get_mode_info(uint32_t mode,uint32_t outW,uint32_t outH){
 DlssModeInfo r{};r.mode=mode;
 if(mode==0){
  r.inW=outW;r.inH=outH;r.pq=NVSDK_NGX_PerfQuality_Value_DLAA;r.name="DLAA";
  return r;
 }

 r.pq=mode==1?NVSDK_NGX_PerfQuality_Value_MaxQuality:
      mode==2?NVSDK_NGX_PerfQuality_Value_Balanced:
              NVSDK_NGX_PerfQuality_Value_MaxPerf;
 r.name=mode==1?"Quality":mode==2?"Balanced":"Performance";

 unsigned int ow=0,oh=0,maxw=0,maxh=0,minw=0,minh=0;
 float sharpness=0.0f;
 NVSDK_NGX_Result qr=NGX_DLSS_GET_OPTIMAL_SETTINGS(
  caps,outW,outH,r.pq,&ow,&oh,&maxw,&maxh,&minw,&minh,&sharpness);

 if(NVSDK_NGX_SUCCEED(qr)&&ow>0&&oh>0){
  r.inW=ow;r.inH=oh;
  logl("NGX optimal settings %s output=%ux%u input=%ux%u range=%ux%u..%ux%u sharpness=%.3f",
   r.name,outW,outH,ow,oh,minw,minh,maxw,maxh,sharpness);
 }else{
  if(mode==1){r.inW=(outW*2u)/3u;r.inH=(outH*2u)/3u;}
  else if(mode==2){r.inW=(outW*58u)/100u;r.inH=(outH*58u)/100u;}
  else {r.inW=outW/2u;r.inH=outH/2u;}
  logl("NGX optimal settings %s unavailable result=0x%08X fallback=%ux%u",
   r.name,(uint32_t)qr,r.inW,r.inH);
 }
 return r;
}

struct DownscalePipe{
 ID3D11VertexShader*vs=nullptr;
 ID3D11PixelShader*psColor=nullptr;
 ID3D11PixelShader*psColorExact2x=nullptr;
 ID3D11PixelShader*psDepth=nullptr;
 ID3D11SamplerState*linearSampler=nullptr;
 ID3D11SamplerState*pointSampler=nullptr;
 bool loggedExact2x=false;

 void release(){
  if(pointSampler){pointSampler->Release();pointSampler=nullptr;}
  if(linearSampler){linearSampler->Release();linearSampler=nullptr;}
  if(psDepth){psDepth->Release();psDepth=nullptr;}
  if(psColorExact2x){psColorExact2x->Release();psColorExact2x=nullptr;}
  if(psColor){psColor->Release();psColor=nullptr;}
  if(vs){vs->Release();vs=nullptr;}
  loggedExact2x=false;
 }

 bool init(ID3D11Device*d){
  if(vs&&psColor&&psColorExact2x&&psDepth&&linearSampler&&pointSampler)return true;

  static const char*vsSrc=
   "struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
   "O main(uint id:SV_VertexID){O o;float2 q=float2((id<<1)&2,id&2);"
   "o.uv=q;o.p=float4(q*float2(2,-2)+float2(-1,1),0,1);return o;}";

  static const char*psColorSrc=
   "Texture2D<float4>T:register(t0);SamplerState S:register(s0);"
   "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
   "return T.SampleLevel(S,uv,0);}";

  static const char*psColorExact2xSrc=
   "Texture2D<float4>T:register(t0);"
   "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
   "int2 q=int2(p.xy)*2;"
   "float4 a=T.Load(int3(q+int2(0,0),0));"
   "float4 b=T.Load(int3(q+int2(1,0),0));"
   "float4 c=T.Load(int3(q+int2(0,1),0));"
   "float4 d=T.Load(int3(q+int2(1,1),0));"
   "return (a+b+c+d)*0.25;}";

  static const char*psDepthSrc=
   "Texture2D<float>T:register(t0);SamplerState S:register(s0);"
   "float main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
   "return T.SampleLevel(S,uv,0);}";

  auto compile=[&](const char*src,const char*profile,ID3DBlob**blob)->bool{
   ID3DBlob*err=nullptr;
   HRESULT hr=D3DCompile(src,strlen(src),nullptr,nullptr,nullptr,"main",profile,0,0,blob,&err);
   if(FAILED(hr))
    logl("downscale shader compile failed %s hr=0x%08X %s",
     profile,(uint32_t)hr,err?(char*)err->GetBufferPointer():"");
   if(err)err->Release();
   return SUCCEEDED(hr);
  };

  ID3DBlob*b=nullptr;
  if(!compile(vsSrc,"vs_5_0",&b))return false;
  HRESULT hr=d->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&vs);
  b->Release();if(FAILED(hr))return false;

  if(!compile(psColorSrc,"ps_5_0",&b))return false;
  hr=d->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&psColor);
  b->Release();if(FAILED(hr))return false;

  if(!compile(psColorExact2xSrc,"ps_5_0",&b))return false;
  hr=d->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&psColorExact2x);
  b->Release();if(FAILED(hr))return false;

  if(!compile(psDepthSrc,"ps_5_0",&b))return false;
  hr=d->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&psDepth);
  b->Release();if(FAILED(hr))return false;

  D3D11_SAMPLER_DESC sd{};
  sd.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD=D3D11_FLOAT32_MAX;
  hr=d->CreateSamplerState(&sd,&linearSampler);if(FAILED(hr))return false;

  sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
  hr=d->CreateSamplerState(&sd,&pointSampler);if(FAILED(hr))return false;

  logl("downscale pipeline ready");
  return true;
 }
};

ID3D11Texture2D* make_scaled_input(
 ID3D11Device*d,uint32_t w,uint32_t h,DXGI_FORMAT fmt,const char*name)
{
 D3D11_TEXTURE2D_DESC td{};
 td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=fmt;
 td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;
 td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
 ID3D11Texture2D*t=nullptr;
 HRESULT hr=d->CreateTexture2D(&td,nullptr,&t);
 logl("scaled input create %s %ux%u fmt=%u hr=0x%08X tex=%p",
  name,w,h,(uint32_t)fmt,(uint32_t)hr,t);
 return SUCCEEDED(hr)?t:nullptr;
}

bool copy_render_subrect(
 ID3D11DeviceContext*ctx,
 ID3D11Texture2D*src,
 ID3D11Texture2D*dst,
 uint32_t w,
 uint32_t h,
 const char*name)
{
 if(!ctx||!src||!dst||w==0||h==0)return false;

 D3D11_TEXTURE2D_DESC sd{},dd{};
 src->GetDesc(&sd);dst->GetDesc(&dd);

 if(sd.Format!=dd.Format||dd.Width!=w||dd.Height!=h||
    sd.Width<w||sd.Height<h)
 {
  logl("SUBRECT COPY invalid %s src=%ux%u fmt=%u dst=%ux%u fmt=%u requested=%ux%u",
   name,sd.Width,sd.Height,(uint32_t)sd.Format,
   dd.Width,dd.Height,(uint32_t)dd.Format,w,h);
  return false;
 }

 D3D11_BOX box{};
 box.left=0;box.top=0;box.front=0;
 box.right=w;box.bottom=h;box.back=1;

 ctx->CopySubresourceRegion(dst,0,0,0,0,src,0,&box);

 static uint64_t copies=0;
 ++copies;
 if(copies<=8||(copies%3600ull)==0)
  logl("SUBRECT COPY %s count=%llu src=%ux%u dst=%ux%u",
   name,(unsigned long long)copies,sd.Width,sd.Height,w,h);

 return true;
}

bool downscale_input(
 ID3D11Device*d,ID3D11DeviceContext*ctx,DownscalePipe&pipe,
 ID3D11Texture2D*src,ID3D11Texture2D*dst,bool depth)
{
 if(!d||!ctx||!src||!dst||!pipe.init(d))return false;

 ID3D11ShaderResourceView*srv=nullptr;
 ID3D11RenderTargetView*rtv=nullptr;
 HRESULT hr=d->CreateShaderResourceView(src,nullptr,&srv);
 if(FAILED(hr)||!srv)return false;
 hr=d->CreateRenderTargetView(dst,nullptr,&rtv);
 if(FAILED(hr)||!rtv){srv->Release();return false;}

 D3D11_TEXTURE2D_DESC td{};dst->GetDesc(&td);
 D3D11_VIEWPORT vp{};vp.Width=(float)td.Width;vp.Height=(float)td.Height;vp.MaxDepth=1.0f;

 ID3D11RenderTargetView*oldRTV=nullptr;ID3D11DepthStencilView*oldDSV=nullptr;
 ctx->OMGetRenderTargets(1,&oldRTV,&oldDSV);
 D3D11_VIEWPORT oldVP{};UINT nvp=1;ctx->RSGetViewports(&nvp,&oldVP);
 ID3D11VertexShader*oldVS=nullptr;ID3D11PixelShader*oldPS=nullptr;
 ctx->VSGetShader(&oldVS,nullptr,nullptr);ctx->PSGetShader(&oldPS,nullptr,nullptr);
 ID3D11ShaderResourceView*oldSrv=nullptr;ctx->PSGetShaderResources(0,1,&oldSrv);
 ID3D11SamplerState*oldSampler=nullptr;ctx->PSGetSamplers(0,1,&oldSampler);
 ID3D11InputLayout*oldIL=nullptr;ctx->IAGetInputLayout(&oldIL);
 D3D11_PRIMITIVE_TOPOLOGY oldTopo=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
 ctx->IAGetPrimitiveTopology(&oldTopo);

 D3D11_TEXTURE2D_DESC srcDesc{};src->GetDesc(&srcDesc);
 const bool exact2xColor =
  !depth &&
  srcDesc.Width == td.Width * 2u &&
  srcDesc.Height == td.Height * 2u;

 ctx->OMSetRenderTargets(1,&rtv,nullptr);ctx->RSSetViewports(1,&vp);
 ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 ctx->VSSetShader(pipe.vs,nullptr,0);
 ctx->PSSetShader(
  depth ? pipe.psDepth :
  (exact2xColor ? pipe.psColorExact2x : pipe.psColor),
  nullptr,0);
 ctx->PSSetShaderResources(0,1,&srv);
 ID3D11SamplerState*sampler=depth?pipe.pointSampler:pipe.linearSampler;

 if(exact2xColor&&!pipe.loggedExact2x){
  pipe.loggedExact2x=true;
  logl("HDR DOWNSAMPLE exact 2x2 box enabled src=%ux%u dst=%ux%u fmt=%u",
   srcDesc.Width,srcDesc.Height,td.Width,td.Height,(uint32_t)srcDesc.Format);
 }
 ctx->PSSetSamplers(0,1,&sampler);ctx->Draw(3,0);

 ID3D11ShaderResourceView*nullSrv=nullptr;ctx->PSSetShaderResources(0,1,&nullSrv);

 ctx->OMSetRenderTargets(1,&oldRTV,oldDSV);if(nvp)ctx->RSSetViewports(1,&oldVP);
 ctx->IASetInputLayout(oldIL);ctx->IASetPrimitiveTopology(oldTopo);
 ctx->VSSetShader(oldVS,nullptr,0);ctx->PSSetShader(oldPS,nullptr,0);
 ctx->PSSetShaderResources(0,1,&oldSrv);ctx->PSSetSamplers(0,1,&oldSampler);

 if(oldRTV)oldRTV->Release();if(oldDSV)oldDSV->Release();
 if(oldVS)oldVS->Release();if(oldPS)oldPS->Release();
 if(oldSrv)oldSrv->Release();if(oldSampler)oldSampler->Release();if(oldIL)oldIL->Release();
 rtv->Release();srv->Release();
 return true;
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 wchar_t p[MAX_PATH]{};GetModuleFileNameW(nullptr,p,MAX_PATH);wchar_t*s=wcsrchr(p,L'\\');if(s)*(s+1)=L'\0';
 logl("Alien: Isolation DLAA Bridge64 v1.0 PID=%lu",GetCurrentProcessId());

 HANDLE map=nullptr;for(int i=0;i<100&&!map;++i){map=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,MAPNAME);if(!map)Sleep(50);}
 if(!map){logl("OpenFileMapping failed error=%lu name=%ls",GetLastError(),MAPNAME);return 2;}
 logl("mapping opened name=%ls",MAPNAME);
 State*st=(State*)MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(State));
 if(!st||st->magic!=MAGIC||st->version!=VERSION){logl("invalid state magic=0x%08X version=%u",st?st->magic:0,st?st->version:0);return 3;}
 st->helper_pid=GetCurrentProcessId();st->status=1;

 ID3D11DeviceContext*ctx=nullptr;ID3D11Device*d=mkdev(st,&ctx);if(!d||!ctx){st->status=-10;return 4;}
 bool ngxsupported=ngx_init(d,p);logl("NGX smoke init supported=%d",ngxsupported?1:0);

 LONG opened=-1;
 ID3D11Texture2D *c=nullptr,*m=nullptr,*dep=nullptr,*pc=nullptr,*pm=nullptr,*pd=nullptr,*out=nullptr,*sharedOut=nullptr;
 ID3D11Texture2D *scaledColor=nullptr,*scaledDepth=nullptr,*scaledMotion=nullptr;
 IDXGIKeyedMutex *cm=nullptr,*mm=nullptr,*dm=nullptr,*sharedOutMutex=nullptr;
 HANDLE sharedOutHandle=nullptr;
 DownscalePipe downscalePipe;
 DlssModeInfo activeMode{};
 bool activeAutoExposure=true;
 LONG lastModeSerial=-1;
 LONG lastSharpnessSerial=-1;
 LONG lastRenderPresetSerial=-1;

 auto rel=[&](){
  if(scaledColor){scaledColor->Release();scaledColor=nullptr;}
  if(scaledDepth){scaledDepth->Release();scaledDepth=nullptr;}
  if(scaledMotion){scaledMotion->Release();scaledMotion=nullptr;}
  if(out){out->Release();out=nullptr;}
  if(sharedOutMutex){sharedOutMutex->Release();sharedOutMutex=nullptr;}if(sharedOut){sharedOut->Release();sharedOut=nullptr;}sharedOutHandle=nullptr;
  if(pc){pc->Release();pc=nullptr;}if(pm){pm->Release();pm=nullptr;}if(pd){pd->Release();pd=nullptr;}
  if(cm){cm->Release();cm=nullptr;}if(mm){mm->Release();mm=nullptr;}if(dm){dm->Release();dm=nullptr;}
  if(c){c->Release();c=nullptr;}if(m){m->Release();m=nullptr;}if(dep){dep->Release();dep=nullptr;}
 };

 uint64_t n=0;
 uint64_t previousSourceFrame=0;
 bool havePreviousSourceFrame=false;
 LONG lastResetSerial=0;
 while(true){
  HANDLE ph=OpenProcess(SYNCHRONIZE,FALSE,st->producer_pid);if(!ph)break;DWORD alive=WaitForSingleObject(ph,0);CloseHandle(ph);if(alive==WAIT_OBJECT_0)break;
  LONG e=st->epoch;
  if(e>0&&e!=opened){
   rel();
   HRESULT hc=d->OpenSharedResource((HANDLE)(uintptr_t)st->color_handle,__uuidof(ID3D11Texture2D),(void**)&c);
   HRESULT hm=d->OpenSharedResource((HANDLE)(uintptr_t)st->motion_handle,__uuidof(ID3D11Texture2D),(void**)&m);
   HRESULT hd=d->OpenSharedResource((HANDLE)(uintptr_t)st->depth_handle,__uuidof(ID3D11Texture2D),(void**)&dep);
   if(FAILED(hc)||FAILED(hm)||FAILED(hd)||!c||!m||!dep){st->status=-20;logl("OpenSharedResource failed color=0x%08X motion=0x%08X depth=0x%08X",(uint32_t)hc,(uint32_t)hm,(uint32_t)hd);Sleep(250);continue;}
   c->QueryInterface(__uuidof(IDXGIKeyedMutex),(void**)&cm);m->QueryInterface(__uuidof(IDXGIKeyedMutex),(void**)&mm);dep->QueryInterface(__uuidof(IDXGIKeyedMutex),(void**)&dm);
   log_tex(d,"sharedColor",c);log_tex(d,"sharedMotion",m);log_tex(d,"sharedDepth",dep);
   pc=make_private_copy(d,c,"privateColor");pm=make_private_copy(d,m,"privateMotion");pd=make_private_copy(d,dep,"privateDepth");
   out=make_dlss_output(d,st->cw,st->ch);
   bool outputShared=make_shared_output(d,st->cw,st->ch,&sharedOut,&sharedOutMutex,&sharedOutHandle);
   if(!cm||!mm||!dm||!pc||!pm||!pd||!out||!outputShared){st->status=-21;logl("resource prep failed cm=%p mm=%p dm=%p pc=%p pm=%p pd=%p out=%p sharedOut=%p",cm,mm,dm,pc,pm,pd,out,sharedOut);Sleep(250);continue;}
   st->output_handle=(uint64_t)(uintptr_t)sharedOutHandle;st->output_width=st->cw;st->output_height=st->ch;st->output_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
   MemoryBarrier();InterlockedIncrement(&st->output_epoch);
   log_tex(d,"dlssOutput",out);
   D3D11_TEXTURE2D_DESC a{},b{},z{};c->GetDesc(&a);m->GetDesc(&b);dep->GetDesc(&z);
   logl("OPENED epoch=%ld color=%ux%u fmt=%u motion=%ux%u fmt=%u depth=%ux%u fmt=%u",e,a.Width,a.Height,(uint32_t)a.Format,b.Width,b.Height,(uint32_t)b.Format,z.Width,z.Height,(uint32_t)z.Format);
   lastModeSerial=-1;
   lastSharpnessSerial=-1;
   lastRenderPresetSerial=-1;
   opened=e;st->status=2;
  }

  if(!cm||!mm||!dm){Sleep(10);continue;}

  const LONG modeSerial=st->mode_serial;
  const LONG sharpnessSerial=st->sharpness_serial;
  const LONG renderPresetSerial=st->render_preset_serial;
  LONG requestedRenderPreset=st->render_preset;
  if(requestedRenderPreset<10||requestedRenderPreset>13)
   requestedRenderPreset=11;
  const LONG requestedSharpnessPercent=st->sharpness_percent;
  const float requestedSharpness=
   static_cast<float>(
    std::clamp<LONG>(requestedSharpnessPercent,0,50)) / 100.0f;
  const uint32_t requestedMode=
   (st->requested_mode<0||st->requested_mode>3)?0u:(uint32_t)st->requested_mode;
  const bool requestedAutoExposure=(st->auto_exposure!=0);

  if(modeSerial!=lastModeSerial||
     sharpnessSerial!=lastSharpnessSerial||
     renderPresetSerial!=lastRenderPresetSerial||
     !dlss)
  {
   ngx_release_feature();

   if(scaledColor){scaledColor->Release();scaledColor=nullptr;}
   if(scaledDepth){scaledDepth->Release();scaledDepth=nullptr;}
   if(scaledMotion){scaledMotion->Release();scaledMotion=nullptr;}

   activeMode=get_mode_info(requestedMode,st->cw,st->ch);

   if(activeMode.mode!=0)
   {
    scaledColor=make_scaled_input(
     d,activeMode.inW,activeMode.inH,DXGI_FORMAT_R11G11B10_FLOAT,"scaledColor");
    scaledDepth=make_scaled_input(
     d,activeMode.inW,activeMode.inH,DXGI_FORMAT_R32_FLOAT,"scaledDepth");
    scaledMotion=make_scaled_input(
     d,activeMode.inW,activeMode.inH,DXGI_FORMAT_R16G16_FLOAT,"scaledMotion");

    if(!scaledColor||!scaledDepth||!scaledMotion)
    {
     st->status=-30;
     logl("mode resource creation failed mode=%s",activeMode.name);
     Sleep(100);
     continue;
    }
   }

   if(!ngxsupported||!ngx_create(
      ctx,activeMode.inW,activeMode.inH,st->cw,st->ch,
      activeMode.pq,activeMode.name,requestedAutoExposure,
      requestedRenderPreset))
   {
    st->status=-31;
    logl("mode feature creation failed mode=%s",activeMode.name);
    Sleep(100);
    continue;
   }

   st->active_mode=(LONG)activeMode.mode;
   st->active_render_width=activeMode.inW;
   st->active_render_height=activeMode.inH;
   st->status=2;
   activeAutoExposure=requestedAutoExposure;

   lastModeSerial=modeSerial;
   lastSharpnessSerial=sharpnessSerial;
   lastRenderPresetSerial=renderPresetSerial;
   previousSourceFrame=0;
   havePreviousSourceFrame=false;

   logl("DLSS MODE ACTIVE %s input=%ux%u output=%ux%u autoExposure=%d serial=%ld sharpness=%.2f sharpSerial=%ld preset=%c presetSerial=%ld",
    activeMode.name,activeMode.inW,activeMode.inH,st->cw,st->ch,
    activeAutoExposure?1:0,modeSerial,requestedSharpness,sharpnessSerial,
    requestedRenderPreset==10?'J':
    requestedRenderPreset==12?'L':
    requestedRenderPreset==13?'M':'K',
    renderPresetSerial);
  }

  HRESULT hc=cm->AcquireSync(1,100);if(hc==WAIT_TIMEOUT)continue;if(FAILED(hc)){logl("color AcquireSync failed 0x%08X",(uint32_t)hc);break;}
  HRESULT hm=mm->AcquireSync(1,100);if(FAILED(hm)){cm->ReleaseSync(1);continue;}
  HRESULT hd=dm->AcquireSync(1,100);if(FAILED(hd)){mm->ReleaseSync(1);cm->ReleaseSync(1);continue;}

  ctx->CopyResource(pc,c);ctx->CopyResource(pm,m);ctx->CopyResource(pd,dep);

  ID3D11Texture2D*evalColor=pc;
  ID3D11Texture2D*evalDepth=pd;
  ID3D11Texture2D*evalMotion=pm;

  if(activeMode.mode!=0)
  {
   if(!downscale_input(
         d,ctx,downscalePipe,pc,scaledColor,false)||
      !downscale_input(
         d,ctx,downscalePipe,pd,scaledDepth,true)||
      !downscale_input(
         d,ctx,downscalePipe,pm,scaledMotion,false))
   {
    logl("full-frame resample failed mode=%s",activeMode.name);
    dm->ReleaseSync(0);mm->ReleaseSync(0);cm->ReleaseSync(0);
    Sleep(1);
    continue;
   }

   static uint64_t fullFrameResamples=0;
   ++fullFrameResamples;
   if(fullFrameResamples<=8||(fullFrameResamples%1800ull)==0)
    logl("FULL_FRAME_RESAMPLE count=%llu src=3840x2160 dst=%ux%u color=1 depth=1 motion=1",
     (unsigned long long)fullFrameResamples,
     activeMode.inW,activeMode.inH);

   evalColor=scaledColor;
   evalDepth=scaledDepth;
   evalMotion=scaledMotion;
  }

  const float jx=st->jitter_x,jy=st->jitter_y;const uint64_t sf=st->source_frame;
  NVSDK_NGX_Result er=NVSDK_NGX_Result_Fail;
  bool resetThisEval=false;
  if(dlss&&runtime&&out){
   const bool depthConverted=(st->df==DXGI_FORMAT_R32_FLOAT);
   float fadeLuma=-1.0f;bool fadeTransition=false;
   fadeProbe.sample(d,ctx,evalColor,fadeLuma,fadeTransition);

   NVSDK_NGX_D3D11_DLSS_Eval_Params ep{};
   ep.Feature.pInColor=evalColor;ep.Feature.pInOutput=out;ep.pInDepth=evalDepth;ep.pInMotionVectors=evalMotion;
   ep.InRenderSubrectDimensions.Width=activeMode.inW;ep.InRenderSubrectDimensions.Height=activeMode.inH;
   ep.InJitterOffsetX=jx;ep.InJitterOffsetY=jy;
   const LONG evalSharpnessPercent=st->sharpness_percent;
   ep.Feature.InSharpness=
    static_cast<float>(
     std::clamp<LONG>(evalSharpnessPercent,0,50)) / 100.0f;

   if(!activeAutoExposure)
   {
    // Manual exposure test: no exposure texture. Neutral scale/pre-exposure
    // explicitly tells NGX to preserve the supplied HDR scene values.
    ep.pInExposureTexture=nullptr;
    ep.InPreExposure=1.0f;
    ep.InExposureScale=1.0f;
   }

   // Proven Alien Isolation motion-vector convention.
   ep.InMVScaleX=-(float)(activeMode.mode==0?st->mw:activeMode.inW);
   ep.InMVScaleY=-(float)(activeMode.mode==0?st->mh:activeMode.inH);
   const bool firstEval = (st->dlss_eval_count==0);
   const bool frameGap = havePreviousSourceFrame && (sf != previousSourceFrame + 1ull);
   const LONG resetSerial=st->reset_serial;
   const bool requestedReset=(resetSerial!=lastResetSerial);
   resetThisEval=(firstEval||frameGap||fadeTransition||requestedReset);
   if(requestedReset){
    lastResetSerial=resetSerial;
    logl("DLSS history reset: ASI request serial=%ld sourceFrame=%llu",
      resetSerial,(unsigned long long)sf);
   }
   ep.InReset=resetThisEval?1:0;
   if(frameGap)
    logl("DLSS history reset: sourceFrame gap previous=%llu current=%llu delta=%lld",
      (unsigned long long)previousSourceFrame,(unsigned long long)sf,(long long)(sf-previousSourceFrame));
   if(fadeTransition)
    logl("DLSS history reset: fade/scene transition sourceFrame=%llu luma=%.6f",
      (unsigned long long)sf,fadeLuma);
   if(st->dlss_eval_count==0){
    logl("EVAL PREFLIGHT v1.0 mode=%s autoExposure=%d MV_DIRECTION=INVERTED_PERMANENT color=%p depth=%p motion=%p output=%p render=%ux%u outputSize=%ux%u jitter=(%.3f,%.3f) MVScale=(%.1f,%.1f) sharpness=%.2f reset=%d",
      activeMode.name,activeAutoExposure?1:0,ep.Feature.pInColor,ep.pInDepth,ep.pInMotionVectors,ep.Feature.pInOutput,
      ep.InRenderSubrectDimensions.Width,ep.InRenderSubrectDimensions.Height,st->cw,st->ch,
      ep.InJitterOffsetX,ep.InJitterOffsetY,ep.InMVScaleX,ep.InMVScaleY,
      ep.Feature.InSharpness,ep.InReset);
   }
   er=NGX_D3D11_EVALUATE_DLSS_EXT(ctx,dlss,runtime,&ep);
   if(st->dlss_eval_count==0){
    logl("DEPTH TEST x86Converted=%d depthFmt=%u depthTex=%p result=%s/%s 0x%08X",
      depthConverted?1:0,st->df,ep.pInDepth,ok(er),result_name(er),(uint32_t)er);
   }
   InterlockedIncrement(&st->dlss_eval_count);st->dlss_last_result=(LONG)er;
   previousSourceFrame=sf;
   havePreviousSourceFrame=true;
   if(NVSDK_NGX_SUCCEED(er)){
    InterlockedIncrement(&st->dlss_eval_success);
    if(sharedOutMutex&&sharedOut&&SUCCEEDED(sharedOutMutex->AcquireSync(0,50))){
     ctx->CopyResource(sharedOut,out);
     st->output_source_frame=sf;MemoryBarrier();InterlockedIncrement(&st->output_frames);
     sharedOutMutex->ReleaseSync(1);
    }
   }
  }

  // ASI v0.3: no GPU->CPU staging/readback in the live path.
  // Periodic checksum/motion sampling from v0.25 was removed because its 120-frame
  // cadence matched the visible ~2 second flicker during output injection.

  dm->ReleaseSync(0);mm->ReleaseSync(0);cm->ReleaseSync(0);
  ++n;InterlockedIncrement(&st->consumed);

  if(n==1){
   logl("EVAL=%llu mode=%s render=%ux%u sourceFrame=%llu result=%s/%s 0x%08X jitter=(%.3f,%.3f) MVScale=(%u,%u) mvDirection=INVERTED_PERMANENT reset=%d success=%ld/%ld producer=%ld outputFrames=%ld",
    (unsigned long long)n,activeMode.name,activeMode.inW,activeMode.inH,
    (unsigned long long)sf,ok(er),result_name(er),(uint32_t)er,jx,jy,
    (activeMode.mode==0?st->mw:activeMode.inW),
    (activeMode.mode==0?st->mh:activeMode.inH),
    resetThisEval?1:0,st->dlss_eval_success,st->dlss_eval_count,st->produced,st->output_frames);
  }
 }

 rel();downscalePipe.release();fadeProbe.release();depthConv.release();st->status=0;ngx_shutdown(d);ctx->Release();d->Release();UnmapViewOfFile(st);CloseHandle(map);logl("shutdown");if(f)fclose(f);return 0;
}
