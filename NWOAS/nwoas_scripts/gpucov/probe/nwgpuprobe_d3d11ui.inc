// nwgpuprobe "d3d11ui" tests (included after nwgpuprobe_interop.inc).
// Why: on 2762 Cinebench's own D3D11 UI draws reach 1920x1009 and 866x547
// targets but stay invisible, while the same app on WARP shows everything.
// Its shaders are vs_4_0/ps_4_0 (TGSI path) and its swapchains are legacy
// blt-model ones (CreateSwapChain, one buffer, created small and resized).
// Every test here uses SM 4.0 shaders through the D3D11 frontend, draws into
// a 64x64 B8G8R8A8 target and reads it back; "warp" runs the same on WARP.
//   cull_*            : CullMode BACK with clockwise / counter-clockwise
//                       triangles and FrontCounterClockwise
//   dynamic_vb_nooverwrite : one DYNAMIC vertex buffer, Map DISCARD then
//                       NO_OVERWRITE appends, one draw per append (UI batching)
//   dynamic_cb_discard: a DYNAMIC constant buffer re-mapped with DISCARD before
//                       each of 8 draws
//   blend_premultiplied, scissor, texture_bgra_linear,
//   update_subresource_texture, offscreen_composite (render to texture, then
//                       draw it as a textured quad)
//   blt_swapchain     : legacy CreateSwapChain (DISCARD, 1 buffer) on two
//                       windows, ResizeBuffers from 120x61, drawn quad, screen
//                       check (user session only)
//   blt_noresize      : the same without ResizeBuffers (120x61 stretched)
//   flip_swapchain    : CreateSwapChainForHwnd FLIP_DISCARD, 2 buffers
// "modeswitch" runs the three at the current mode, at another listed mode
// (ChangeDisplaySettingsEx) and after restoring the original mode.

namespace d3d11ui {

const char *kVs =
    "cbuffer C : register(b0) { float4 color; float4 rect; };\n"
    "struct I { float2 pos : POSITION; float4 col : COLOR; };\n"
    "struct O { float4 p : SV_Position; float4 c : COLOR0; float2 uv : TEXCOORD0; };\n"
    "O main(I i) { O o; o.p = float4(i.pos, 0, 1); o.c = i.col; o.uv = i.pos * float2(0.5, -0.5) + 0.5; return o; }\n";
const char *kVsRect =
    "cbuffer C : register(b0) { float4 color; float4 rect; };\n"
    "struct O { float4 p : SV_Position; float4 c : COLOR0; float2 uv : TEXCOORD0; };\n"
    "O main(uint id : SV_VertexID) { O o; float2 t = float2((id == 1 || id == 2 || id == 4) ? 1 : 0, (id == 2 || id == 4 || id == 5) ? 1 : 0);\n"
    "  float2 p = float2(lerp(rect.x, rect.z, t.x), lerp(rect.w, rect.y, t.y)); o.p = float4(p, 0, 1); o.c = color; o.uv = t; return o; }\n";
const char *kPsColor =
    "float4 main(float4 p : SV_Position, float4 c : COLOR0, float2 uv : TEXCOORD0) : SV_Target { return c; }\n";
const char *kPsTex =
    "Texture2D t0 : register(t0); SamplerState s0 : register(s0);\n"
    "float4 main(float4 p : SV_Position, float4 c : COLOR0, float2 uv : TEXCOORD0) : SV_Target { return t0.Sample(s0, uv); }\n";

struct Vtx { float x, y, r, g, b, a; };

struct Ctx {
  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  ID3D11VertexShader *vs = nullptr, *vs_rect = nullptr;
  ID3D11PixelShader *ps = nullptr, *ps_tex = nullptr;
  ID3D11InputLayout *il = nullptr;
  ID3D11Buffer *cb = nullptr;
  ID3D11Texture2D *rt = nullptr, *st = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  ID3D11SamplerState *linear = nullptr;
};

bool Compile(const char *src, const char *target, ID3DBlob **out) {
  ID3DBlob *err = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), "nwgp", nullptr, nullptr, "main", target, 0, 0, out, &err);
  if (FAILED(hr)) Out("d3d11ui compile target=%s hr=0x%08lx msg=\"%.160s\"", target, (unsigned long)hr, err ? (const char *)err->GetBufferPointer() : "");
  Rel(err);
  return SUCCEEDED(hr);
}

bool Open(Ctx &c) {
  static const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  HRESULT hr = D3D11CreateDevice(nullptr, g_warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, fl, 2, D3D11_SDK_VERSION, &c.dev, nullptr, &c.ctx);
  char name[128] = "none";
  if (SUCCEEDED(hr)) {
    IDXGIDevice *xd = nullptr; IDXGIAdapter *a = nullptr;
    if (SUCCEEDED(c.dev->QueryInterface(IID_PPV_ARGS(&xd))) && SUCCEEDED(xd->GetAdapter(&a))) {
      DXGI_ADAPTER_DESC d = {}; a->GetDesc(&d); Narrow(d.Description, name, sizeof(name));
    }
    Rel(a); Rel(xd);
  }
  Out("d3d11ui device adapter=\"%s\" hr=0x%08lx", name, (unsigned long)hr);
  if (FAILED(hr)) return false;
  ID3DBlob *b1 = nullptr, *b2 = nullptr, *b3 = nullptr, *b4 = nullptr;
  bool ok = Compile(kVs, "vs_4_0", &b1) && Compile(kVsRect, "vs_4_0", &b2) && Compile(kPsColor, "ps_4_0", &b3) && Compile(kPsTex, "ps_4_0", &b4);
  if (ok) hr = c.dev->CreateVertexShader(b1->GetBufferPointer(), b1->GetBufferSize(), nullptr, &c.vs);
  if (ok && SUCCEEDED(hr)) hr = c.dev->CreateVertexShader(b2->GetBufferPointer(), b2->GetBufferSize(), nullptr, &c.vs_rect);
  if (ok && SUCCEEDED(hr)) hr = c.dev->CreatePixelShader(b3->GetBufferPointer(), b3->GetBufferSize(), nullptr, &c.ps);
  if (ok && SUCCEEDED(hr)) hr = c.dev->CreatePixelShader(b4->GetBufferPointer(), b4->GetBufferSize(), nullptr, &c.ps_tex);
  const D3D11_INPUT_ELEMENT_DESC il[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                         {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
  if (ok && SUCCEEDED(hr)) hr = c.dev->CreateInputLayout(il, 2, b1->GetBufferPointer(), b1->GetBufferSize(), &c.il);
  D3D11_BUFFER_DESC bd = {32, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
  if (ok && SUCCEEDED(hr)) hr = c.dev->CreateBuffer(&bd, nullptr, &c.cb);
  D3D11_TEXTURE2D_DESC td = {64, 64, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
  if (ok && SUCCEEDED(hr)) hr = c.dev->CreateTexture2D(&td, nullptr, &c.rt);
  if (ok && SUCCEEDED(hr)) hr = c.dev->CreateRenderTargetView(c.rt, nullptr, &c.rtv);
  td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (ok && SUCCEEDED(hr)) hr = c.dev->CreateTexture2D(&td, nullptr, &c.st);
  D3D11_SAMPLER_DESC sd = {};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  if (ok && SUCCEEDED(hr)) hr = c.dev->CreateSamplerState(&sd, &c.linear);
  Rel(b1); Rel(b2); Rel(b3); Rel(b4);
  if (!ok || FAILED(hr)) Out("d3d11ui setup hr=0x%08lx", (unsigned long)hr);
  return ok && SUCCEEDED(hr);
}

void Close(Ctx &c) {
  Rel(c.linear); Rel(c.st); Rel(c.rtv); Rel(c.rt); Rel(c.cb); Rel(c.il); Rel(c.ps_tex); Rel(c.ps); Rel(c.vs_rect); Rel(c.vs);
  if (c.dev) Out("d3d11ui end removed_reason=0x%08lx", (unsigned long)c.dev->GetDeviceRemovedReason());
  Rel(c.ctx); Rel(c.dev);
}

// Default pipeline after ClearState: no blend, default rasterizer (CullMode BACK,
// clockwise front faces), no scissor, full viewport, RT bound.
void Begin(Ctx &c, const float clear[4]) {
  c.ctx->ClearState();
  c.ctx->ClearRenderTargetView(c.rtv, clear);
  c.ctx->OMSetRenderTargets(1, &c.rtv, nullptr);
  D3D11_VIEWPORT vp = {0, 0, 64, 64, 0, 1};
  c.ctx->RSSetViewports(1, &vp);
  c.ctx->IASetInputLayout(c.il);
  c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  c.ctx->VSSetShader(c.vs, nullptr, 0);
  c.ctx->PSSetShader(c.ps, nullptr, 0);
}

// Readback: BGRA bytes of pixel (x, y).
struct Px { BYTE b, g, r, a; };
bool Read(Ctx &c, Px out[64][64]) {
  c.ctx->CopyResource(c.st, c.rt);
  D3D11_MAPPED_SUBRESOURCE m = {};
  if (FAILED(c.ctx->Map(c.st, 0, D3D11_MAP_READ, 0, &m))) return false;
  for (UINT y = 0; y < 64; ++y) memcpy(out[y], (const BYTE *)m.pData + y * m.RowPitch, 64 * 4);
  c.ctx->Unmap(c.st, 0);
  return true;
}
bool Near(const Px &p, int r, int g, int b) { return abs(p.r - r) <= 3 && abs(p.g - g) <= 3 && abs(p.b - b) <= 3; }

ID3D11Buffer *Vb(Ctx &c, const Vtx *v, UINT n) {
  D3D11_BUFFER_DESC bd = {n * (UINT)sizeof(Vtx), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  D3D11_SUBRESOURCE_DATA sd = {v, 0, 0};
  ID3D11Buffer *b = nullptr;
  c.dev->CreateBuffer(&bd, &sd, &b);
  return b;
}
void BindVb(Ctx &c, ID3D11Buffer *b) {
  UINT stride = sizeof(Vtx), offset = 0;
  c.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
}

void Report(const char *name, bool ok, const Px &centre) {
  Out("d3d11ui %s result=%s centre_rgb=%02x%02x%02x a=%02x", name, ok ? "PASS" : "FAIL", centre.r, centre.g, centre.b, centre.a);
}

void Cull(Ctx &c, bool ccwTriangles, bool frontCcw, bool expectVisible, const char *name) {
  // full-target quad as two triangles; clockwise on screen (y down) unless reversed
  Vtx cw[6] = {{-1, 1, 1, 0, 0, 1}, {1, 1, 1, 0, 0, 1}, {1, -1, 1, 0, 0, 1},
               {-1, 1, 1, 0, 0, 1}, {1, -1, 1, 0, 0, 1}, {-1, -1, 1, 0, 0, 1}};
  if (ccwTriangles) { std::swap(cw[1], cw[2]); std::swap(cw[4], cw[5]); }
  ID3D11Buffer *vb = Vb(c, cw, 6);
  D3D11_RASTERIZER_DESC rd = {D3D11_FILL_SOLID, D3D11_CULL_BACK, frontCcw, 0, 0, 0, TRUE, FALSE, FALSE, FALSE};
  ID3D11RasterizerState *rs = nullptr;
  c.dev->CreateRasterizerState(&rd, &rs);
  const float clear[4] = {0, 0, 1, 1};
  Begin(c, clear);
  c.ctx->RSSetState(rs);
  BindVb(c, vb);
  c.ctx->Draw(6, 0);
  static Px px[64][64];
  bool ok = Read(c, px);
  const bool red = Near(px[32][32], 255, 0, 0), blue = Near(px[32][32], 0, 0, 255);
  Report(name, ok && (expectVisible ? red : blue), px[32][32]);
  Rel(rs); Rel(vb);
}

void DynamicVb(Ctx &c) {
  D3D11_BUFFER_DESC bd = {8 * 6 * (UINT)sizeof(Vtx), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
  ID3D11Buffer *vb = nullptr;
  HRESULT hr = c.dev->CreateBuffer(&bd, nullptr, &vb);
  const float clear[4] = {0, 0, 0, 1};
  Begin(c, clear);
  BindVb(c, vb);
  for (UINT i = 0; SUCCEEDED(hr) && i < 8; ++i) {
    D3D11_MAPPED_SUBRESOURCE m = {};
    hr = c.ctx->Map(vb, 0, i ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD, 0, &m);
    if (FAILED(hr)) break;
    const float x0 = -1 + i * 0.25f, x1 = x0 + 0.25f, g = (i + 1) / 8.0f;
    Vtx q[6] = {{x0, 1, 1, g, 0, 1}, {x1, 1, 1, g, 0, 1}, {x1, -1, 1, g, 0, 1},
                {x0, 1, 1, g, 0, 1}, {x1, -1, 1, g, 0, 1}, {x0, -1, 1, g, 0, 1}};
    memcpy((Vtx *)m.pData + i * 6, q, sizeof(q));
    c.ctx->Unmap(vb, 0);
    c.ctx->Draw(6, i * 6);
  }
  static Px px[64][64];
  bool ok = SUCCEEDED(hr) && Read(c, px);
  UINT good = 0;
  for (UINT i = 0; ok && i < 8; ++i) if (Near(px[32][i * 8 + 4], 255, (int)((i + 1) * 255 / 8.0f + 0.5f), 0)) ++good;
  Out("d3d11ui dynamic_vb_nooverwrite result=%s columns_ok=%u/8 hr=0x%08lx col0=%02x%02x%02x col7=%02x%02x%02x", ok && good == 8 ? "PASS" : "FAIL",
      good, (unsigned long)hr, px[32][4].r, px[32][4].g, px[32][4].b, px[32][60].r, px[32][60].g, px[32][60].b);
  Rel(vb);
}

void DynamicCb(Ctx &c) {
  const float clear[4] = {0, 0, 0, 1};
  Begin(c, clear);
  c.ctx->IASetInputLayout(nullptr);
  c.ctx->VSSetShader(c.vs_rect, nullptr, 0);
  c.ctx->VSSetConstantBuffers(0, 1, &c.cb);
  HRESULT hr = S_OK;
  for (UINT i = 0; i < 8 && SUCCEEDED(hr); ++i) {
    D3D11_MAPPED_SUBRESOURCE m = {};
    hr = c.ctx->Map(c.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
    if (FAILED(hr)) break;
    const float x0 = -1 + i * 0.25f;
    const float data[8] = {0, (i + 1) / 8.0f, 1, 1, x0, -1, x0 + 0.25f, 1};
    memcpy(m.pData, data, sizeof(data));
    c.ctx->Unmap(c.cb, 0);
    c.ctx->Draw(6, 0);
  }
  static Px px[64][64];
  bool ok = SUCCEEDED(hr) && Read(c, px);
  UINT good = 0;
  for (UINT i = 0; ok && i < 8; ++i) if (Near(px[32][i * 8 + 4], 0, (int)((i + 1) * 255 / 8.0f + 0.5f), 255)) ++good;
  Out("d3d11ui dynamic_cb_discard result=%s columns_ok=%u/8 hr=0x%08lx col0=%02x%02x%02x col7=%02x%02x%02x", ok && good == 8 ? "PASS" : "FAIL",
      good, (unsigned long)hr, px[32][4].r, px[32][4].g, px[32][4].b, px[32][60].r, px[32][60].g, px[32][60].b);
}

void FullQuad(Ctx &c, float r, float g, float b, float a, ID3D11Buffer **vb) {
  Vtx q[6] = {{-1, 1, r, g, b, a}, {1, 1, r, g, b, a}, {1, -1, r, g, b, a},
              {-1, 1, r, g, b, a}, {1, -1, r, g, b, a}, {-1, -1, r, g, b, a}};
  *vb = Vb(c, q, 6);
  BindVb(c, *vb);
}

void Premultiplied(Ctx &c) {
  D3D11_BLEND_DESC bd = {};
  bd.RenderTarget[0] = {TRUE, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD,
                        D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
  ID3D11BlendState *bs = nullptr;
  c.dev->CreateBlendState(&bd, &bs);
  const float clear[4] = {0, 0, 1, 1};
  Begin(c, clear);
  c.ctx->OMSetBlendState(bs, nullptr, 0xffffffff);
  ID3D11Buffer *vb = nullptr;
  FullQuad(c, 0.5f, 0, 0, 0.5f, &vb);
  c.ctx->Draw(6, 0);
  static Px px[64][64];
  bool ok = Read(c, px);
  Report("blend_premultiplied", ok && Near(px[32][32], 128, 0, 128), px[32][32]);
  Rel(vb); Rel(bs);
}

void Scissor(Ctx &c) {
  D3D11_RASTERIZER_DESC rd = {D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0, 0, TRUE, TRUE, FALSE, FALSE};
  ID3D11RasterizerState *rs = nullptr;
  c.dev->CreateRasterizerState(&rd, &rs);
  const float clear[4] = {0, 0, 0, 1};
  Begin(c, clear);
  c.ctx->RSSetState(rs);
  D3D11_RECT sc = {16, 16, 48, 48};
  c.ctx->RSSetScissorRects(1, &sc);
  ID3D11Buffer *vb = nullptr;
  FullQuad(c, 1, 1, 0, 1, &vb);
  c.ctx->Draw(6, 0);
  static Px px[64][64];
  bool ok = Read(c, px);
  Report("scissor", ok && Near(px[32][32], 255, 255, 0) && Near(px[4][4], 0, 0, 0) && Near(px[60][60], 0, 0, 0), px[32][32]);
  Rel(vb); Rel(rs);
}

ID3D11ShaderResourceView *SolidTexture(Ctx &c, UINT32 bgra, bool update, ID3D11Texture2D **tex) {
  static UINT32 data[16 * 16];
  for (UINT32 &d : data) d = bgra;
  D3D11_TEXTURE2D_DESC td = {16, 16, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  D3D11_SUBRESOURCE_DATA sd = {data, 16 * 4, 0};
  c.dev->CreateTexture2D(&td, update ? nullptr : &sd, tex);
  if (!*tex) return nullptr;
  if (update) c.ctx->UpdateSubresource(*tex, 0, nullptr, data, 16 * 4, 0);
  ID3D11ShaderResourceView *srv = nullptr;
  c.dev->CreateShaderResourceView(*tex, nullptr, &srv);
  return srv;
}

void Textured(Ctx &c, bool update, const char *name) {
  const float clear[4] = {0, 0, 0, 1};
  ID3D11Texture2D *tex = nullptr;
  Begin(c, clear);
  ID3D11ShaderResourceView *srv = SolidTexture(c, 0xff2080e0u /* a=ff r=20 g=80 b=e0 */, update, &tex);
  c.ctx->PSSetShader(c.ps_tex, nullptr, 0);
  c.ctx->PSSetShaderResources(0, 1, &srv);
  c.ctx->PSSetSamplers(0, 1, &c.linear);
  ID3D11Buffer *vb = nullptr;
  FullQuad(c, 1, 1, 1, 1, &vb);
  c.ctx->Draw(6, 0);
  static Px px[64][64];
  bool ok = srv && Read(c, px);
  Report(name, ok && Near(px[32][32], 0x20, 0x80, 0xe0), px[32][32]);
  Rel(vb); Rel(srv); Rel(tex);
}

void Offscreen(Ctx &c) {
  D3D11_TEXTURE2D_DESC td = {128, 128, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
  ID3D11Texture2D *off = nullptr;
  ID3D11RenderTargetView *ortv = nullptr;
  ID3D11ShaderResourceView *osrv = nullptr;
  c.dev->CreateTexture2D(&td, nullptr, &off);
  if (off) { c.dev->CreateRenderTargetView(off, nullptr, &ortv); c.dev->CreateShaderResourceView(off, nullptr, &osrv); }
  if (!ortv || !osrv) { Out("d3d11ui offscreen_composite result=FAIL stage=create"); Rel(osrv); Rel(ortv); Rel(off); return; }
  // pass 1: draw a green quad into the 128x128 offscreen target
  c.ctx->ClearState();
  const float black[4] = {0, 0, 0, 1};
  c.ctx->ClearRenderTargetView(ortv, black);
  c.ctx->OMSetRenderTargets(1, &ortv, nullptr);
  D3D11_VIEWPORT vp = {0, 0, 128, 128, 0, 1};
  c.ctx->RSSetViewports(1, &vp);
  c.ctx->IASetInputLayout(c.il);
  c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  c.ctx->VSSetShader(c.vs, nullptr, 0);
  c.ctx->PSSetShader(c.ps, nullptr, 0);
  ID3D11Buffer *vb = nullptr;
  FullQuad(c, 0, 1, 0, 1, &vb);
  c.ctx->Draw(6, 0);
  Rel(vb);
  // pass 2: sample it into the final target
  Begin(c, black);
  c.ctx->PSSetShader(c.ps_tex, nullptr, 0);
  c.ctx->PSSetShaderResources(0, 1, &osrv);
  c.ctx->PSSetSamplers(0, 1, &c.linear);
  FullQuad(c, 1, 1, 1, 1, &vb);
  c.ctx->Draw(6, 0);
  static Px px[64][64];
  bool ok = Read(c, px);
  Report("offscreen_composite", ok && Near(px[32][32], 0, 255, 0), px[32][32]);
  Rel(vb); Rel(osrv); Rel(ortv); Rel(off);
}

void ScreenTwo(const char *name, HWND w, int r, int g, int b) {
  Sleep(400);
  Pump();
  POINT o = {0, 0};
  ClientToScreen(w, &o);
  HDC dc = GetDC(nullptr);
  COLORREF q = GetPixel(dc, o.x + 35, o.y + 35), bg = GetPixel(dc, o.x + 200, o.y + 100);
  ReleaseDC(nullptr, dc);
  const bool quad = abs(GetRValue(q) - r) < 24 && abs(GetGValue(q) - g) < 24 && abs(GetBValue(q) - b) < 24;
  const bool white = GetRValue(bg) > 230 && GetGValue(bg) > 230 && GetBValue(bg) > 230;
  Out("d3d11ui %s result=%s quad_rgb=%02x%02x%02x background_rgb=%02x%02x%02x", name, quad && white ? "PASS" : "FAIL",
      GetRValue(q), GetGValue(q), GetBValue(q), GetRValue(bg), GetGValue(bg), GetBValue(bg));
}

// Swapchain presents read back from the screen (user session only).
//   blt  : legacy CreateSwapChain, DXGI_SWAP_EFFECT_DISCARD, one buffer,
//          B8G8R8A8, created at 120x61 (as Cinebench creates them); with
//          resize it is ResizeBuffers'd to the 256x128 window, without it the
//          120x61 buffer is stretched to the window by the present.
//   flip : CreateSwapChainForHwnd, FLIP_DISCARD, two buffers, 256x128.
// Each window gets a white clear and a drawn quad over (10,10)-(60,60).
void SwapTest(Ctx &c, bool flip, bool resize, const char *prefix) {
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  if (!session) { Out("d3d11ui %s result=SKIP reason=session0", prefix); return; }
  EnsureClass();
  HWND w[2] = {CreateWindowExW(WS_EX_TOPMOST, L"NwGpuProbe", L"swap1", WS_POPUP | WS_VISIBLE, 120, 120, 256, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr),
               CreateWindowExW(WS_EX_TOPMOST, L"NwGpuProbe", L"swap2", WS_POPUP | WS_VISIBLE, 420, 120, 256, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
  Pump();
  IDXGIDevice *xd = nullptr; IDXGIAdapter *a = nullptr; IDXGIFactory2 *f = nullptr;
  IDXGISwapChain *sc[2] = {};
  HRESULT hr = c.dev->QueryInterface(IID_PPV_ARGS(&xd));
  if (SUCCEEDED(hr)) hr = xd->GetAdapter(&a);
  if (SUCCEEDED(hr)) hr = a->GetParent(IID_PPV_ARGS(&f));
  for (int i = 0; i < 2 && SUCCEEDED(hr); ++i) {
    if (flip) {
      DXGI_SWAP_CHAIN_DESC1 d = {};
      d.Width = 256; d.Height = 128; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2;
      d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d.Scaling = DXGI_SCALING_STRETCH;
      IDXGISwapChain1 *s1 = nullptr;
      hr = f->CreateSwapChainForHwnd(c.dev, w[i], &d, nullptr, nullptr, &s1);
      sc[i] = s1;
    } else {
      DXGI_SWAP_CHAIN_DESC d = {};
      d.BufferDesc.Width = 120; d.BufferDesc.Height = 61; d.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 1;
      d.OutputWindow = w[i]; d.Windowed = TRUE; d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
      hr = f->CreateSwapChain(c.dev, &d, &sc[i]);
      if (SUCCEEDED(hr) && resize) hr = sc[i]->ResizeBuffers(1, 256, 128, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    }
  }
  Out("d3d11ui %s create hr=0x%08lx", prefix, (unsigned long)hr);
  HRESULT presentHr = S_OK;
  for (int frame = 0; frame < 30 && SUCCEEDED(hr); ++frame)
    for (int i = 0; i < 2 && SUCCEEDED(hr); ++i) {
      ID3D11Texture2D *bb = nullptr; ID3D11RenderTargetView *rtv = nullptr;
      hr = sc[i]->GetBuffer(0, IID_PPV_ARGS(&bb));
      if (SUCCEEDED(hr)) hr = c.dev->CreateRenderTargetView(bb, nullptr, &rtv);
      if (SUCCEEDED(hr)) {
        D3D11_TEXTURE2D_DESC bd = {};
        bb->GetDesc(&bd);
        if (!frame && !i) Out("d3d11ui %s backbuffer=%ux%u", prefix, bd.Width, bd.Height);
        c.ctx->ClearState();
        const float bg[4] = {1, 1, 1, 1};
        c.ctx->ClearRenderTargetView(rtv, bg);
        c.ctx->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT vp = {10, 10, 50, 50, 0, 1};
        c.ctx->RSSetViewports(1, &vp);
        c.ctx->IASetInputLayout(c.il);
        c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c.ctx->VSSetShader(c.vs, nullptr, 0);
        c.ctx->PSSetShader(c.ps, nullptr, 0);
        ID3D11Buffer *vb = nullptr;
        FullQuad(c, i ? 0.0f : 1.0f, 0, i ? 1.0f : 0.0f, 1, &vb);
        c.ctx->Draw(6, 0);
        Rel(vb);
        HRESULT p = sc[i]->Present(0, 0);
        if (FAILED(p) && SUCCEEDED(presentHr)) presentHr = p;
      }
      Rel(rtv); Rel(bb);
      Pump();
    }
  Out("d3d11ui %s present_first_error=0x%08lx", prefix, (unsigned long)presentHr);
  if (SUCCEEDED(hr)) {
    char n1[96], n2[96];
    snprintf(n1, sizeof(n1), "%s_1", prefix);
    snprintf(n2, sizeof(n2), "%s_2", prefix);
    ScreenTwo(n1, w[0], 255, 0, 0); ScreenTwo(n2, w[1], 0, 0, 255);
  } else Out("d3d11ui %s result=FAIL hr=0x%08lx", prefix, (unsigned long)hr);
  Rel(sc[0]); Rel(sc[1]); Rel(f); Rel(a); Rel(xd);
  DestroyWindow(w[0]); DestroyWindow(w[1]);
  Pump();
}

void BltSwapchains(Ctx &c) { SwapTest(c, false, true, "blt_swapchain"); }

void SwapSet(Ctx &c, const char *suffix) {
  char n[64];
  snprintf(n, sizeof(n), "blt_swapchain%s", suffix); SwapTest(c, false, true, n);
  snprintf(n, sizeof(n), "blt_noresize%s", suffix); SwapTest(c, false, false, n);
  snprintf(n, sizeof(n), "flip_swapchain%s", suffix); SwapTest(c, true, false, n);
}

// Mode change at runtime: the primary display's current mode, then another
// one it lists (a different size, same depth), the swapchain set at each,
// and the original mode restored. CDS_FULLSCREEN keeps the change temporary
// (Windows restores the registry mode when this process ends, too).
void ModeSwitch(Ctx &c) {
  DEVMODEW cur = {}; cur.dmSize = sizeof(cur);
  if (!EnumDisplaySettingsExW(nullptr, ENUM_CURRENT_SETTINGS, &cur, 0)) {
    Out("d3d11ui modeswitch result=FAIL reason=current gle=%lu", GetLastError()); return;
  }
  Out("d3d11ui modeswitch current=%lux%lu bpp=%lu hz=%lu", cur.dmPelsWidth, cur.dmPelsHeight, cur.dmBitsPerPel, cur.dmDisplayFrequency);
  SwapSet(c, "_mode0");
  DEVMODEW pick = {}; bool found = false; DWORD listed = 0;
  for (DWORD i = 0;; ++i) {
    DEVMODEW m = {}; m.dmSize = sizeof(m);
    if (!EnumDisplaySettingsExW(nullptr, i, &m, 0)) break;
    ++listed;
    Out("d3d11ui modeswitch listed=%lux%lu bpp=%lu hz=%lu", m.dmPelsWidth, m.dmPelsHeight, m.dmBitsPerPel, m.dmDisplayFrequency);
    if (m.dmBitsPerPel != cur.dmBitsPerPel) continue;
    if (m.dmPelsWidth == cur.dmPelsWidth && m.dmPelsHeight == cur.dmPelsHeight) continue;
    if (m.dmPelsWidth < 800 || m.dmPelsHeight < 600) continue;
    // The largest listed size below the current one: a real scaled mode, big
    // enough for both test windows.
    if (!found || (unsigned long long)m.dmPelsWidth * m.dmPelsHeight >
                  (unsigned long long)pick.dmPelsWidth * pick.dmPelsHeight) {
      if ((unsigned long long)m.dmPelsWidth * m.dmPelsHeight <
          (unsigned long long)cur.dmPelsWidth * cur.dmPelsHeight) { pick = m; found = true; }
    }
  }
  Out("d3d11ui modeswitch listed_count=%lu", listed);
  if (!found) { Out("d3d11ui modeswitch result=SKIP reason=no_other_mode"); return; }
  pick.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
  LONG r = ChangeDisplaySettingsExW(nullptr, &pick, nullptr, CDS_FULLSCREEN, nullptr);
  Sleep(3000); Pump();
  DEVMODEW now = {}; now.dmSize = sizeof(now);
  EnumDisplaySettingsExW(nullptr, ENUM_CURRENT_SETTINGS, &now, 0);
  Out("d3d11ui modeswitch set=%lux%lu change=%ld now=%lux%lu", pick.dmPelsWidth, pick.dmPelsHeight, r, now.dmPelsWidth, now.dmPelsHeight);
  Out("d3d11ui modeswitch_set result=%s", r == DISP_CHANGE_SUCCESSFUL && now.dmPelsWidth == pick.dmPelsWidth && now.dmPelsHeight == pick.dmPelsHeight ? "PASS" : "FAIL");
  if (r == DISP_CHANGE_SUCCESSFUL) SwapSet(c, "_mode1");
  LONG back = ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
  Sleep(3000); Pump();
  now = DEVMODEW{}; now.dmSize = sizeof(now);
  EnumDisplaySettingsExW(nullptr, ENUM_CURRENT_SETTINGS, &now, 0);
  Out("d3d11ui modeswitch restore=%ld now=%lux%lu", back, now.dmPelsWidth, now.dmPelsHeight);
  Out("d3d11ui modeswitch_restore result=%s", back == DISP_CHANGE_SUCCESSFUL && now.dmPelsWidth == cur.dmPelsWidth && now.dmPelsHeight == cur.dmPelsHeight ? "PASS" : "FAIL");
  SwapSet(c, "_restored");
}

}  // namespace d3d11ui

static void TestD3d11Ui() {
  d3d11ui::Ctx c;
  if (d3d11ui::Open(c)) {
    d3d11ui::Cull(c, false, false, true, "cull_back_cw_visible");
    d3d11ui::Cull(c, true, false, false, "cull_back_ccw_culled");
    d3d11ui::Cull(c, false, true, false, "cull_back_cw_frontccw_culled");
    d3d11ui::Cull(c, true, true, true, "cull_back_ccw_frontccw_visible");
    d3d11ui::DynamicVb(c);
    d3d11ui::DynamicCb(c);
    d3d11ui::Premultiplied(c);
    d3d11ui::Scissor(c);
    d3d11ui::Textured(c, false, "texture_bgra_linear");
    d3d11ui::Textured(c, true, "update_subresource_texture");
    d3d11ui::Offscreen(c);
    d3d11ui::BltSwapchains(c);
    d3d11ui::SwapTest(c, false, false, "blt_noresize");
    d3d11ui::SwapTest(c, true, false, "flip_swapchain");
  }
  d3d11ui::Close(c);
}

// "modeswitch": swapchain set, runtime mode change, set again, restore, set.
static void TestModeSwitch() {
  d3d11ui::Ctx c;
  if (d3d11ui::Open(c)) d3d11ui::ModeSwitch(c);
  d3d11ui::Close(c);
}
