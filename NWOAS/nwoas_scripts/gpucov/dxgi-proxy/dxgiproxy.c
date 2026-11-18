/* NWOAS gpucov: dxgi.dll proxy that records every swapchain an application
 * creates and how often each one presents. Placed next to the application's
 * .exe together with dxgi_sys.dll (a copy of System32\dxgi.dll) by the K21 job
 * and removed afterwards. All exports forward to dxgi_sys.dll; the three
 * CreateDXGIFactory functions also patch the factory and swapchain vtables
 * (all objects of those classes share them, including factories D3D creates
 * internally). Lines go to C:\NWOAS\gpucov\dxgi-proxy.log:
 *   DXGIPROXY swapchain sc=<ptr> api=<CreateSwapChain|ForHwnd|ForComposition>
 *     device=<d3d11|d3d12|other>:<ptr> adapter="<name>" hwnd=<h> class="<c>"
 *     parent=<h> rect=<x,y,w,h> size=<w>x<h> format=<n> buffers=<n>
 *     effect=<n> flags=0x<f> alpha=<n> scaling=<n> hr=0x<hr>
 *   DXGIPROXY present sc=<ptr> n=<count> hwnd=<h> size=<w>x<h> (1st, 2nd,
 *     10th, 100th, then every 500th present per swapchain) */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <initguid.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <d3d12.h>
#include <stdio.h>

#pragma comment(linker, "/export:ApplyCompatResolutionQuirking=dxgi_sys.ApplyCompatResolutionQuirking")
#pragma comment(linker, "/export:CompatString=dxgi_sys.CompatString")
#pragma comment(linker, "/export:CompatValue=dxgi_sys.CompatValue")
#pragma comment(linker, "/export:CreateDXGIFactory=Proxy_CreateDXGIFactory")
#pragma comment(linker, "/export:CreateDXGIFactory1=Proxy_CreateDXGIFactory1")
#pragma comment(linker, "/export:CreateDXGIFactory2=Proxy_CreateDXGIFactory2")
#pragma comment(linker, "/export:DXGID3D10CreateDevice=dxgi_sys.DXGID3D10CreateDevice")
#pragma comment(linker, "/export:DXGID3D10CreateLayeredDevice=dxgi_sys.DXGID3D10CreateLayeredDevice")
#pragma comment(linker, "/export:DXGID3D10GetLayeredDeviceSize=dxgi_sys.DXGID3D10GetLayeredDeviceSize")
#pragma comment(linker, "/export:DXGID3D10RegisterLayers=dxgi_sys.DXGID3D10RegisterLayers")
#pragma comment(linker, "/export:DXGIDeclareAdapterRemovalSupport=dxgi_sys.DXGIDeclareAdapterRemovalSupport")
#pragma comment(linker, "/export:DXGIDisableVBlankVirtualization=dxgi_sys.DXGIDisableVBlankVirtualization")
#pragma comment(linker, "/export:DXGIDumpJournal=dxgi_sys.DXGIDumpJournal")
#pragma comment(linker, "/export:DXGIGetDebugInterface1=dxgi_sys.DXGIGetDebugInterface1")
#pragma comment(linker, "/export:DXGIReportAdapterConfiguration=dxgi_sys.DXGIReportAdapterConfiguration")
#pragma comment(linker, "/export:PIXBeginCapture=dxgi_sys.PIXBeginCapture")
#pragma comment(linker, "/export:PIXEndCapture=dxgi_sys.PIXEndCapture")
#pragma comment(linker, "/export:PIXGetCaptureState=dxgi_sys.PIXGetCaptureState")
#pragma comment(linker, "/export:SetAppCompatStringPointer=dxgi_sys.SetAppCompatStringPointer")
#pragma comment(linker, "/export:UpdateHMDEmulationStatus=dxgi_sys.UpdateHMDEmulationStatus")

static CRITICAL_SECTION lock;

static void out(const char *fmt, ...) {
  char line[768];
  va_list ap;
  va_start(ap, fmt);
  int n = _vsnprintf_s(line, sizeof(line) - 2, _TRUNCATE, fmt, ap);
  va_end(ap);
  if (n < 0) n = (int)strlen(line);
  line[n++] = '\r'; line[n++] = '\n';
  HANDLE f = CreateFileA("C:\\NWOAS\\gpucov\\dxgi-proxy.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(f, line, (DWORD)n, &w, NULL);
  CloseHandle(f);
}

static HMODULE real(void) {
  static HMODULE m;
  if (!m) m = LoadLibraryW(L"dxgi_sys.dll");
  return m;
}

/* ---------- swapchains ---------- */
typedef HRESULT(STDMETHODCALLTYPE *PFN_PRESENT)(IDXGISwapChain *, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *PFN_PRESENT1)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
struct ScVt { void *vt; PFN_PRESENT present; PFN_PRESENT1 present1; };
static struct ScVt scvts[8];
struct Sc { void *sc; HWND hwnd; UINT w, h; LONG presents; };
static struct Sc scs[128];

static struct ScVt *vtOf(void *obj) {
  void *vt = *(void **)obj;
  for (int i = 0; i < 8; ++i) if (scvts[i].vt == vt) return &scvts[i];
  return NULL;
}

static void counted(void *sc) {
  struct Sc *s = NULL;
  EnterCriticalSection(&lock);
  for (int i = 0; i < 128; ++i) if (scs[i].sc == sc) { s = &scs[i]; break; }
  LeaveCriticalSection(&lock);
  if (!s) return;
  LONG n = InterlockedIncrement(&s->presents);
  if (n == 1 || n == 2 || n == 10 || n == 100 || n % 500 == 0)
    out("DXGIPROXY present sc=%p n=%ld hwnd=%p size=%ux%u", sc, n, (void *)s->hwnd, s->w, s->h);
}

static HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain *This, UINT sync, UINT flags) {
  struct ScVt *v = vtOf(This);
  counted(This);
  return v && v->present ? v->present(This, sync, flags) : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1 *This, UINT sync, UINT flags,
                                              const DXGI_PRESENT_PARAMETERS *p) {
  struct ScVt *v = vtOf(This);
  counted(This);
  return v && v->present1 ? v->present1(This, sync, flags, p) : E_FAIL;
}

static void patch(void **slot, void *value) {
  DWORD old;
  if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
    *slot = value;
    VirtualProtect(slot, sizeof(void *), old, &old);
  }
}

static void hookSwapChain(IDXGISwapChain *sc, HWND hwnd, UINT w, UINT h) {
  IDXGISwapChain1 *sc1 = NULL;
  EnterCriticalSection(&lock);
  for (int i = 0; i < 128; ++i)
    if (!scs[i].sc || scs[i].sc == sc) { scs[i].sc = sc; scs[i].hwnd = hwnd; scs[i].w = w; scs[i].h = h; scs[i].presents = 0; break; }
  if (!vtOf(sc)) {
    for (int i = 0; i < 8; ++i)
      if (!scvts[i].vt) {
        IDXGISwapChainVtbl *vt = sc->lpVtbl;
        scvts[i].vt = vt;
        scvts[i].present = (PFN_PRESENT)vt->Present;
        patch((void **)&vt->Present, (void *)HookPresent);
        if (SUCCEEDED(IDXGISwapChain_QueryInterface(sc, &IID_IDXGISwapChain1, (void **)&sc1)) && sc1) {
          IDXGISwapChain1Vtbl *vt1 = sc1->lpVtbl;
          if ((void *)vt1 == (void *)vt) {
            scvts[i].present1 = (PFN_PRESENT1)vt1->Present1;
            patch((void **)&vt1->Present1, (void *)HookPresent1);
          }
          IDXGISwapChain1_Release(sc1);
        }
        break;
      }
  }
  LeaveCriticalSection(&lock);
}

static void describeDevice(IUnknown *dev, char *buf, size_t n) {
  ID3D12CommandQueue *q = NULL;
  ID3D11Device *d11 = NULL;
  char name[128] = "?";
  const char *kind = "other";
  if (dev && SUCCEEDED(IUnknown_QueryInterface(dev, &IID_ID3D12CommandQueue, (void **)&q)) && q) {
    kind = "d3d12";
    ID3D12CommandQueue_Release(q);
  } else if (dev && SUCCEEDED(IUnknown_QueryInterface(dev, &IID_ID3D11Device, (void **)&d11)) && d11) {
    IDXGIDevice *xd = NULL;
    IDXGIAdapter *a = NULL;
    kind = "d3d11";
    if (SUCCEEDED(ID3D11Device_QueryInterface(d11, &IID_IDXGIDevice, (void **)&xd)) && xd) {
      if (SUCCEEDED(IDXGIDevice_GetAdapter(xd, &a)) && a) {
        DXGI_ADAPTER_DESC d;
        if (SUCCEEDED(IDXGIAdapter_GetDesc(a, &d))) {
          int i = 0;
          for (; d.Description[i] && i < 127; ++i) name[i] = d.Description[i] < 128 ? (char)d.Description[i] : '?';
          name[i] = 0;
        }
        IDXGIAdapter_Release(a);
      }
      IDXGIDevice_Release(xd);
    }
    ID3D11Device_Release(d11);
  }
  _snprintf_s(buf, n, _TRUNCATE, "device=%s:%p adapter=\"%s\"", kind, (void *)dev, name);
}

static void logSwapChain(const char *api, IUnknown *dev, HWND hwnd, UINT w, UINT h, UINT fmt, UINT buffers, UINT effect,
                         UINT flags, UINT alpha, UINT scaling, HRESULT hr, void *sc) {
  char devs[200], cls[64] = "", rect[64] = "-";
  RECT r;
  describeDevice(dev, devs, sizeof(devs));
  if (hwnd) {
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (GetWindowRect(hwnd, &r)) _snprintf_s(rect, sizeof(rect), _TRUNCATE, "%ld,%ld,%ldx%ld", r.left, r.top, r.right - r.left, r.bottom - r.top);
  }
  out("DXGIPROXY swapchain sc=%p api=%s %s hwnd=%p class=\"%s\" parent=%p rect=%s size=%ux%u format=%u buffers=%u effect=%u flags=0x%x alpha=%u scaling=%u hr=0x%08lx",
      sc, api, devs, (void *)hwnd, cls, hwnd ? (void *)GetParent(hwnd) : NULL, rect, w, h, fmt, buffers, effect, flags, alpha,
      scaling, (unsigned long)hr);
}

/* ---------- factories ---------- */
typedef HRESULT(STDMETHODCALLTYPE *PFN_CSC)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CSCH)(IDXGIFactory2 *, IUnknown *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CSCC)(IDXGIFactory2 *, IUnknown *, const DXGI_SWAP_CHAIN_DESC1 *, IDXGIOutput *,
                                             IDXGISwapChain1 **);
struct FVt { void *vt; PFN_CSC csc; PFN_CSCH csch; PFN_CSCC cscc; };
static struct FVt fvts[8];

static struct FVt *fvtOf(void *obj) {
  void *vt = *(void **)obj;
  for (int i = 0; i < 8; ++i) if (fvts[i].vt == vt) return &fvts[i];
  return NULL;
}

static HRESULT STDMETHODCALLTYPE HookCSC(IDXGIFactory *This, IUnknown *dev, DXGI_SWAP_CHAIN_DESC *d, IDXGISwapChain **sc) {
  struct FVt *v = fvtOf(This);
  HRESULT hr = v && v->csc ? v->csc(This, dev, d, sc) : E_FAIL;
  if (d) logSwapChain("CreateSwapChain", dev, d->OutputWindow, d->BufferDesc.Width, d->BufferDesc.Height,
                      d->BufferDesc.Format, d->BufferCount, d->SwapEffect, d->Flags, 0, 0, hr, sc ? *sc : NULL);
  if (SUCCEEDED(hr) && sc && *sc) hookSwapChain(*sc, d ? d->OutputWindow : NULL, d ? d->BufferDesc.Width : 0, d ? d->BufferDesc.Height : 0);
  return hr;
}

static HRESULT STDMETHODCALLTYPE HookCSCH(IDXGIFactory2 *This, IUnknown *dev, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1 *d,
                                          const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fs, IDXGIOutput *o, IDXGISwapChain1 **sc) {
  struct FVt *v = fvtOf(This);
  HRESULT hr = v && v->csch ? v->csch(This, dev, hwnd, d, fs, o, sc) : E_FAIL;
  if (d) logSwapChain("ForHwnd", dev, hwnd, d->Width, d->Height, d->Format, d->BufferCount, d->SwapEffect, d->Flags,
                      d->AlphaMode, d->Scaling, hr, sc ? *sc : NULL);
  if (SUCCEEDED(hr) && sc && *sc) hookSwapChain((IDXGISwapChain *)*sc, hwnd, d ? d->Width : 0, d ? d->Height : 0);
  return hr;
}

static HRESULT STDMETHODCALLTYPE HookCSCC(IDXGIFactory2 *This, IUnknown *dev, const DXGI_SWAP_CHAIN_DESC1 *d,
                                          IDXGIOutput *o, IDXGISwapChain1 **sc) {
  struct FVt *v = fvtOf(This);
  HRESULT hr = v && v->cscc ? v->cscc(This, dev, d, o, sc) : E_FAIL;
  if (d) logSwapChain("ForComposition", dev, NULL, d->Width, d->Height, d->Format, d->BufferCount, d->SwapEffect,
                      d->Flags, d->AlphaMode, d->Scaling, hr, sc ? *sc : NULL);
  if (SUCCEEDED(hr) && sc && *sc) hookSwapChain((IDXGISwapChain *)*sc, NULL, d ? d->Width : 0, d ? d->Height : 0);
  return hr;
}

static void hookFactory(void *obj) {
  IDXGIFactory2 *f2 = NULL;
  if (!obj) return;
  EnterCriticalSection(&lock);
  if (!fvtOf(obj)) {
    for (int i = 0; i < 8; ++i)
      if (!fvts[i].vt) {
        IDXGIFactoryVtbl *vt = ((IDXGIFactory *)obj)->lpVtbl;
        fvts[i].vt = vt;
        fvts[i].csc = (PFN_CSC)vt->CreateSwapChain;
        patch((void **)&vt->CreateSwapChain, (void *)HookCSC);
        if (SUCCEEDED(IUnknown_QueryInterface((IUnknown *)obj, &IID_IDXGIFactory2, (void **)&f2)) && f2) {
          IDXGIFactory2Vtbl *vt2 = f2->lpVtbl;
          if ((void *)vt2 == (void *)vt) {
            fvts[i].csch = (PFN_CSCH)vt2->CreateSwapChainForHwnd;
            fvts[i].cscc = (PFN_CSCC)vt2->CreateSwapChainForComposition;
            patch((void **)&vt2->CreateSwapChainForHwnd, (void *)HookCSCH);
            patch((void **)&vt2->CreateSwapChainForComposition, (void *)HookCSCC);
          }
          IDXGIFactory2_Release(f2);
        }
        out("DXGIPROXY factory vtable=%p hooked slot=%d", (void *)vt, i);
        break;
      }
  }
  LeaveCriticalSection(&lock);
}

typedef HRESULT(WINAPI *PFN_CF)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_CF2)(UINT, REFIID, void **);

HRESULT WINAPI Proxy_CreateDXGIFactory(REFIID riid, void **f) {
  PFN_CF fn = real() ? (PFN_CF)GetProcAddress(real(), "CreateDXGIFactory") : NULL;
  HRESULT hr = fn ? fn(riid, f) : E_FAIL;
  if (SUCCEEDED(hr) && f) hookFactory(*f);
  return hr;
}

HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void **f) {
  PFN_CF fn = real() ? (PFN_CF)GetProcAddress(real(), "CreateDXGIFactory1") : NULL;
  HRESULT hr = fn ? fn(riid, f) : E_FAIL;
  if (SUCCEEDED(hr) && f) hookFactory(*f);
  return hr;
}

HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT flags, REFIID riid, void **f) {
  PFN_CF2 fn = real() ? (PFN_CF2)GetProcAddress(real(), "CreateDXGIFactory2") : NULL;
  HRESULT hr = fn ? fn(flags, riid, f) : E_FAIL;
  if (SUCCEEDED(hr) && f) hookFactory(*f);
  return hr;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
  (void)h; (void)r;
  if (reason == DLL_PROCESS_ATTACH) {
    InitializeCriticalSection(&lock);
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    out("DXGIPROXY loaded pid=%lu exe=\"%s\"", GetCurrentProcessId(), exe);
  }
  return TRUE;
}
