// nwgpuprobe "interop" tests (user session; included after nwgpuprobe_comp.inc).
// Why: on 2759 Cinebench shows its license text but not its buttons, while
// every D3D12 draw/present path probed so far passes. These are the other
// ways a UI toolkit gets pixels on screen:
//   d2d_d3d11_offscreen : Direct2D rectangle + DirectWrite text into a D3D11
//                         BGRA texture (BGRA_SUPPORT device), read back
//   d2d_d3d11_window    : the same through ID2D1DeviceContext on a swapchain
//   d2d_on_d3d11on12    : D2D/DWrite into a D3D12 texture wrapped by D3D11On12
//   gdi_getdc_swapchain : IDXGISurface1::GetDC on a GDI-compatible swapchain
//   shared_12_to_11     : D3D12 NT-handle texture opened by D3D11.1
//   shared_11_to_12     : D3D11 NT-handle texture opened by D3D12
//   shared_keyed_mutex  : two D3D11 devices, keyed-mutex texture
//   layered_gdi / layered_d3d11 : WS_EX_LAYERED + UpdateLayeredWindow from a
//                         DIB filled by GDI, and from a D3D11 render read back
#include <d2d1_1.h>
#include <dwrite.h>
#include <d3d11on12.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

static ID3D11Device *Dev11(ID3D11DeviceContext **ctx, UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT) {
  ID3D11Device *d = nullptr;
  static const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fl, 2, D3D11_SDK_VERSION, &d, nullptr, ctx);
  if (FAILED(hr)) Out("interop d3d11 device flags=0x%x hr=0x%08lx", flags, (unsigned long)hr);
  return d;
}

// Draws "NWOAS 123" in black on a white background and a solid rectangle of `rect`
// colour at (10,10)-(60,60) into the D2D target.
static HRESULT D2dScene(ID2D1RenderTarget *rt, D2D1_COLOR_F rect) {
  IDWriteFactory *dw = nullptr;
  IDWriteTextFormat *tf = nullptr;
  ID2D1SolidColorBrush *br = nullptr, *ink = nullptr;
  HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&dw);
  if (SUCCEEDED(hr)) hr = dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                               DWRITE_FONT_STRETCH_NORMAL, 36.0f, L"en-us", &tf);
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(rect, &br);
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &ink);
  if (SUCCEEDED(hr)) {
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(1, 1, 1, 1));
    rt->FillRectangle(D2D1::RectF(10, 10, 60, 60), br);
    rt->DrawText(L"NWOAS 123", 9, tf, D2D1::RectF(70, 10, 250, 70), ink);
    hr = rt->EndDraw();
  }
  Rel(ink); Rel(br); Rel(tf); Rel(dw);
  return hr;
}

// Counts rect-colour pixels inside the rectangle and dark pixels in the text box of a
// 256x128 BGRA image (row pitch `pitch`).
static void D2dCheck(const char *name, const BYTE *p, UINT pitch, BYTE r, BYTE g, BYTE b) {
  UINT rectOk = 0, ink = 0;
  for (UINT y = 15; y < 55; ++y)
    for (UINT x = 15; x < 55; ++x) {
      const BYTE *c = p + y * pitch + x * 4;  // BGRA
      if (abs(c[2] - r) < 8 && abs(c[1] - g) < 8 && abs(c[0] - b) < 8) ++rectOk;
    }
  for (UINT y = 10; y < 70; ++y)
    for (UINT x = 70; x < 250; ++x) {
      const BYTE *c = p + y * pitch + x * 4;
      if (c[0] < 80 && c[1] < 80 && c[2] < 80) ++ink;
    }
  Out("interop %s result=%s rect_pixels=%u/1600 text_ink_pixels=%u", name, rectOk > 1500 && ink > 300 ? "PASS" : "FAIL",
      rectOk, ink);
}

static ID3D11Texture2D *Tex11(ID3D11Device *d, UINT w, UINT h, UINT bind, UINT misc, D3D11_USAGE usage, UINT cpu,
                              DXGI_FORMAT fmt = DXGI_FORMAT_B8G8R8A8_UNORM) {
  D3D11_TEXTURE2D_DESC td = {};
  td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt; td.SampleDesc.Count = 1;
  td.Usage = usage; td.BindFlags = bind; td.CPUAccessFlags = cpu; td.MiscFlags = misc;
  ID3D11Texture2D *t = nullptr;
  HRESULT hr = d->CreateTexture2D(&td, nullptr, &t);
  if (FAILED(hr)) Out("interop texture11 bind=0x%x misc=0x%x hr=0x%08lx", bind, misc, (unsigned long)hr);
  return t;
}

static void D2dOffscreen() {
  ID3D11DeviceContext *ctx = nullptr;
  ID3D11Device *d = Dev11(&ctx);
  ID3D11Texture2D *t = d ? Tex11(d, 256, 128, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, D3D11_USAGE_DEFAULT, 0) : nullptr;
  ID3D11Texture2D *st = d ? Tex11(d, 256, 128, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ) : nullptr;
  IDXGISurface *surf = nullptr;
  ID2D1Factory *f = nullptr;
  ID2D1RenderTarget *rt = nullptr;
  HRESULT hr = t && st ? t->QueryInterface(IID_PPV_ARGS(&surf)) : E_FAIL;
  if (SUCCEEDED(hr)) hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f);
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  if (SUCCEEDED(hr)) hr = f->CreateDxgiSurfaceRenderTarget(surf, &props, &rt);
  if (SUCCEEDED(hr)) hr = D2dScene(rt, D2D1::ColorF(1, 0, 0, 1));
  if (SUCCEEDED(hr)) {
    ctx->CopyResource(st, t);
    D3D11_MAPPED_SUBRESOURCE m = {};
    hr = ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
    if (SUCCEEDED(hr)) { D2dCheck("d2d_d3d11_offscreen", (const BYTE *)m.pData, m.RowPitch, 255, 0, 0); ctx->Unmap(st, 0); }
  }
  if (FAILED(hr)) Out("interop d2d_d3d11_offscreen result=FAIL hr=0x%08lx", (unsigned long)hr);
  Rel(rt); Rel(f); Rel(surf); Rel(st); Rel(t); Rel(ctx); Rel(d);
}

// Samples the composed screen: rect colour at the rectangle centre, dark pixels in the text box.
static void ScreenCheck(const char *name, HWND w, BYTE r, BYTE g, BYTE b) {
  // One BitBlt of the window area (CAPTUREBLT includes layered windows) instead
  // of thousands of GetPixel calls, each of which is a screen read under DWM.
  Sleep(400);
  Pump();
  POINT o = {0, 0};
  ClientToScreen(w, &o);
  BITMAPINFO bi = {};
  bi.bmiHeader = {sizeof(BITMAPINFOHEADER), 256, -128, 1, 32, BI_RGB};
  void *bits = nullptr;
  HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
  HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, dib);
  BOOL ok = dib && BitBlt(mem, 0, 0, 256, 128, screen, o.x, o.y, SRCCOPY | CAPTUREBLT);
  GdiFlush();
  UINT ink = 0;
  BYTE cr = 0, cg = 0, cb = 0;
  if (ok) {
    const BYTE *p = (const BYTE *)bits;  // BGRA rows of 1024 bytes
    const BYTE *c = p + 35 * 1024 + 35 * 4;
    cb = c[0]; cg = c[1]; cr = c[2];
    for (int y = 10; y < 70; y += 2)
      for (int x = 70; x < 250; x += 2) {
        const BYTE *k = p + y * 1024 + x * 4;
        if (k[0] < 80 && k[1] < 80 && k[2] < 80) ++ink;
      }
  }
  SelectObject(mem, old);
  if (dib) DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  const bool rectOk = ok && abs(cr - r) < 20 && abs(cg - g) < 20 && abs(cb - b) < 20;
  Out("interop %s result=%s screen_rect=%02x%02x%02x text_ink_samples=%u", name, rectOk && ink > 60 ? "PASS" : "FAIL",
      cr, cg, cb, ink);
}

static void EnsureClass() {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"NwGpuProbe";
  RegisterClassW(&wc);  // fails harmlessly when already registered
}

static HWND SmallWindow(const WCHAR *title, DWORD ex = WS_EX_TOPMOST, DWORD style = WS_POPUP | WS_VISIBLE) {
  EnsureClass();
  HWND w = CreateWindowExW(ex, L"NwGpuProbe", title, style, 120, 120, 256, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  ShowWindow(w, SW_SHOWNOACTIVATE);
  Pump();
  return w;
}

static void D2dWindow() {
  ID3D11DeviceContext *ctx = nullptr;
  ID3D11Device *d = Dev11(&ctx);
  HWND w = SmallWindow(L"d2d");
  IDXGIDevice *xd = nullptr; IDXGIAdapter *a = nullptr; IDXGIFactory2 *xf = nullptr; IDXGISwapChain1 *sc = nullptr;
  IDXGISurface *surf = nullptr; ID2D1Factory1 *f = nullptr; ID2D1Device *dd = nullptr; ID2D1DeviceContext *dc = nullptr;
  ID2D1Bitmap1 *bmp = nullptr;
  HRESULT hr = d && w ? d->QueryInterface(IID_PPV_ARGS(&xd)) : E_FAIL;
  if (SUCCEEDED(hr)) hr = xd->GetAdapter(&a);
  if (SUCCEEDED(hr)) hr = a->GetParent(IID_PPV_ARGS(&xf));
  DXGI_SWAP_CHAIN_DESC1 sd = {};
  sd.Width = 256; sd.Height = 128; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  if (SUCCEEDED(hr)) hr = xf->CreateSwapChainForHwnd(d, w, &sd, nullptr, nullptr, &sc);
  if (SUCCEEDED(hr)) hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr, (void **)&f);
  if (SUCCEEDED(hr)) hr = f->CreateDevice(xd, &dd);
  if (SUCCEEDED(hr)) hr = dd->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
  for (int frame = 0; frame < 3 && SUCCEEDED(hr); ++frame) {
    hr = sc->GetBuffer(0, IID_PPV_ARGS(&surf));
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    if (SUCCEEDED(hr)) hr = dc->CreateBitmapFromDxgiSurface(surf, &bp, &bmp);
    if (SUCCEEDED(hr)) { dc->SetTarget(bmp); hr = D2dScene(dc, D2D1::ColorF(0, 1, 0, 1)); dc->SetTarget(nullptr); }
    Rel(bmp); Rel(surf);
    if (SUCCEEDED(hr)) hr = sc->Present(1, 0);
    Pump();
  }
  if (SUCCEEDED(hr)) ScreenCheck("d2d_d3d11_window", w, 0, 255, 0);
  else Out("interop d2d_d3d11_window result=FAIL hr=0x%08lx", (unsigned long)hr);
  Rel(dc); Rel(dd); Rel(f); Rel(sc); Rel(xf); Rel(a); Rel(xd); Rel(ctx); Rel(d);
  if (w) DestroyWindow(w);
  Pump();
}

static void D2dOn11On12() {
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  ID3D11Device *d11 = nullptr; ID3D11DeviceContext *c11 = nullptr; ID3D11On12Device *on12 = nullptr;
  ID3D12Resource *t12 = nullptr, *rb = nullptr; ID3D11Resource *w11 = nullptr; IDXGISurface *surf = nullptr;
  ID2D1Factory *f = nullptr; ID2D1RenderTarget *rt = nullptr;
  IUnknown *queues[] = {s.q};
  HRESULT hr = D3D11On12CreateDevice(s.dev, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, queues, 1, 0, &d11, &c11, nullptr);
  if (SUCCEEDED(hr)) hr = d11->QueryInterface(IID_PPV_ARGS(&on12));
  D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = 256; rd.Height = 128; rd.DepthOrArraySize = 1;
  rd.MipLevels = 1; rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (SUCCEEDED(hr)) hr = s.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&t12));
  if (SUCCEEDED(hr)) hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, 256 * 128 * 4, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, &rb);
  D3D11_RESOURCE_FLAGS rf = {D3D11_BIND_RENDER_TARGET};
  if (SUCCEEDED(hr)) hr = on12->CreateWrappedResource(t12, &rf, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE, IID_PPV_ARGS(&w11));
  if (SUCCEEDED(hr)) hr = w11->QueryInterface(IID_PPV_ARGS(&surf));
  if (SUCCEEDED(hr)) hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f);
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  if (SUCCEEDED(hr)) hr = f->CreateDxgiSurfaceRenderTarget(surf, &props, &rt);
  if (SUCCEEDED(hr)) {
    ID3D11Resource *list[] = {w11};
    on12->AcquireWrappedResources(list, 1);
    hr = D2dScene(rt, D2D1::ColorF(0, 0, 1, 1));
    on12->ReleaseWrappedResources(list, 1);
    c11->Flush();
  }
  if (SUCCEEDED(hr)) {
    s.ca->Reset();
    s.cl->Reset(s.ca, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint = {DXGI_FORMAT_B8G8R8A8_UNORM, 256, 128, 1, 256 * 4};
    src.pResource = t12; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    s.cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Barrier(s.cl, t12, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    hr = s.cl->Close();
    ID3D12CommandList *lists[] = {s.cl};
    if (SUCCEEDED(hr)) s.q->ExecuteCommandLists(1, lists);
    if (SUCCEEDED(hr) && !D12Wait(s, "d2d_on_d3d11on12")) hr = E_FAIL;
    void *m = nullptr;
    D3D12_RANGE r = {0, 256 * 128 * 4};
    if (SUCCEEDED(hr) && SUCCEEDED(rb->Map(0, &r, &m))) {
      D2dCheck("d2d_on_d3d11on12", (const BYTE *)m, 256 * 4, 0, 0, 255);
      D3D12_RANGE none = {0, 0};
      rb->Unmap(0, &none);
    }
  }
  if (FAILED(hr)) Out("interop d2d_on_d3d11on12 result=FAIL hr=0x%08lx", (unsigned long)hr);
  Rel(rt); Rel(f); Rel(surf); Rel(w11); Rel(rb); Rel(t12); Rel(on12); Rel(c11); Rel(d11);
  D12Close(s);
}

static void GdiGetDc() {
  ID3D11DeviceContext *ctx = nullptr;
  ID3D11Device *d = Dev11(&ctx);
  HWND w = SmallWindow(L"gdi");
  IDXGIDevice *xd = nullptr; IDXGIAdapter *a = nullptr; IDXGIFactory2 *xf = nullptr; IDXGISwapChain1 *sc = nullptr;
  HRESULT hr = d && w ? d->QueryInterface(IID_PPV_ARGS(&xd)) : E_FAIL;
  if (SUCCEEDED(hr)) hr = xd->GetAdapter(&a);
  if (SUCCEEDED(hr)) hr = a->GetParent(IID_PPV_ARGS(&xf));
  DXGI_SWAP_CHAIN_DESC1 sd = {};
  sd.Width = 256; sd.Height = 128; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
  if (SUCCEEDED(hr)) hr = xf->CreateSwapChainForHwnd(d, w, &sd, nullptr, nullptr, &sc);
  Out("interop gdi_getdc_swapchain swapchain hr=0x%08lx", (unsigned long)hr);
  for (int frame = 0; frame < 3 && SUCCEEDED(hr); ++frame) {
    IDXGISurface1 *s1 = nullptr;
    HDC dc = nullptr;
    hr = sc->GetBuffer(0, IID_PPV_ARGS(&s1));
    if (SUCCEEDED(hr)) hr = s1->GetDC(FALSE, &dc);
    if (SUCCEEDED(hr)) {
      RECT all = {0, 0, 256, 128}, box = {10, 10, 60, 60};
      FillRect(dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
      HBRUSH blue = CreateSolidBrush(RGB(0, 0, 255));
      FillRect(dc, &box, blue);
      DeleteObject(blue);
      HFONT font = CreateFontW(40, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
      HGDIOBJ old = SelectObject(dc, font);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(0, 0, 0));
      TextOutW(dc, 70, 15, L"NWOAS 123", 9);
      SelectObject(dc, old);
      DeleteObject(font);
      s1->ReleaseDC(nullptr);
    } else Out("interop gdi_getdc_swapchain getdc hr=0x%08lx", (unsigned long)hr);
    Rel(s1);
    if (SUCCEEDED(hr)) hr = sc->Present(1, 0);
    Pump();
  }
  if (SUCCEEDED(hr)) ScreenCheck("gdi_getdc_swapchain", w, 0, 0, 255);
  else Out("interop gdi_getdc_swapchain result=FAIL hr=0x%08lx", (unsigned long)hr);
  Rel(sc); Rel(xf); Rel(a); Rel(xd); Rel(ctx); Rel(d);
  if (w) DestroyWindow(w);
  Pump();
}

// Reads a 64x64 RGBA8 D3D11 texture through a staging copy and checks one colour.
static void Check11(const char *name, ID3D11Device *d, ID3D11DeviceContext *ctx, ID3D11Texture2D *t, BYTE r, BYTE g, BYTE b) {
  ID3D11Texture2D *st = Tex11(d, 64, 64, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ, DXGI_FORMAT_R8G8B8A8_UNORM);
  if (!st) { Out("interop %s result=FAIL stage=staging", name); return; }
  ctx->CopyResource(st, t);
  D3D11_MAPPED_SUBRESOURCE m = {};
  HRESULT hr = ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
  if (SUCCEEDED(hr)) {
    const BYTE *c = (const BYTE *)m.pData + 32 * m.RowPitch + 32 * 4;
    const bool ok = abs(c[0] - r) < 4 && abs(c[1] - g) < 4 && abs(c[2] - b) < 4;
    Out("interop %s result=%s centre=%02x%02x%02x%02x", name, ok ? "PASS" : "FAIL", c[0], c[1], c[2], c[3]);
    ctx->Unmap(st, 0);
  } else Out("interop %s result=FAIL stage=map hr=0x%08lx", name, (unsigned long)hr);
  Rel(st);
}

static void Shared12To11() {
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  ID3D11DeviceContext *ctx = nullptr;
  ID3D11Device *d = Dev11(&ctx, 0);
  ID3D11Device1 *d1 = nullptr;
  ID3D12Resource *t = nullptr;
  ID3D12DescriptorHeap *rtv = nullptr;
  ID3D11Texture2D *t11 = nullptr;
  HANDLE h = nullptr;
  HRESULT hr = d ? d->QueryInterface(IID_PPV_ARGS(&d1)) : E_FAIL;
  D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = 64; rd.Height = 64; rd.DepthOrArraySize = 1;
  rd.MipLevels = 1; rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (SUCCEEDED(hr)) hr = s.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&t));
  if (SUCCEEDED(hr)) hr = s.dev->CreateSharedHandle(t, nullptr, GENERIC_ALL, nullptr, &h);
  D3D12_DESCRIPTOR_HEAP_DESC dh = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
  if (SUCCEEDED(hr)) hr = s.dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&rtv));
  if (SUCCEEDED(hr)) {
    s.dev->CreateRenderTargetView(t, nullptr, rtv->GetCPUDescriptorHandleForHeapStart());
    s.ca->Reset();
    s.cl->Reset(s.ca, nullptr);
    const float c[4] = {1.0f, 0.5f, 0.0f, 1.0f};
    s.cl->ClearRenderTargetView(rtv->GetCPUDescriptorHandleForHeapStart(), c, 0, nullptr);
    Barrier(s.cl, t, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
    hr = s.cl->Close();
    ID3D12CommandList *lists[] = {s.cl};
    if (SUCCEEDED(hr)) s.q->ExecuteCommandLists(1, lists);
    if (SUCCEEDED(hr) && !D12Wait(s, "shared_12_to_11")) hr = E_FAIL;
  }
  if (SUCCEEDED(hr)) hr = d1->OpenSharedResource1(h, IID_PPV_ARGS(&t11));
  if (SUCCEEDED(hr)) Check11("shared_12_to_11", d, ctx, t11, 255, 128, 0);
  else Out("interop shared_12_to_11 result=FAIL hr=0x%08lx", (unsigned long)hr);
  if (h) CloseHandle(h);
  Rel(t11); Rel(rtv); Rel(t); Rel(d1); Rel(ctx); Rel(d);
  D12Close(s);
}

static void Shared11To12() {
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  ID3D11DeviceContext *ctx = nullptr;
  ID3D11Device *d = Dev11(&ctx, 0);
  ID3D11Texture2D *t11 = d ? Tex11(d, 64, 64, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                                   D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE, D3D11_USAGE_DEFAULT, 0,
                                   DXGI_FORMAT_R8G8B8A8_UNORM) : nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  IDXGIResource1 *xr = nullptr;
  HANDLE h = nullptr;
  ID3D12Resource *t = nullptr, *rb = nullptr;
  HRESULT hr = t11 ? d->CreateRenderTargetView(t11, nullptr, &rtv) : E_FAIL;
  if (SUCCEEDED(hr)) {
    const float c[4] = {0.0f, 0.5f, 1.0f, 1.0f};
    ctx->ClearRenderTargetView(rtv, c);
    ctx->Flush();
    hr = t11->QueryInterface(IID_PPV_ARGS(&xr));
  }
  if (SUCCEEDED(hr)) hr = xr->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h);
  if (SUCCEEDED(hr)) {
    ID3D11Query *q = nullptr;
    D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT};
    if (SUCCEEDED(d->CreateQuery(&qd, &q))) {  // the D3D11 clear must be finished before D3D12 reads
      ctx->End(q);
      BOOL done = FALSE;
      for (int i = 0; i < 500 && ctx->GetData(q, &done, sizeof(done), 0) != S_OK; ++i) Sleep(2);
      Rel(q);
    }
    hr = s.dev->OpenSharedHandle(h, IID_PPV_ARGS(&t));
  }
  if (SUCCEEDED(hr)) hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, 64 * 64 * 4, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, &rb);
  if (SUCCEEDED(hr)) {
    s.ca->Reset();
    s.cl->Reset(s.ca, nullptr);
    Barrier(s.cl, t, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, 256};
    src.pResource = t; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    s.cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Barrier(s.cl, t, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    hr = s.cl->Close();
    ID3D12CommandList *lists[] = {s.cl};
    if (SUCCEEDED(hr)) s.q->ExecuteCommandLists(1, lists);
    if (SUCCEEDED(hr) && !D12Wait(s, "shared_11_to_12")) hr = E_FAIL;
    BYTE *m = nullptr;
    D3D12_RANGE r = {0, 64 * 64 * 4};
    if (SUCCEEDED(hr) && SUCCEEDED(rb->Map(0, &r, (void **)&m))) {
      const BYTE *c = m + 32 * 256 + 32 * 4;
      const bool ok = c[0] < 4 && abs(c[1] - 128) < 4 && c[2] > 251;
      Out("interop shared_11_to_12 result=%s centre=%02x%02x%02x%02x", ok ? "PASS" : "FAIL", c[0], c[1], c[2], c[3]);
      D3D12_RANGE none = {0, 0};
      rb->Unmap(0, &none);
    }
  }
  if (FAILED(hr)) Out("interop shared_11_to_12 result=FAIL hr=0x%08lx", (unsigned long)hr);
  if (h) CloseHandle(h);
  Rel(rb); Rel(t); Rel(xr); Rel(rtv); Rel(t11); Rel(ctx); Rel(d);
  D12Close(s);
}

static void SharedKeyedMutex() {
  ID3D11DeviceContext *ca = nullptr, *cb = nullptr;
  ID3D11Device *da = Dev11(&ca, 0), *db = Dev11(&cb, 0);
  ID3D11Device1 *db1 = nullptr;
  ID3D11Texture2D *ta = da ? Tex11(da, 64, 64, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                                   D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE,
                                   D3D11_USAGE_DEFAULT, 0, DXGI_FORMAT_R8G8B8A8_UNORM) : nullptr;
  ID3D11Texture2D *tb = nullptr;
  IDXGIKeyedMutex *ka = nullptr, *kb = nullptr;
  IDXGIResource1 *xr = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  HANDLE h = nullptr;
  HRESULT hr = ta && db ? ta->QueryInterface(IID_PPV_ARGS(&ka)) : E_FAIL;
  if (SUCCEEDED(hr)) hr = ka->AcquireSync(0, 2000);
  if (SUCCEEDED(hr)) hr = da->CreateRenderTargetView(ta, nullptr, &rtv);
  if (SUCCEEDED(hr)) {
    const float c[4] = {0.5f, 0.0f, 1.0f, 1.0f};
    ca->ClearRenderTargetView(rtv, c);
    hr = ka->ReleaseSync(1);
  }
  if (SUCCEEDED(hr)) hr = ta->QueryInterface(IID_PPV_ARGS(&xr));
  if (SUCCEEDED(hr)) hr = xr->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h);
  if (SUCCEEDED(hr)) hr = db->QueryInterface(IID_PPV_ARGS(&db1));
  if (SUCCEEDED(hr)) hr = db1->OpenSharedResource1(h, IID_PPV_ARGS(&tb));
  if (SUCCEEDED(hr)) hr = tb->QueryInterface(IID_PPV_ARGS(&kb));
  if (SUCCEEDED(hr)) hr = kb->AcquireSync(1, 2000);
  if (SUCCEEDED(hr)) {
    Check11("shared_keyed_mutex", db, cb, tb, 128, 0, 255);
    kb->ReleaseSync(0);
  } else Out("interop shared_keyed_mutex result=FAIL hr=0x%08lx", (unsigned long)hr);
  if (h) CloseHandle(h);
  Rel(kb); Rel(tb); Rel(db1); Rel(xr); Rel(rtv); Rel(ka); Rel(ta); Rel(cb); Rel(db); Rel(ca); Rel(da);
}

// UpdateLayeredWindow from a 32-bpp DIB, filled either by GDI or with pixels a D3D11 render produced.
static void Layered(bool fromD3d) {
  const char *name = fromD3d ? "layered_d3d11" : "layered_gdi";
  HWND w = SmallWindow(L"layered", WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
  BITMAPINFO bi = {};
  bi.bmiHeader = {sizeof(BITMAPINFOHEADER), 256, -128, 1, 32, BI_RGB};
  void *bits = nullptr;
  HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
  HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, dib);
  HRESULT hr = w && dib ? S_OK : E_FAIL;
  Out("interop %s step=window hwnd=%p dib=%p", name, (void *)w, (void *)dib);
  BYTE r = 255, g = 0, b = 255;
  if (SUCCEEDED(hr) && !fromD3d) {
    BYTE *p = (BYTE *)bits;
    for (UINT i = 0; i < 256 * 128; ++i) { p[i * 4] = 255; p[i * 4 + 1] = 255; p[i * 4 + 2] = 255; p[i * 4 + 3] = 255; }
    for (UINT y = 10; y < 60; ++y)
      for (UINT x = 10; x < 60; ++x) { BYTE *c = p + (y * 256 + x) * 4; c[0] = b; c[1] = g; c[2] = r; }
    HFONT font = CreateFontW(40, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, L"Segoe UI");
    HGDIOBJ of = SelectObject(mem, font);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(0, 0, 0));
    TextOutW(mem, 70, 15, L"NWOAS 123", 9);
    SelectObject(mem, of);
    DeleteObject(font);
    GdiFlush();
    for (UINT i = 0; i < 256 * 128; ++i) p[i * 4 + 3] = 255;  // GDI clears alpha
  } else if (SUCCEEDED(hr)) {
    r = 0; g = 255; b = 255;
    ID3D11DeviceContext *ctx = nullptr;
    ID3D11Device *d = Dev11(&ctx);
    ID3D11Texture2D *t = d ? Tex11(d, 256, 128, D3D11_BIND_RENDER_TARGET, 0, D3D11_USAGE_DEFAULT, 0) : nullptr;
    ID3D11Texture2D *st = d ? Tex11(d, 256, 128, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ) : nullptr;
    IDXGISurface *surf = nullptr;
    ID2D1Factory *f = nullptr;
    ID2D1RenderTarget *rt = nullptr;
    hr = t && st ? t->QueryInterface(IID_PPV_ARGS(&surf)) : E_FAIL;
    if (SUCCEEDED(hr)) hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (SUCCEEDED(hr)) hr = f->CreateDxgiSurfaceRenderTarget(surf, &props, &rt);
    if (SUCCEEDED(hr)) hr = D2dScene(rt, D2D1::ColorF(0, 1, 1, 1));
    if (SUCCEEDED(hr)) {
      ctx->CopyResource(st, t);
      D3D11_MAPPED_SUBRESOURCE m = {};
      hr = ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
      if (SUCCEEDED(hr)) {
        for (UINT y = 0; y < 128; ++y) memcpy((BYTE *)bits + y * 1024, (const BYTE *)m.pData + y * m.RowPitch, 1024);
        ctx->Unmap(st, 0);
      }
    }
    Rel(rt); Rel(f); Rel(surf); Rel(st); Rel(t); Rel(ctx); Rel(d);
  }
  if (SUCCEEDED(hr)) {
    POINT dst = {120, 120}, src = {0, 0};
    SIZE size = {256, 128};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(w, screen, &dst, &size, mem, &src, 0, &bf, ULW_ALPHA)) hr = HRESULT_FROM_WIN32(GetLastError());
  }
  Out("interop %s step=updated hr=0x%08lx", name, (unsigned long)hr);
  if (SUCCEEDED(hr)) ScreenCheck(name, w, r, g, b);
  else Out("interop %s result=FAIL hr=0x%08lx", name, (unsigned long)hr);
  Out("interop %s step=checked", name);
  SelectObject(mem, old);
  if (dib) DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  if (w) DestroyWindow(w);
  Pump();
}

static void TestInterop() {
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  if (session == 0) { Out("interop result=SKIP reason=session0_needs_user_session"); return; }
  EnsureClass();
  D2dOffscreen();
  D2dWindow();
  D2dOn11On12();
  GdiGetDc();
  Shared12To11();
  Shared11To12();
  SharedKeyedMutex();
  Layered(false);
  Layered(true);
}
