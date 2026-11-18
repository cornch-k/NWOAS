// nwgpuprobe present variants (included before TestPresent): a D3D12 swapchain
// on a child window, and a D3D12 composition swapchain shown through a
// DirectComposition visual. A UI toolkit may put widget strips in child
// windows or composition visuals; the main-window present test covers neither.
#include <dcomp.h>
#pragma comment(lib, "dcomp.lib")

static void Present12Variant(HWND parent, bool dcomp) {
  const char *name = dcomp ? "present12_dcomp" : "present12_child";
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  HWND target = parent;
  if (!dcomp) {
    target = CreateWindowExW(0, L"NwGpuProbe", L"child", WS_CHILD | WS_VISIBLE, 300, 120, 200, 150, parent, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    Pump();
  }
  IDXGIFactory4 *f = nullptr;
  IDXGISwapChain1 *sc1 = nullptr;
  IDXGISwapChain3 *sc = nullptr;
  ID3D12DescriptorHeap *rtvh = nullptr;
  IDCompositionDevice *cd = nullptr;
  IDCompositionTarget *ct = nullptr;
  IDCompositionVisual *cv = nullptr;
  ID3D12Resource *bufs[2] = {};
  HRESULT hr = target ? CreateDXGIFactory2(0, IID_PPV_ARGS(&f)) : E_FAIL;
  DXGI_SWAP_CHAIN_DESC1 d = {};
  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  d.BufferCount = 2; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  if (dcomp) { d.Width = 200; d.Height = 150; d.Scaling = DXGI_SCALING_STRETCH; d.AlphaMode = DXGI_ALPHA_MODE_IGNORE; }
  if (SUCCEEDED(hr)) hr = dcomp ? f->CreateSwapChainForComposition(s.q, &d, nullptr, &sc1)
                                : f->CreateSwapChainForHwnd(s.q, target, &d, nullptr, nullptr, &sc1);
  if (SUCCEEDED(hr)) hr = sc1->QueryInterface(IID_PPV_ARGS(&sc));
  if (SUCCEEDED(hr) && dcomp) {
    hr = DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&cd));
    if (SUCCEEDED(hr)) hr = cd->CreateTargetForHwnd(parent, TRUE, &ct);
    if (SUCCEEDED(hr)) hr = cd->CreateVisual(&cv);
    if (SUCCEEDED(hr)) hr = cv->SetContent(sc);
    if (SUCCEEDED(hr)) hr = cv->SetOffsetX(300.0f);
    if (SUCCEEDED(hr)) hr = cv->SetOffsetY(120.0f);
    if (SUCCEEDED(hr)) hr = ct->SetRoot(cv);
    if (SUCCEEDED(hr)) hr = cd->Commit();
  }
  D3D12_DESCRIPTOR_HEAP_DESC dh = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2};
  if (SUCCEEDED(hr)) hr = s.dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&rtvh));
  Out("%s swapchain hr=0x%08lx", name, (unsigned long)hr);
  if (SUCCEEDED(hr)) {
    const UINT inc = s.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (UINT i = 0; i < 2 && SUCCEEDED(hr); ++i) {
      hr = sc->GetBuffer(i, IID_PPV_ARGS(&bufs[i]));
      D3D12_CPU_DESCRIPTOR_HANDLE h = rtvh->GetCPUDescriptorHandleForHeapStart();
      h.ptr += i * inc;
      if (SUCCEEDED(hr)) s.dev->CreateRenderTargetView(bufs[i], nullptr, h);
    }
    // yellow for the child window, orange for the composition visual
    const float c[4] = {1.0f, dcomp ? 0.5f : 1.0f, 0.0f, 1.0f};
    UINT presented = 0;
    HRESULT ph = hr;
    for (UINT i = 0; i < 60 && SUCCEEDED(hr); ++i) {
      UINT bi = sc->GetCurrentBackBufferIndex();
      D3D12_CPU_DESCRIPTOR_HANDLE h = rtvh->GetCPUDescriptorHandleForHeapStart();
      h.ptr += bi * inc;
      s.ca->Reset();
      s.cl->Reset(s.ca, nullptr);
      Barrier(s.cl, bufs[bi], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
      s.cl->ClearRenderTargetView(h, c, 0, nullptr);
      Barrier(s.cl, bufs[bi], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
      hr = s.cl->Close();
      if (FAILED(hr)) break;
      ID3D12CommandList *lists[] = {s.cl};
      s.q->ExecuteCommandLists(1, lists);
      ph = sc->Present(1, 0);
      if (FAILED(ph) || !D12Wait(s, name)) { if (SUCCEEDED(ph)) ph = E_FAIL; break; }
      ++presented;
      Pump();
    }
    Sleep(300);
    Pump();
    POINT p = {400, 195};  // centre of the 200x150 area at (300,120) in the parent's client area
    ClientToScreen(parent, &p);
    HDC dc = GetDC(nullptr);
    COLORREF px = GetPixel(dc, p.x, p.y);
    ReleaseDC(nullptr, dc);
    const int wantG = dcomp ? 128 : 255;
    const bool ok = GetRValue(px) > 200 && abs((int)GetGValue(px) - wantG) < 40 && GetBValue(px) < 60;
    Out("%s frames=%u present_hr=0x%08lx screen_rgb=%02x%02x%02x screen_check=%s", name, presented, (unsigned long)ph,
        GetRValue(px), GetGValue(px), GetBValue(px), ok ? "PASS" : "FAIL");
  }
  Rel(bufs[0]); Rel(bufs[1]); Rel(rtvh); Rel(cv); Rel(ct); Rel(cd); Rel(sc); Rel(sc1); Rel(f);
  if (!dcomp && target) DestroyWindow(target);
  D12Close(s);
}
