// probe18: psfile=<path> - pixel-shader expressions read from a text file, drawn on the hardware D3D11 device and
// on WARP in the same process, and compared pixel by pixel (red channel, 64x64). New cases need no new binary:
// a job can write the text file. One case per line:  name=HLSL expression
// Available in the expression: x, y = TEXCOORD3 (noperspective) = (a, b) * 100 with a, b in (-1,1);
// uv (float2, the same input), p (SV_Position), t0 (TEXCOORD0 = clip position). Lines starting with # are skipped.
//   psfile line: case=<name> bad=<pixels where |hw - warp| > 3> max_err first_bad=x,y hw=<v> warp=<v>
static char g_psFile[MAX_PATH];

static bool PsFileDraw(ID3D11Device *dev, ID3D11DeviceContext *ctx, const char *vsrc, const char *psrc, BYTE *red, const char *who, const char *name) {
  const UINT W = 64, H = 64;
  ID3DBlob *vsb = CompileCs(vsrc, "vs_5_0"), *psb = CompileCs(psrc, "ps_5_0");
  ID3D11VertexShader *vs = nullptr; ID3D11PixelShader *ps = nullptr;
  ID3D11Texture2D *rt = nullptr, *st = nullptr; ID3D11RenderTargetView *rtv = nullptr;
  HRESULT hr = (vsb && psb) ? dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs) : E_FAIL;
  if (SUCCEEDED(hr)) hr = dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
  D3D11_TEXTURE2D_DESC td = {};
  td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (SUCCEEDED(hr)) hr = dev->CreateTexture2D(&td, nullptr, &rt);
  if (SUCCEEDED(hr)) hr = dev->CreateRenderTargetView(rt, nullptr, &rtv);
  td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (SUCCEEDED(hr)) hr = dev->CreateTexture2D(&td, nullptr, &st);
  bool ok = false;
  if (SUCCEEDED(hr)) {
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
    if (SUCCEEDED(hr)) {
      for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) red[y * W + x] = ((const BYTE *)m.pData + y * m.RowPitch + x * 4)[0];
      ctx->Unmap(st, 0); ok = true;
    }
  }
  if (!ok) Out("psfile case=%s result=FAIL stage=%s hr=0x%08lx", name, who, (unsigned long)hr);
  ctx->OMSetRenderTargets(0, nullptr, nullptr);
  Rel(st); Rel(rtv); Rel(rt); Rel(ps); Rel(vs); Rel(psb); Rel(vsb);
  return ok;
}

static void TestPsFile() {
  FILE *f = nullptr;
  if (fopen_s(&f, g_psFile, "r") || !f) { Out("psfile result=FAIL stage=open path=\"%s\"", g_psFile); return; }
  ID3D11Device *hw = nullptr, *sw = nullptr; ID3D11DeviceContext *hc = nullptr, *sc = nullptr;
  D3D_FEATURE_LEVEL got = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &hw, &got, &hc);
  if (SUCCEEDED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sw, &got, &sc);
  if (FAILED(hr)) { Out("psfile result=FAIL stage=device hr=0x%08lx arch=%s", (unsigned long)hr, Arch()); fclose(f); Rel(hc); Rel(hw); Rel(sc); Rel(sw); return; }
  Out("psfile begin arch=%s path=\"%s\"", Arch(), g_psFile);
  static const char *kVs =
      "struct VO { float4 pos : SV_Position; noperspective float4 t0 : TEXCOORD0; noperspective float4 t1 : TEXCOORD1; noperspective float4 t2 : TEXCOORD2; noperspective float2 uv : TEXCOORD3; };\n"
      "VO main(uint id : SV_VertexID) { float2 p = float2((id << 1) & 2, id & 2); VO o;\n"
      "  o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); o.t0 = o.pos; o.t1 = o.pos; o.t2 = float4(0.25, 0.5, 0.75, 1);\n"
      "  o.uv = (p * 2.0 - 1.0) * 100.0; return o; }\n";
  char line[900]; unsigned ran = 0, failed = 0;
  static BYTE a[4096], b[4096];
  while (fgets(line, sizeof(line), f)) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
    char *eq = strchr(line, '=');
    if (!n || line[0] == '#' || !eq) continue;
    *eq = 0; const char *name = line, *expr = eq + 1;
    char psrc[1500];
    sprintf_s(psrc, "float4 main(float4 p : SV_Position, noperspective float4 t0 : TEXCOORD0, noperspective float4 t1 : TEXCOORD1, noperspective float4 t2 : TEXCOORD2, noperspective float2 uv : TEXCOORD3) : SV_Target {\n"
                    "  float x = uv.x, y = uv.y;\n  float e = %s;\n  return float4(e, 0.5, 0.5, 1.0); }\n", expr);
    if (!PsFileDraw(hw, hc, kVs, psrc, a, "hw", name) || !PsFileDraw(sw, sc, kVs, psrc, b, "warp", name)) { ++failed; continue; }
    unsigned bad = 0, maxErr = 0; int fx = -1, fy = -1, fh = 0, fw = 0;
    for (unsigned i = 0; i < 4096; ++i) {
      unsigned err = (unsigned)abs((int)a[i] - (int)b[i]);
      if (err > maxErr) maxErr = err;
      if (err > 3) { if (!bad) { fx = (int)(i % 64); fy = (int)(i / 64); fh = a[i]; fw = b[i]; } ++bad; }
    }
    if (bad) {
      for (unsigned gy = 0; gy < 8; ++gy) {
        char r1[9], r2[9];
        for (unsigned gx = 0; gx < 8; ++gx) { r1[gx] = "0123456789abcdef"[a[(gy * 8 + 4) * 64 + gx * 8 + 4] >> 4]; r2[gx] = "0123456789abcdef"[b[(gy * 8 + 4) * 64 + gx * 8 + 4] >> 4]; }
        r1[8] = 0; r2[8] = 0;
        Out("psfile map case=%s row=%u hw=%s warp=%s", name, gy, r1, r2);
      }
      ++failed;
    }
    Out("psfile case=%s bad=%u of=4096 max_err=%u first_bad=%d,%d hw=%d warp=%d result=%s", name, bad, maxErr, fx, fy, fh, fw, bad ? "FAIL" : "PASS");
    ++ran;
  }
  fclose(f);
  Out("psfile end ran=%u failed_cases=%u removed_reason=0x%08lx result=%s", ran, failed, (unsigned long)hw->GetDeviceRemovedReason(), failed ? "FAIL" : "PASS");
  Rel(hc); Rel(hw); Rel(sc); Rel(sw);
}
