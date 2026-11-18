// nwgpuprobe "chromium" tests (included after nwgpuprobe_d3d11ui.inc).
// Why: on 2766 Edge's GPU process stops at skia_output_surface_impl.cc:1228
// after "skia_output_device_gl.cc:223 Couldn't create surface" (ANGLE's EGL
// window surface over D3D11), with and without DirectComposition. Each line
// is one call Chromium/ANGLE makes while it brings up its output surface:
//   chromium device        : D3D11 FL 11_1 + BGRA, ID3D11Device1/2/3, IDXGIDevice2
//   chromium feature_*     : CheckFeatureSupport the ANGLE renderer reads
//   chromium tearing       : IDXGIFactory5 PRESENT_ALLOW_TEARING
//   chromium output_*      : IDXGIOutput enumeration, IDXGIOutput6::GetDesc1
//   chromium fmt_*         : CheckFormatSupport (DISPLAY, RT, SRV, BLENDABLE)
//   chromium tex_*         : CreateTexture2D, BGRA8/RGBA8/RGB10A2/RGBA16F x
//                            {plain, SHARED, SHARED_KEYEDMUTEX, SHARED_NTHANDLE,
//                             SHARED_NTHANDLE|SHARED_KEYEDMUTEX}, then the share
//                            handle / NT handle / keyed mutex / open on a second
//                            device
//   chromium swap_*        : CreateSwapChainForHwnd FLIP_DISCARD/FLIP_SEQUENTIAL/
//                            DISCARD/SEQUENTIAL with ALLOW_TEARING and
//                            FRAME_LATENCY_WAITABLE_OBJECT, Present1 with dirty
//                            rects, Present(ALLOW_TEARING), ResizeBuffers
//   chromium dcomp_*       : CreateSwapChainForComposition + DCompositionCreate
//                            Device + target + visual + Commit, Present
//   chromium video_device  : ID3D11VideoDevice QueryInterface
// Every line prints the HRESULT; "result=FAIL" marks a call that fails here.

#include <d3d11_4.h>
#include <dcomp.h>
#pragma comment(lib, "dcomp.lib")

namespace chromium {

static int g_fail = 0;
static ID3D11Device *g_d = nullptr, *g_d2 = nullptr;
static ID3D11DeviceContext *g_ctx = nullptr, *g_ctx2 = nullptr;
static IDXGIFactory2 *g_f2 = nullptr;
HRESULT MakeDevice(ID3D11Device **dev, ID3D11DeviceContext **ctx, D3D_FEATURE_LEVEL *got);

// K35 v2: one refused call must not hide the rest. When the device was
// removed, say so (with the test that preceded it) and make a new one.
void Fresh(const char *before) {
  HRESULT why = g_d ? g_d->GetDeviceRemovedReason() : E_POINTER;
  if (SUCCEEDED(why)) return;
  Out("chromium device_removed before=%s reason=0x%08lx", before, (unsigned long)why);
  Rel(g_f2); Rel(g_ctx2); Rel(g_d2); Rel(g_ctx); Rel(g_d);
  D3D_FEATURE_LEVEL fl;
  HRESULT hr = MakeDevice(&g_d, &g_ctx, &fl);
  if (SUCCEEDED(hr)) hr = MakeDevice(&g_d2, &g_ctx2, &fl);
  IDXGIDevice *xd = nullptr; IDXGIAdapter *a = nullptr;
  if (SUCCEEDED(hr)) hr = g_d->QueryInterface(IID_PPV_ARGS(&xd));
  if (SUCCEEDED(hr)) hr = xd->GetAdapter(&a);
  if (SUCCEEDED(hr)) hr = a->GetParent(IID_PPV_ARGS(&g_f2));
  Rel(a); Rel(xd);
  Out("chromium device_recreated hr=0x%08lx", (unsigned long)hr);
}

void R(const char *name, HRESULT hr, const char *extra = "") {
  if (FAILED(hr)) ++g_fail;
  Out("chromium %s result=%s hr=0x%08lx %s", name, SUCCEEDED(hr) ? "PASS" : "FAIL", (unsigned long)hr, extra);
}

const char *FmtName(DXGI_FORMAT f) {
  switch (f) {
  case DXGI_FORMAT_B8G8R8A8_UNORM: return "bgra8";
  case DXGI_FORMAT_R8G8B8A8_UNORM: return "rgba8";
  case DXGI_FORMAT_R10G10B10A2_UNORM: return "rgb10a2";
  case DXGI_FORMAT_R16G16B16A16_FLOAT: return "rgba16f";
  default: return "other";
  }
}

HRESULT MakeDevice(ID3D11Device **dev, ID3D11DeviceContext **ctx, D3D_FEATURE_LEVEL *got) {
  static const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                         D3D_FEATURE_LEVEL_10_0};
  return D3D11CreateDevice(nullptr, g_warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, fl, 4, D3D11_SDK_VERSION, dev, got, ctx);
}

void Features(ID3D11Device *d) {
  D3D11_FEATURE_DATA_THREADING th = {};
  R("feature_threading", d->CheckFeatureSupport(D3D11_FEATURE_THREADING, &th, sizeof(th)));
  D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS hw = {};
  R("feature_d3d10x", d->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &hw, sizeof(hw)));
  D3D11_FEATURE_DATA_D3D11_OPTIONS o = {};
  HRESULT hr = d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof(o));
  char e[128];
  snprintf(e, sizeof(e), "outputMergerLogicOp=%d constantBufferOffsetting=%d", o.OutputMergerLogicOp,
           o.ConstantBufferOffsetting);
  R("feature_options", hr, e);
  D3D11_FEATURE_DATA_D3D11_OPTIONS1 o1 = {};
  R("feature_options1", d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &o1, sizeof(o1)));
  D3D11_FEATURE_DATA_D3D11_OPTIONS2 o2 = {};
  R("feature_options2", d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &o2, sizeof(o2)));
  D3D11_FEATURE_DATA_D3D11_OPTIONS3 o3 = {};
  R("feature_options3", d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3, &o3, sizeof(o3)));
  D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT sh = {};
  R("feature_d3d9_shadow", d->CheckFeatureSupport(D3D11_FEATURE_D3D9_SHADOW_SUPPORT, &sh, sizeof(sh)));
  D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT mp = {};
  R("feature_min_precision", d->CheckFeatureSupport(D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &mp, sizeof(mp)));
  D3D11_FEATURE_DATA_MARKER_SUPPORT mk = {};
  R("feature_marker", d->CheckFeatureSupport(D3D11_FEATURE_MARKER_SUPPORT, &mk, sizeof(mk)));
}

void Outputs(IDXGIAdapter1 *a) {
  for (UINT i = 0;; ++i) {
    IDXGIOutput *o = nullptr;
    HRESULT hr = a->EnumOutputs(i, &o);
    if (hr == DXGI_ERROR_NOT_FOUND) { Out("chromium output_count=%u", i); if (!i) R("output_enum", E_FAIL, "no outputs"); break; }
    if (FAILED(hr) || !o) { R("output_enum", FAILED(hr) ? hr : E_POINTER); break; }
    DXGI_OUTPUT_DESC d = {};
    o->GetDesc(&d);
    char e[160];
    snprintf(e, sizeof(e), "index=%u attached=%d rect=%ld,%ld,%ld,%ld", i, d.AttachedToDesktop, d.DesktopCoordinates.left,
             d.DesktopCoordinates.top, d.DesktopCoordinates.right, d.DesktopCoordinates.bottom);
    R("output_desc", hr, e);
    IDXGIOutput6 *o6 = nullptr;
    hr = o->QueryInterface(IID_PPV_ARGS(&o6));
    if (SUCCEEDED(hr)) {
      DXGI_OUTPUT_DESC1 d1 = {};
      hr = o6->GetDesc1(&d1);
      snprintf(e, sizeof(e), "bits=%u colorspace=%u maxlum=%.1f", d1.BitsPerColor, (unsigned)d1.ColorSpace, d1.MaxLuminance);
      R("output_desc1", hr, e);
      UINT support = 0;
      hr = o6->CheckHardwareCompositionSupport(&support);
      snprintf(e, sizeof(e), "flags=0x%x", support);
      R("output_hw_composition", hr, e);
    } else R("output6_qi", hr);
    UINT n = 0;
    hr = o->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &n, nullptr);
    snprintf(e, sizeof(e), "modes=%u", n);
    R("output_modes_bgra8", hr, e);
    Rel(o6); Rel(o);
  }
}

void Formats(ID3D11Device *d) {
  const DXGI_FORMAT fs[] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM,
                            DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_NV12, DXGI_FORMAT_P010, DXGI_FORMAT_YUY2};
  for (DXGI_FORMAT f : fs) {
    UINT s = 0;
    HRESULT hr = d->CheckFormatSupport(f, &s);
    char n[64], e[160];
    snprintf(n, sizeof(n), "fmt_%u", (unsigned)f);
    snprintf(e, sizeof(e), "support=0x%08x display=%d rt=%d srv=%d blend=%d", s, !!(s & D3D11_FORMAT_SUPPORT_DISPLAY),
             !!(s & D3D11_FORMAT_SUPPORT_RENDER_TARGET), !!(s & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE),
             !!(s & D3D11_FORMAT_SUPPORT_BLENDABLE));
    Out("chromium %s hr=0x%08lx %s", n, (unsigned long)hr, e);
  }
}

void Textures() {
  const DXGI_FORMAT fs[] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM,
                            DXGI_FORMAT_R16G16B16A16_FLOAT};
  struct M { UINT misc; const char *name; } ms[] = {
      {0, "plain"},
      {D3D11_RESOURCE_MISC_SHARED, "shared"},
      {D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX, "keyedmutex"},
      {D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED, "nthandle"},
      {D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX, "nthandle_keyedmutex"}};
  for (DXGI_FORMAT f : fs)
    for (const M &m : ms) {
      char before[96];
      snprintf(before, sizeof(before), "tex_%s_%s", FmtName(f), m.name);
      Fresh(before);
      ID3D11Device *d = g_d, *d2 = g_d2;
      ID3D11Device1 *d21 = nullptr;
      if (d2) d2->QueryInterface(IID_PPV_ARGS(&d21));
      D3D11_TEXTURE2D_DESC td = {256, 128, 1, 1, f, {1, 0}, D3D11_USAGE_DEFAULT,
                                 D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, m.misc};
      ID3D11Texture2D *t = nullptr;
      char n[96];
      snprintf(n, sizeof(n), "tex_%s_%s", FmtName(f), m.name);
      HRESULT hr = d->CreateTexture2D(&td, nullptr, &t);
      R(n, hr);
      if (FAILED(hr)) { Rel(d21); continue; }
      if (m.misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) {
        IDXGIResource1 *r1 = nullptr; HANDLE h = nullptr;
        hr = t->QueryInterface(IID_PPV_ARGS(&r1));
        if (SUCCEEDED(hr)) hr = r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h);
        snprintf(n, sizeof(n), "tex_%s_%s_create_nt_handle", FmtName(f), m.name);
        R(n, hr);
        if (SUCCEEDED(hr) && d21) {
          ID3D11Texture2D *o = nullptr;
          hr = d21->OpenSharedResource1(h, IID_PPV_ARGS(&o));
          snprintf(n, sizeof(n), "tex_%s_%s_open_nt_handle", FmtName(f), m.name);
          R(n, hr);
          Rel(o);
        }
        if (h) CloseHandle(h);
        Rel(r1);
      } else if (m.misc & (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)) {
        IDXGIResource *r = nullptr; HANDLE h = nullptr;
        hr = t->QueryInterface(IID_PPV_ARGS(&r));
        if (SUCCEEDED(hr)) hr = r->GetSharedHandle(&h);
        snprintf(n, sizeof(n), "tex_%s_%s_get_shared_handle", FmtName(f), m.name);
        R(n, hr);
        if (SUCCEEDED(hr) && d2) {
          ID3D11Texture2D *o = nullptr;
          hr = d2->OpenSharedResource(h, IID_PPV_ARGS(&o));
          snprintf(n, sizeof(n), "tex_%s_%s_open_shared", FmtName(f), m.name);
          R(n, hr);
          Rel(o);
        }
        Rel(r);
      }
      if (m.misc & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) {
        IDXGIKeyedMutex *km = nullptr;
        hr = t->QueryInterface(IID_PPV_ARGS(&km));
        if (SUCCEEDED(hr)) hr = km->AcquireSync(0, 1000);
        if (SUCCEEDED(hr)) hr = km->ReleaseSync(1);
        snprintf(n, sizeof(n), "tex_%s_%s_keyed_mutex", FmtName(f), m.name);
        R(n, hr);
        Rel(km);
      }
      ID3D11RenderTargetView *rtv = nullptr;
      hr = d->CreateRenderTargetView(t, nullptr, &rtv);
      snprintf(n, sizeof(n), "tex_%s_%s_rtv", FmtName(f), m.name);
      R(n, hr);
      Rel(rtv); Rel(t); Rel(d21);
    }
  Fresh("depth");
  ID3D11Device *d = g_d;
  // ANGLE's window surface: a depth-stencil buffer, an SRV on the swapchain-
  // shaped offscreen texture, and a 4x MSAA colour buffer it resolves.
  const DXGI_FORMAT ds[] = {DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D16_UNORM};
  for (DXGI_FORMAT f : ds) {
    D3D11_TEXTURE2D_DESC td = {256, 128, 1, 1, f, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0};
    ID3D11Texture2D *t = nullptr; ID3D11DepthStencilView *v = nullptr;
    HRESULT hr = d->CreateTexture2D(&td, nullptr, &t);
    if (SUCCEEDED(hr)) hr = d->CreateDepthStencilView(t, nullptr, &v);
    char n[64];
    snprintf(n, sizeof(n), "tex_depth_%u", (unsigned)f);
    R(n, hr);
    Rel(v); Rel(t);
  }
  for (UINT samples : {4u, 2u}) {
    UINT q = 0;
    HRESULT hr = d->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, samples, &q);
    char n[64], e[64];
    snprintf(n, sizeof(n), "msaa_%ux_quality", samples);
    snprintf(e, sizeof(e), "levels=%u", q);
    R(n, hr, e);
    if (FAILED(hr) || !q) continue;
    D3D11_TEXTURE2D_DESC td = {256, 128, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {samples, 0}, D3D11_USAGE_DEFAULT,
                               D3D11_BIND_RENDER_TARGET, 0, 0};
    ID3D11Texture2D *t = nullptr, *r = nullptr;
    hr = d->CreateTexture2D(&td, nullptr, &t);
    td.SampleDesc.Count = 1;
    if (SUCCEEDED(hr)) hr = d->CreateTexture2D(&td, nullptr, &r);
    ID3D11DeviceContext *c = nullptr;
    d->GetImmediateContext(&c);
    if (SUCCEEDED(hr)) c->ResolveSubresource(r, 0, t, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    snprintf(n, sizeof(n), "msaa_%ux_resolve", samples);
    R(n, hr);
    Rel(c); Rel(r); Rel(t);
  }
}

void Swap(DXGI_SWAP_EFFECT effect, DXGI_FORMAT fmt, UINT flags, const char *name) {
  Fresh(name);
  ID3D11Device *d = g_d; ID3D11DeviceContext *ctx = g_ctx; IDXGIFactory2 *f = g_f2;
  if (!d || !f) { R(name, E_POINTER, "no device"); return; }
  HWND w = SmallWindow(L"chromium-swap");
  DXGI_SWAP_CHAIN_DESC1 sd = {};
  sd.Width = 256; sd.Height = 128; sd.Format = fmt; sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
  sd.BufferCount = (effect == DXGI_SWAP_EFFECT_DISCARD || effect == DXGI_SWAP_EFFECT_SEQUENTIAL) ? 1 : 2;
  sd.SwapEffect = effect; sd.Scaling = DXGI_SCALING_STRETCH; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE; sd.Flags = flags;
  IDXGISwapChain1 *sc = nullptr;
  char n[128];
  HRESULT hr = f->CreateSwapChainForHwnd(d, w, &sd, nullptr, nullptr, &sc);
  snprintf(n, sizeof(n), "swap_%s_create", name);
  R(n, hr);
  if (SUCCEEDED(hr)) {
    if (flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
      IDXGISwapChain2 *s2 = nullptr;
      hr = sc->QueryInterface(IID_PPV_ARGS(&s2));
      if (SUCCEEDED(hr)) hr = s2->SetMaximumFrameLatency(1);
      HANDLE wait = SUCCEEDED(hr) ? s2->GetFrameLatencyWaitableObject() : nullptr;
      if (SUCCEEDED(hr) && !wait) hr = E_HANDLE;
      if (wait) { WaitForSingleObject(wait, 1000); CloseHandle(wait); }
      snprintf(n, sizeof(n), "swap_%s_waitable", name);
      R(n, hr);
      Rel(s2);
    }
    for (int frame = 0; frame < 3; ++frame) {
      ID3D11Texture2D *bb = nullptr; ID3D11RenderTargetView *rtv = nullptr;
      hr = sc->GetBuffer(0, IID_PPV_ARGS(&bb));
      if (SUCCEEDED(hr)) hr = d->CreateRenderTargetView(bb, nullptr, &rtv);
      if (SUCCEEDED(hr)) { const float c[4] = {0, 0.5f, 1, 1}; ctx->ClearRenderTargetView(rtv, c); }
      Rel(rtv); Rel(bb);
      if (FAILED(hr)) break;
      RECT dirty = {0, 0, 64, 64};
      DXGI_PRESENT_PARAMETERS pp = {1, &dirty, nullptr, nullptr};
      UINT pflags = (flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) ? DXGI_PRESENT_ALLOW_TEARING : 0;
      hr = frame == 0 ? sc->Present1(0, pflags, &pp) : sc->Present(0, pflags);
      Pump();
      if (FAILED(hr)) break;
    }
    snprintf(n, sizeof(n), "swap_%s_present", name);
    R(n, hr);
    hr = sc->ResizeBuffers(0, 300, 150, DXGI_FORMAT_UNKNOWN, flags);
    snprintf(n, sizeof(n), "swap_%s_resize", name);
    R(n, hr);
  }
  Rel(sc);
  DestroyWindow(w);
  Pump();
}

void DComp(DXGI_FORMAT fmt, UINT flags, const char *name) {
  Fresh(name);
  ID3D11Device *d = g_d; ID3D11DeviceContext *ctx = g_ctx; IDXGIFactory2 *f = g_f2;
  if (!d || !f) { R(name, E_POINTER, "no device"); return; }
  HWND w = SmallWindow(L"chromium-dcomp");
  IDXGIDevice *xd = nullptr; IDCompositionDevice *cd = nullptr; IDCompositionTarget *tg = nullptr;
  IDCompositionVisual *v = nullptr; IDXGISwapChain1 *sc = nullptr;
  char n[128];
  HRESULT hr = d->QueryInterface(IID_PPV_ARGS(&xd));
  if (SUCCEEDED(hr)) hr = DCompositionCreateDevice(xd, IID_PPV_ARGS(&cd));
  snprintf(n, sizeof(n), "dcomp_%s_device", name);
  R(n, hr);
  if (SUCCEEDED(hr)) hr = cd->CreateTargetForHwnd(w, TRUE, &tg);
  if (SUCCEEDED(hr)) hr = cd->CreateVisual(&v);
  snprintf(n, sizeof(n), "dcomp_%s_target_visual", name);
  R(n, hr);
  if (SUCCEEDED(hr)) {
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = 256; sd.Height = 128; sd.Format = fmt; sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; sd.Scaling = DXGI_SCALING_STRETCH;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; sd.Flags = flags;
    hr = f->CreateSwapChainForComposition(d, &sd, nullptr, &sc);
    snprintf(n, sizeof(n), "dcomp_%s_swapchain", name);
    R(n, hr);
  }
  if (sc) {
    hr = v->SetContent(sc);
    if (SUCCEEDED(hr)) hr = tg->SetRoot(v);
    if (SUCCEEDED(hr)) hr = cd->Commit();
    snprintf(n, sizeof(n), "dcomp_%s_commit", name);
    R(n, hr);
    for (int frame = 0; SUCCEEDED(hr) && frame < 3; ++frame) {
      ID3D11Texture2D *bb = nullptr; ID3D11RenderTargetView *rtv = nullptr;
      hr = sc->GetBuffer(0, IID_PPV_ARGS(&bb));
      if (SUCCEEDED(hr)) hr = d->CreateRenderTargetView(bb, nullptr, &rtv);
      if (SUCCEEDED(hr)) { const float c[4] = {1, 0.5f, 0, 1}; ctx->ClearRenderTargetView(rtv, c); }
      Rel(rtv); Rel(bb);
      if (SUCCEEDED(hr))
        hr = sc->Present(0, (flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) ? DXGI_PRESENT_ALLOW_TEARING : 0);
      Pump();
    }
    snprintf(n, sizeof(n), "dcomp_%s_present", name);
    R(n, hr);
  }
  Rel(sc); Rel(v); Rel(tg); Rel(cd); Rel(xd);
  DestroyWindow(w);
  Pump();
}

}  // namespace chromium

static void TestChromium() {
  using namespace chromium;
  g_fail = 0;
  D3D_FEATURE_LEVEL fl = (D3D_FEATURE_LEVEL)0, fl2 = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = MakeDevice(&g_d, &g_ctx, &fl);
  char e[160];
  snprintf(e, sizeof(e), "feature_level=0x%x", (unsigned)fl);
  R("device", hr, e);
  if (FAILED(hr)) return;
  R("device2", MakeDevice(&g_d2, &g_ctx2, &fl2));
  ID3D11Device *d = g_d;
  const IID ids[] = {__uuidof(ID3D11Device1), __uuidof(ID3D11Device2), __uuidof(ID3D11Device3), __uuidof(ID3D11Device5),
                     __uuidof(IDXGIDevice2), __uuidof(ID3D11Multithread), __uuidof(ID3D11VideoDevice)};
  const char *names[] = {"qi_device1", "qi_device2", "qi_device3", "qi_device5", "qi_dxgidevice2", "qi_multithread",
                         "video_device"};
  for (int i = 0; i < 7; ++i) {
    IUnknown *u = nullptr;
    HRESULT q = d->QueryInterface(ids[i], (void **)&u);
    if (i == 6) Out("chromium video_device hr=0x%08lx (informational)", (unsigned long)q);
    else R(names[i], q);
    Rel(u);
  }
  Features(d);
  IDXGIDevice *xd = nullptr; IDXGIAdapter *a = nullptr; IDXGIAdapter1 *a1 = nullptr;
  IDXGIFactory5 *f5 = nullptr;
  hr = d->QueryInterface(IID_PPV_ARGS(&xd));
  if (SUCCEEDED(hr)) hr = xd->GetAdapter(&a);
  if (SUCCEEDED(hr)) hr = a->QueryInterface(IID_PPV_ARGS(&a1));
  if (SUCCEEDED(hr)) hr = a->GetParent(IID_PPV_ARGS(&g_f2));
  R("factory2", hr);
  if (SUCCEEDED(hr)) {
    BOOL tearing = FALSE;
    HRESULT t = g_f2->QueryInterface(IID_PPV_ARGS(&f5));
    if (SUCCEEDED(t)) t = f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));
    snprintf(e, sizeof(e), "allow_tearing=%d", tearing);
    R("tearing", t, e);
    Outputs(a1);
    Formats(d);
    Rel(f5); Rel(a1); Rel(a); Rel(xd);
    Textures();
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    if (!session) Out("chromium swap result=SKIP reason=session0");
    else {
      const UINT tear = tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
      const UINT wait = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
      Swap(DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_FORMAT_B8G8R8A8_UNORM, 0, "flipdiscard_bgra8");
      Swap(DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_FORMAT_B8G8R8A8_UNORM, tear | wait, "flipdiscard_bgra8_tear_wait");
      Swap(DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_FORMAT_B8G8R8A8_UNORM, 0, "flipseq_bgra8");
      Swap(DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_FORMAT_R8G8B8A8_UNORM, wait, "flipseq_rgba8_wait");
      Swap(DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_FORMAT_R10G10B10A2_UNORM, 0, "flipdiscard_rgb10a2");
      Swap(DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, "flipdiscard_rgba16f");
      Swap(DXGI_SWAP_EFFECT_DISCARD, DXGI_FORMAT_B8G8R8A8_UNORM, 0, "discard_bgra8");
      Swap(DXGI_SWAP_EFFECT_SEQUENTIAL, DXGI_FORMAT_B8G8R8A8_UNORM, 0, "sequential_bgra8");
      DComp(DXGI_FORMAT_B8G8R8A8_UNORM, 0, "bgra8");
      DComp(DXGI_FORMAT_B8G8R8A8_UNORM, tear, "bgra8_tear");
      DComp(DXGI_FORMAT_R16G16B16A16_FLOAT, 0, "rgba16f");
    }
  } else { Rel(a1); Rel(a); Rel(xd); }
  Fresh("end");
  Out("chromium summary fail=%d", g_fail);
  Rel(g_f2); Rel(g_ctx2); Rel(g_d2); Rel(g_ctx); Rel(g_d);
}
