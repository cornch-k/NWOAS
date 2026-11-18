/* NWOAS gpucov: d3d11.dll proxy that runs an application's D3D11 devices on
 * WARP (Microsoft's software rasterizer) while the real GPU driver keeps the
 * display. Used only as an A/B: placed next to the application's .exe together
 * with d3d11_sys.dll (a copy of System32\d3d11.dll) by the K18 job, removed
 * afterwards. Every export except the two device-creation functions forwards
 * to d3d11_sys.dll (linker /export pragmas below). WARP is forced only when the process has
 * NWOAS_FORCE_WARP=1; otherwise the calls pass through unchanged. Each call is
 * logged to C:\NWOAS\gpucov\warp-proxy.log. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>

typedef HRESULT(WINAPI *PFN_CREATE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
                                    UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT(WINAPI *PFN_CREATE_SC)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
                                       UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **,
                                       D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

/* Export forwarding (generated from the System32 d3d11.dll export list). */
#pragma comment(linker, "/export:CreateDirect3D11DeviceFromDXGIDevice=d3d11_sys.CreateDirect3D11DeviceFromDXGIDevice")
#pragma comment(linker, "/export:CreateDirect3D11SurfaceFromDXGISurface=d3d11_sys.CreateDirect3D11SurfaceFromDXGISurface")
#pragma comment(linker, "/export:D3D11CoreCreateDevice=d3d11_sys.D3D11CoreCreateDevice")
#pragma comment(linker, "/export:D3D11CoreCreateLayeredDevice=d3d11_sys.D3D11CoreCreateLayeredDevice")
#pragma comment(linker, "/export:D3D11CoreGetLayeredDeviceSize=d3d11_sys.D3D11CoreGetLayeredDeviceSize")
#pragma comment(linker, "/export:D3D11CoreRegisterLayers=d3d11_sys.D3D11CoreRegisterLayers")
#pragma comment(linker, "/export:D3D11CreateDevice=Proxy_D3D11CreateDevice")
#pragma comment(linker, "/export:D3D11CreateDeviceAndSwapChain=Proxy_D3D11CreateDeviceAndSwapChain")
#pragma comment(linker, "/export:D3D11CreateDeviceForD3D12=d3d11_sys.D3D11CreateDeviceForD3D12")
#pragma comment(linker, "/export:D3D11On12CreateDevice=d3d11_sys.D3D11On12CreateDevice")
#pragma comment(linker, "/export:D3DKMTCloseAdapter=d3d11_sys.D3DKMTCloseAdapter")
#pragma comment(linker, "/export:D3DKMTCreateAllocation=d3d11_sys.D3DKMTCreateAllocation")
#pragma comment(linker, "/export:D3DKMTCreateContext=d3d11_sys.D3DKMTCreateContext")
#pragma comment(linker, "/export:D3DKMTCreateDevice=d3d11_sys.D3DKMTCreateDevice")
#pragma comment(linker, "/export:D3DKMTCreateSynchronizationObject=d3d11_sys.D3DKMTCreateSynchronizationObject")
#pragma comment(linker, "/export:D3DKMTDestroyAllocation=d3d11_sys.D3DKMTDestroyAllocation")
#pragma comment(linker, "/export:D3DKMTDestroyContext=d3d11_sys.D3DKMTDestroyContext")
#pragma comment(linker, "/export:D3DKMTDestroyDevice=d3d11_sys.D3DKMTDestroyDevice")
#pragma comment(linker, "/export:D3DKMTDestroySynchronizationObject=d3d11_sys.D3DKMTDestroySynchronizationObject")
#pragma comment(linker, "/export:D3DKMTEscape=d3d11_sys.D3DKMTEscape")
#pragma comment(linker, "/export:D3DKMTGetContextSchedulingPriority=d3d11_sys.D3DKMTGetContextSchedulingPriority")
#pragma comment(linker, "/export:D3DKMTGetDeviceState=d3d11_sys.D3DKMTGetDeviceState")
#pragma comment(linker, "/export:D3DKMTGetDisplayModeList=d3d11_sys.D3DKMTGetDisplayModeList")
#pragma comment(linker, "/export:D3DKMTGetMultisampleMethodList=d3d11_sys.D3DKMTGetMultisampleMethodList")
#pragma comment(linker, "/export:D3DKMTGetRuntimeData=d3d11_sys.D3DKMTGetRuntimeData")
#pragma comment(linker, "/export:D3DKMTGetSharedPrimaryHandle=d3d11_sys.D3DKMTGetSharedPrimaryHandle")
#pragma comment(linker, "/export:D3DKMTLock=d3d11_sys.D3DKMTLock")
#pragma comment(linker, "/export:D3DKMTOpenAdapterFromHdc=d3d11_sys.D3DKMTOpenAdapterFromHdc")
#pragma comment(linker, "/export:D3DKMTOpenResource=d3d11_sys.D3DKMTOpenResource")
#pragma comment(linker, "/export:D3DKMTPresent=d3d11_sys.D3DKMTPresent")
#pragma comment(linker, "/export:D3DKMTQueryAdapterInfo=d3d11_sys.D3DKMTQueryAdapterInfo")
#pragma comment(linker, "/export:D3DKMTQueryAllocationResidency=d3d11_sys.D3DKMTQueryAllocationResidency")
#pragma comment(linker, "/export:D3DKMTQueryResourceInfo=d3d11_sys.D3DKMTQueryResourceInfo")
#pragma comment(linker, "/export:D3DKMTRender=d3d11_sys.D3DKMTRender")
#pragma comment(linker, "/export:D3DKMTSetAllocationPriority=d3d11_sys.D3DKMTSetAllocationPriority")
#pragma comment(linker, "/export:D3DKMTSetContextSchedulingPriority=d3d11_sys.D3DKMTSetContextSchedulingPriority")
#pragma comment(linker, "/export:D3DKMTSetDisplayMode=d3d11_sys.D3DKMTSetDisplayMode")
#pragma comment(linker, "/export:D3DKMTSetDisplayPrivateDriverFormat=d3d11_sys.D3DKMTSetDisplayPrivateDriverFormat")
#pragma comment(linker, "/export:D3DKMTSetGammaRamp=d3d11_sys.D3DKMTSetGammaRamp")
#pragma comment(linker, "/export:D3DKMTSetVidPnSourceOwner=d3d11_sys.D3DKMTSetVidPnSourceOwner")
#pragma comment(linker, "/export:D3DKMTSignalSynchronizationObject=d3d11_sys.D3DKMTSignalSynchronizationObject")
#pragma comment(linker, "/export:D3DKMTUnlock=d3d11_sys.D3DKMTUnlock")
#pragma comment(linker, "/export:D3DKMTWaitForSynchronizationObject=d3d11_sys.D3DKMTWaitForSynchronizationObject")
#pragma comment(linker, "/export:D3DKMTWaitForVerticalBlankEvent=d3d11_sys.D3DKMTWaitForVerticalBlankEvent")
#pragma comment(linker, "/export:D3DPerformance_BeginEvent=d3d11_sys.D3DPerformance_BeginEvent")
#pragma comment(linker, "/export:D3DPerformance_EndEvent=d3d11_sys.D3DPerformance_EndEvent")
#pragma comment(linker, "/export:D3DPerformance_GetStatus=d3d11_sys.D3DPerformance_GetStatus")
#pragma comment(linker, "/export:D3DPerformance_SetMarker=d3d11_sys.D3DPerformance_SetMarker")
#pragma comment(linker, "/export:EnableFeatureLevelUpgrade=d3d11_sys.EnableFeatureLevelUpgrade")
#pragma comment(linker, "/export:OpenAdapter10=d3d11_sys.OpenAdapter10")
#pragma comment(linker, "/export:OpenAdapter10_2=d3d11_sys.OpenAdapter10_2")

static HMODULE real(void) {
  static HMODULE m;
  if (!m) m = LoadLibraryW(L"d3d11_sys.dll");
  return m;
}

static BOOL force(void) {
  char v[2] = {0};
  return GetEnvironmentVariableA("NWOAS_FORCE_WARP", v, sizeof(v)) == 1 && v[0] == '1';
}

static void note(const char *what, IDXGIAdapter *a, D3D_DRIVER_TYPE t, UINT flags, HRESULT hr, BOOL forced) {
  char line[256];
  HANDLE f = CreateFileA("C:\\NWOAS\\gpucov\\warp-proxy.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return;
  int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "WARPPROXY %s pid=%lu adapter=%p type=%d flags=0x%x forced=%d hr=0x%08lx\r\n",
                      what, GetCurrentProcessId(), (void *)a, (int)t, flags, forced, (unsigned long)hr);
  DWORD w = 0;
  if (n > 0) WriteFile(f, line, (DWORD)n, &w, NULL);
  CloseHandle(f);
}

HRESULT WINAPI Proxy_D3D11CreateDevice(IDXGIAdapter *a, D3D_DRIVER_TYPE t, HMODULE sw, UINT flags,
                                       const D3D_FEATURE_LEVEL *fl, UINT nfl, UINT sdk, ID3D11Device **dev,
                                       D3D_FEATURE_LEVEL *got, ID3D11DeviceContext **ctx) {
  PFN_CREATE fn = real() ? (PFN_CREATE)GetProcAddress(real(), "D3D11CreateDevice") : NULL;
  const BOOL forced = force() && t != D3D_DRIVER_TYPE_NULL && t != D3D_DRIVER_TYPE_REFERENCE;
  HRESULT hr = fn ? fn(forced ? NULL : a, forced ? D3D_DRIVER_TYPE_WARP : t, forced ? NULL : sw, flags, fl, nfl, sdk,
                       dev, got, ctx)
                  : E_FAIL;
  note("D3D11CreateDevice", a, t, flags, hr, forced);
  return hr;
}

HRESULT WINAPI Proxy_D3D11CreateDeviceAndSwapChain(IDXGIAdapter *a, D3D_DRIVER_TYPE t, HMODULE sw, UINT flags,
                                                   const D3D_FEATURE_LEVEL *fl, UINT nfl, UINT sdk,
                                                   const DXGI_SWAP_CHAIN_DESC *sd, IDXGISwapChain **sc,
                                                   ID3D11Device **dev, D3D_FEATURE_LEVEL *got,
                                                   ID3D11DeviceContext **ctx) {
  PFN_CREATE_SC fn = real() ? (PFN_CREATE_SC)GetProcAddress(real(), "D3D11CreateDeviceAndSwapChain") : NULL;
  const BOOL forced = force() && t != D3D_DRIVER_TYPE_NULL && t != D3D_DRIVER_TYPE_REFERENCE;
  HRESULT hr = fn ? fn(forced ? NULL : a, forced ? D3D_DRIVER_TYPE_WARP : t, forced ? NULL : sw, flags, fl, nfl, sdk, sd,
                       sc, dev, got, ctx)
                  : E_FAIL;
  note("D3D11CreateDeviceAndSwapChain", a, t, flags, hr, forced);
  return hr;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
  (void)h; (void)r;
  if (reason == DLL_PROCESS_ATTACH) note("loaded", NULL, (D3D_DRIVER_TYPE)0, 0, S_OK, force());
  return TRUE;
}
