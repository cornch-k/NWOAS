// probe17: psvary - values passed from the vertex shader to the pixel shader (D3D11 hardware device, or WARP).
// Background (K55, 2026-11-20): Edge's conic-gradient pixel shader reads only v4.xy (noperspective, local
// coordinates in pixels, magnitude ~100) while the vertex shader also writes o1..o3; its atan2 arithmetic is the
// same sequence psmath already passed, so the value arriving in v4 is the suspect.
// The vertex shader writes TEXCOORD0..2 (float4) and TEXCOORD3.xy = (a, b) * scale, a and b in (-1,1) as in psmath.
//   psvary line: case=<scale>-<interp>-<all|last|sweep> bad=<pixels off by more than 3/255> max_err first_bad got want
//   all  : the pixel shader also reads TEXCOORD0..2;  last : it reads only TEXCOORD3 (like Edge's shader)
//   sweep: red = atan2(-uv.y, -uv.x) / 2pi + 0.5 (what the conic gradient computes), reads only TEXCOORD3

static void TestPsVary() {
  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  D3D_FEATURE_LEVEL got = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = D3D11CreateDevice(nullptr, g_warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev, &got, &ctx);
  if (FAILED(hr)) { Out("psvary result=FAIL stage=device hr=0x%08lx arch=%s", (unsigned long)hr, Arch()); return; }
  Out("psvary begin arch=%s warp=%u feature_level=0x%x", Arch(), g_warp ? 1u : 0u, (unsigned)got);
  const UINT W = 64, H = 64;
  ID3D11Texture2D *rt = nullptr, *st = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  D3D11_TEXTURE2D_DESC td = {};
  td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
  hr = dev->CreateTexture2D(&td, nullptr, &rt);
  if (SUCCEEDED(hr)) hr = dev->CreateRenderTargetView(rt, nullptr, &rtv);
  td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (SUCCEEDED(hr)) hr = dev->CreateTexture2D(&td, nullptr, &st);
  if (FAILED(hr)) { Out("psvary result=FAIL stage=create hr=0x%08lx", (unsigned long)hr); Rel(st); Rel(rtv); Rel(rt); Rel(ctx); Rel(dev); return; }
  static const double kScales[] = {1.0, 100.0};
  static const char *kInterp[] = {"", "noperspective"};
  static const char *kKinds[] = {"all", "last", "sweep"};
  unsigned failedCases = 0, ran = 0;
  for (unsigned si = 0; si < 2; ++si) for (unsigned ii = 0; ii < 2; ++ii) for (unsigned ki = 0; ki < 3; ++ki) {
    const double S = kScales[si];
    char vsrc[900], psrc[900], name[48];
    sprintf_s(name, "%g-%s-%s", S, ii ? "noperspective" : "linear", kKinds[ki]);
    sprintf_s(vsrc, "struct VO { float4 pos : SV_Position; %s float4 t0 : TEXCOORD0; %s float4 t1 : TEXCOORD1; %s float4 t2 : TEXCOORD2; %s float2 uv : TEXCOORD3; };\n"
                    "VO main(uint id : SV_VertexID) { float2 p = float2((id << 1) & 2, id & 2); VO o;\n"
                    "  o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); o.t0 = o.pos; o.t1 = o.pos; o.t2 = float4(0.25, 0.5, 0.75, 1);\n"
                    "  o.uv = (p * 2.0 - 1.0) * %g; return o; }\n", kInterp[ii], kInterp[ii], kInterp[ii], kInterp[ii], S);
    const char *bodyAll = "return float4(uv.x / %g + 0.5, uv.y / %g + 0.5, t0.x * 0.25 + t1.x * 0.25 + t2.y, 1.0);";
    const char *bodyLast = "return float4(uv.x / %g + 0.5, uv.y / %g + 0.5, 0.5, 1.0);";
    const char *bodySweep = "return float4(atan2(-uv.y, -uv.x) / 6.2831853 + 0.5, uv.y / %g + 0.5, 0.5 + 0.0 * %g, 1.0);";
    char body[300];
    sprintf_s(body, ki == 0 ? bodyAll : ki == 1 ? bodyLast : bodySweep, 2.0 * S, 2.0 * S);
    sprintf_s(psrc, "float4 main(float4 pos : SV_Position, %s float4 t0 : TEXCOORD0, %s float4 t1 : TEXCOORD1, %s float4 t2 : TEXCOORD2, %s float2 uv : TEXCOORD3) : SV_Target {\n  %s }\n",
              kInterp[ii], kInterp[ii], kInterp[ii], kInterp[ii], body);
    ID3DBlob *vsb = CompileCs(vsrc, "vs_5_0"), *psb = CompileCs(psrc, "ps_5_0");
    ID3D11VertexShader *vs = nullptr; ID3D11PixelShader *ps = nullptr;
    hr = (vsb && psb) ? dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs) : E_FAIL;
    if (SUCCEEDED(hr)) hr = dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
    if (FAILED(hr)) { Out("psvary case=%s result=FAIL stage=shader hr=0x%08lx", name, (unsigned long)hr); ++failedCases; Rel(ps); Rel(vs); Rel(psb); Rel(vsb); continue; }
    const float mid[4] = {0.5f, 0, 0, 1};
    ctx->ClearRenderTargetView(rtv, mid);
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
    if (FAILED(hr)) { Out("psvary case=%s result=FAIL stage=map hr=0x%08lx", name, (unsigned long)hr); ++failedCases; Rel(ps); Rel(vs); Rel(psb); Rel(vsb); continue; }
    unsigned bad = 0, maxErr = 0, badG = 0; int fx = -1, fy = -1, fg = 0, fw = 0;
    for (UINT y = 0; y < H; ++y)
      for (UINT x = 0; x < W; ++x) {
        const BYTE *px = (const BYTE *)m.pData + y * m.RowPitch + x * 4;
        double a = (x + 0.5) / 32.0 - 1.0, b = (y + 0.5) / 32.0 - 1.0;
        double wr = ki == 2 ? atan2(-b, -a) / (2 * kPmPi) + 0.5 : a * 0.5 + 0.5;
        int want = (int)floor(PmSat(wr) * 255.0 + 0.5), wantG = (int)floor(PmSat(b * 0.5 + 0.5) * 255.0 + 0.5);
        unsigned err = (unsigned)abs((int)px[0] - want);
        if (abs((int)px[1] - wantG) > 3) ++badG;
        if (err > maxErr) maxErr = err;
        if (err > 3) { if (!bad) { fx = (int)x; fy = (int)y; fg = px[0]; fw = want; } ++bad; }
      }
    if (bad || badG) {
      for (UINT gy = 0; gy < 8; ++gy) {
        char row[9], rowg[9];
        for (UINT gx = 0; gx < 8; ++gx) {
          const BYTE *px = (const BYTE *)m.pData + (gy * 8 + 4) * m.RowPitch + (gx * 8 + 4) * 4;
          row[gx] = "0123456789abcdef"[px[0] >> 4]; rowg[gx] = "0123456789abcdef"[px[1] >> 4];
        }
        row[8] = 0; rowg[8] = 0;
        Out("psvary map case=%s row=%u red=%s green=%s", name, gy, row, rowg);
      }
      ++failedCases;
    }
    Out("psvary case=%s bad=%u bad_green=%u of=%u max_err=%u first_bad=%d,%d got=%d want=%d result=%s", name, bad, badG, W * H, maxErr, fx, fy,
        fg, fw, (bad || badG) ? "FAIL" : "PASS");
    ctx->Unmap(st, 0);
    ++ran;
    Rel(ps); Rel(vs); Rel(psb); Rel(vsb);
  }
  Out("psvary end ran=%u failed_cases=%u removed_reason=0x%08lx result=%s", ran, failedCases,
      (unsigned long)dev->GetDeviceRemovedReason(), failedCases ? "FAIL" : "PASS");
  Rel(st); Rel(rtv); Rel(rt); Rel(ctx); Rel(dev);
}
