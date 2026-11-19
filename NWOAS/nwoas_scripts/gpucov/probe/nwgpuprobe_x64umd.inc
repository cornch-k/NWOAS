// probe10 (2773, P37): which user-mode driver this process really runs on, and one D3D11 draw.
//   umd    : after a hardware D3D11 device exists, print which AppleAgx UMD images are mapped in this
//            process (forwarder / ARM64 / x64), whether Microsoft's software UMD (d3d10warp.dll) is
//            mapped, the machine of each image as the loader sees it in this process, and whether the
//            x64 VC runtime this UMD imports (vcruntime140.dll, msvcp140.dll absent) resolves here.
//   draw11 : one D3D11 draw (SV_VertexID full-screen triangle, constant-colour PS, 64x64 RGBA8)
//            on the hardware adapter, read back; PASS = every pixel the PS colour.
// The adapter alone is not proof: DXGI names the AGX adapter even when no AGX UMD loads.
static const char *ImageMachine(HMODULE m) {
  if (!m) return "none";
  const BYTE *b = (const BYTE *)m;
  const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)b;
  const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(b + dos->e_lfanew);
  switch (nt->FileHeader.Machine) {
  case 0x8664: return "x64";
  case 0xAA64: return "arm64";
  case 0xA641: return "arm64ec";
  case 0x014C: return "x86";
  default: return "other";
  }
}

static void ModuleLine(const char *tag, const WCHAR *name) {
  HMODULE m = GetModuleHandleW(name);
  WCHAR path[MAX_PATH] = L"";
  char a[MAX_PATH] = "";
  if (m && GetModuleFileNameW(m, path, MAX_PATH)) Narrow(path, a, sizeof(a));
  Out("umd module=%s loaded=%u machine=%s path=\"%s\"", tag, m ? 1u : 0u, ImageMachine(m), a);
}

static void TestUmdModules() {
  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  D3D_FEATURE_LEVEL got = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 &got, &ctx);
  char name[128] = "none";
  if (SUCCEEDED(hr)) {
    IDXGIDevice *xd = nullptr;
    IDXGIAdapter *ad = nullptr;
    if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&xd))) && SUCCEEDED(xd->GetAdapter(&ad))) {
      DXGI_ADAPTER_DESC d = {};
      ad->GetDesc(&d);
      Narrow(d.Description, name, sizeof(name));
    }
    Rel(ad); Rel(xd);
  }
  Out("umd d3d11 arch=%s adapter=\"%s\" hr=0x%08lx feature_level=0x%x", Arch(), name, (unsigned long)hr, (unsigned)got);
  ModuleLine("forwarder", L"AppleAgxRenderAdmissionUmd.dll");
  ModuleLine("arm64", L"AppleAgxRenderAdmissionUmdArm64.dll");
  ModuleLine("x64", L"AppleAgxRenderAdmissionUmdX64.dll");
  ModuleLine("warp", L"d3d10warp.dll");
  HMODULE agx = GetModuleHandleW(L"AppleAgxRenderAdmissionUmdX64.dll");
  if (!agx) agx = GetModuleHandleW(L"AppleAgxRenderAdmissionUmdArm64.dll");
  if (!agx) agx = GetModuleHandleW(L"AppleAgxRenderAdmissionUmd.dll");
  HMODULE warp = GetModuleHandleW(L"d3d10warp.dll");
  const char *expected = !strcmp(Arch(), "x64") ? "x64" : "arm64";
  bool own = agx && !strcmp(ImageMachine(agx), expected);
  Out("umd summary arch=%s agx_umd=%s warp=%u result=%s", Arch(), agx ? ImageMachine(agx) : "none", warp ? 1u : 0u,
      (SUCCEEDED(hr) && own && strstr(name, "Apple AGX")) ? "PASS" : "FAIL");
  // The x64 UMD imports VCRUNTIME140.dll, VCRUNTIME140_1.dll and MSVCP140.dll (dumpbin, 2773). An x64
  // process can map them only if System32 has x64-compatible (ARM64X) copies: Microsoft documents that
  // the X64 Redistributable "contains both ARM64 and X64 binaries"; the J274 machine had only
  // vc_redist.arm64.exe (I4). crt_x64_ready=0 means the x64 UMD cannot load in x64 processes.
  static const WCHAR *crt[] = {L"vcruntime140.dll", L"vcruntime140_1.dll", L"msvcp140.dll"};
  static const char *crtName[] = {"vcruntime140", "vcruntime140_1", "msvcp140"};
  UINT ready = 1u;
  for (int i = 0; i < 3; ++i) {
    HMODULE vc = LoadLibraryW(crt[i]);
    DWORD err = vc ? 0ul : GetLastError();
    Out("umd crt %s loaded=%u machine=%s err=%lu", crtName[i], vc ? 1u : 0u, ImageMachine(vc), err);
    if (!vc) ready = 0u;
    if (vc) FreeLibrary(vc);
  }
  Out("umd crt_x64_ready=%u arch=%s", ready, Arch());
  if (dev) Out("umd d3d11 removed_reason=0x%08lx", (unsigned long)dev->GetDeviceRemovedReason());
  Rel(ctx); Rel(dev);
  // D3D12 on the hardware adapter in this process (x64 D3D12 apps; upstream EXP1795 got 0x887A0004 without a UMD)
  IDXGIFactory1 *f = nullptr;
  IDXGIAdapter1 *a = nullptr;
  ID3D12Device *d12 = nullptr;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) a = PickHardwareAdapter(f);
  hr = a ? D3D12CreateDevice(a, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d12)) : E_FAIL;
  Out("umd d3d12 arch=%s hr=0x%08lx x64_umd_loaded=%u", Arch(), (unsigned long)hr,
      GetModuleHandleW(L"AppleAgxRenderAdmissionUmdX64.dll") ? 1u : 0u);
  Rel(d12); Rel(a); Rel(f);
}

static const char *kDrawVs =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 p = float2((id << 1) & 2, id & 2);\n"
    "  return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }\n";
static const char *kDrawPs = "float4 main() : SV_Target { return float4(1.0, 0.5, 0.25, 1.0); }\n";

static void TestDraw11() {
  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  D3D_FEATURE_LEVEL got = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 &got, &ctx);
  if (FAILED(hr)) { Out("draw11 result=FAIL stage=device hr=0x%08lx arch=%s", (unsigned long)hr, Arch()); return; }
  const UINT W = 64, H = 64;
  ID3DBlob *vsb = CompileCs(kDrawVs, "vs_4_0"), *psb = CompileCs(kDrawPs, "ps_4_0");
  ID3D11VertexShader *vs = nullptr;
  ID3D11PixelShader *ps = nullptr;
  ID3D11Texture2D *rt = nullptr, *st = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  hr = (vsb && psb) ? dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs) : E_FAIL;
  if (SUCCEEDED(hr)) hr = dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
  D3D11_TEXTURE2D_DESC td = {};
  td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (SUCCEEDED(hr)) hr = dev->CreateTexture2D(&td, nullptr, &rt);
  if (SUCCEEDED(hr)) hr = dev->CreateRenderTargetView(rt, nullptr, &rtv);
  td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (SUCCEEDED(hr)) hr = dev->CreateTexture2D(&td, nullptr, &st);
  if (SUCCEEDED(hr)) {
    const float black[4] = {0, 0, 0, 1};
    ctx->ClearRenderTargetView(rtv, black);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->Draw(3, 0);
    ctx->CopyResource(st, rt);
    D3D11_MAPPED_SUBRESOURCE m = {};
    hr = ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
    if (SUCCEEDED(hr)) {
      UINT bad = 0;
      const BYTE *c0 = (const BYTE *)m.pData;
      for (UINT y = 0; y < H; ++y)
        for (UINT x = 0; x < W; ++x) {
          const BYTE *c = (const BYTE *)m.pData + y * m.RowPitch + x * 4;
          if (c[0] != 255 || abs(c[1] - 128) > 1 || abs(c[2] - 64) > 1 || c[3] != 255) ++bad;
        }
      Out("draw11 result=%s arch=%s first=%02x%02x%02x%02x bad_pixels=%u of=%u x64_umd_loaded=%u", bad ? "FAIL" : "PASS",
          Arch(), c0[0], c0[1], c0[2], c0[3], bad, W * H, GetModuleHandleW(L"AppleAgxRenderAdmissionUmdX64.dll") ? 1u : 0u);
      ctx->Unmap(st, 0);
    } else Out("draw11 result=FAIL stage=map hr=0x%08lx arch=%s", (unsigned long)hr, Arch());
  } else Out("draw11 result=FAIL stage=create hr=0x%08lx arch=%s", (unsigned long)hr, Arch());
  Out("draw11 end removed_reason=0x%08lx", (unsigned long)dev->GetDeviceRemovedReason());
  Rel(st); Rel(rtv); Rel(rt); Rel(ps); Rel(vs); Rel(psb); Rel(vsb); Rel(ctx); Rel(dev);
}
