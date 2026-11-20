// probe16: psmath - pixel-shader arithmetic check on the hardware D3D11 device (or WARP with "warp").
// Background: Edge with GPU rasterization draws conic gradients as four flat quadrants on the AGX driver
// (K52, 2026-11-20) while Parallels draws them correctly, so one of the operations behind atan2() is suspected.
// Each case draws a 64x64 RGBA8 target with a full-screen triangle; a = x in (-1,1), b = y in (-1,1) from
// SV_Position, the shader writes one value in [0,1] to red and the CPU computes the same value.
//   psmath line: case=<name> target=<ps_4_0|ps_5_0> bad=<pixels off by more than 3/255> max_err=<0..255>
//                first_bad=x,y got=<0..255> want=<0..255>

#include <math.h>

struct PsMathCase { const char *name; const char *expr; double (*want)(double a, double b); };

static double PmSat(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static const double kPmPi = 3.14159265358979323846;
static const PsMathCase kPsMathCases[] = {
    {"const", "0.5", [](double, double) { return 0.5; }},
    {"pos_a", "a*0.5+0.5", [](double a, double) { return a * 0.5 + 0.5; }},
    {"pos_b", "b*0.5+0.5", [](double, double b) { return b * 0.5 + 0.5; }},
    {"abs", "abs(a)", [](double a, double) { return fabs(a); }},
    {"mul", "a*b*0.5+0.5", [](double a, double b) { return a * b * 0.5 + 0.5; }},
    {"min", "min(abs(a),abs(b))", [](double a, double b) { return fmin(fabs(a), fabs(b)); }},
    {"max", "max(abs(a),abs(b))", [](double a, double b) { return fmax(fabs(a), fabs(b)); }},
    {"min_signed", "min(a,b)*0.5+0.5", [](double a, double b) { return fmin(a, b) * 0.5 + 0.5; }},
    {"max_signed", "max(a,b)*0.5+0.5", [](double a, double b) { return fmax(a, b) * 0.5 + 0.5; }},
    {"div", "abs(a)/(1.0+abs(b))", [](double a, double b) { return fabs(a) / (1.0 + fabs(b)); }},
    {"rcp", "0.5/(0.5+abs(a)+abs(b))", [](double a, double b) { return 0.5 / (0.5 + fabs(a) + fabs(b)); }},
    {"min_over_max", "min(abs(a),abs(b))/max(abs(a),abs(b))", [](double a, double b) { return fmin(fabs(a), fabs(b)) / fmax(fabs(a), fabs(b)); }},
    {"lt_select", "(a<b)?0.75:0.25", [](double a, double b) { return a < b ? 0.75 : 0.25; }},
    {"gt_abs_select", "(abs(b)>abs(a))?0.75:0.25", [](double a, double b) { return fabs(b) > fabs(a) ? 0.75 : 0.25; }},
    {"step", "step(a,b)", [](double a, double b) { return b >= a ? 1.0 : 0.0; }},
    {"sign", "sign(a)*0.25+0.5", [](double a, double) { return (a > 0 ? 1 : a < 0 ? -1 : 0) * 0.25 + 0.5; }},
    {"saturate", "saturate(a+b)", [](double a, double b) { return PmSat(a + b); }},
    {"clamp", "clamp(a*2.0,0.25,0.75)", [](double a, double) { double v = a * 2.0; return v < 0.25 ? 0.25 : v > 0.75 ? 0.75 : v; }},
    {"floor", "floor(a*4.0)/8.0+0.5", [](double a, double) { return floor(a * 4.0) / 8.0 + 0.5; }},
    {"frac", "frac(a*4.0)", [](double a, double) { double v = a * 4.0; return v - floor(v); }},
    {"sqrt", "sqrt(a*a+b*b)/1.5", [](double a, double b) { return sqrt(a * a + b * b) / 1.5; }},
    {"rsqrt_norm", "normalize(float2(a,b)).x*0.5+0.5", [](double a, double b) { return a / sqrt(a * a + b * b) * 0.5 + 0.5; }},
    {"sin", "sin(a*3.14159265)*0.5+0.5", [](double a, double) { return sin(a * kPmPi) * 0.5 + 0.5; }},
    {"cos", "cos(b*3.14159265)*0.5+0.5", [](double, double b) { return cos(b * kPmPi) * 0.5 + 0.5; }},
    {"pow", "pow(abs(a),0.5)", [](double a, double) { return pow(fabs(a), 0.5); }},
    {"exp_log", "exp(log(abs(a)+0.5)*0.5)*0.5", [](double a, double) { return exp(log(fabs(a) + 0.5) * 0.5) * 0.5; }},
    {"atan", "atan(b/a)/3.14159265+0.5", [](double a, double b) { return atan(b / a) / kPmPi + 0.5; }},
    {"atan_small", "atan(min(abs(a),abs(b))/max(abs(a),abs(b)))*1.2", [](double a, double b) { return atan(fmin(fabs(a), fabs(b)) / fmax(fabs(a), fabs(b))) * 1.2; }},
    {"atan2", "atan2(b,a)/6.2831853+0.5", [](double a, double b) { return atan2(b, a) / (2 * kPmPi) + 0.5; }},
    {"atan2_neg", "atan2(-b,-a)/6.2831853+0.5", [](double a, double b) { return atan2(-b, -a) / (2 * kPmPi) + 0.5; }},
    {"acos", "acos(a)/3.14159265", [](double a, double) { return acos(a) / kPmPi; }},
    {"asin", "asin(a)/3.14159265+0.5", [](double a, double) { return asin(a) / kPmPi + 0.5; }},
};

static void TestPsMath() {
  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  D3D_FEATURE_LEVEL got = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = D3D11CreateDevice(nullptr, g_warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev, &got, &ctx);
  if (FAILED(hr)) { Out("psmath result=FAIL stage=device hr=0x%08lx arch=%s", (unsigned long)hr, Arch()); return; }
  Out("psmath begin arch=%s warp=%u feature_level=0x%x cases=%u", Arch(), g_warp ? 1u : 0u, (unsigned)got,
      (unsigned)(sizeof(kPsMathCases) / sizeof(kPsMathCases[0])));
  const UINT W = 64, H = 64;
  ID3DBlob *vsb = CompileCs(kDrawVs, "vs_4_0");
  ID3D11VertexShader *vs = nullptr;
  ID3D11Texture2D *rt = nullptr, *st = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  hr = vsb ? dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs) : E_FAIL;
  D3D11_TEXTURE2D_DESC td = {};
  td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (SUCCEEDED(hr)) hr = dev->CreateTexture2D(&td, nullptr, &rt);
  if (SUCCEEDED(hr)) hr = dev->CreateRenderTargetView(rt, nullptr, &rtv);
  td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (SUCCEEDED(hr)) hr = dev->CreateTexture2D(&td, nullptr, &st);
  if (FAILED(hr)) { Out("psmath result=FAIL stage=create hr=0x%08lx", (unsigned long)hr); Rel(st); Rel(rtv); Rel(rt); Rel(vs); Rel(vsb); Rel(ctx); Rel(dev); return; }
  unsigned failedCases = 0, ran = 0;
  static const char *kTargets[] = {"ps_4_0", "ps_5_0"};
  for (unsigned t = 0; t < 2; ++t) {
    for (unsigned i = 0; i < sizeof(kPsMathCases) / sizeof(kPsMathCases[0]); ++i) {
      const PsMathCase &c = kPsMathCases[i];
      char src[700];
      sprintf_s(src, "float4 main(float4 p : SV_Position) : SV_Target {\n"
                     "  float a = p.x / 32.0 - 1.0; float b = p.y / 32.0 - 1.0;\n"
                     "  float e = %s;\n  return float4(e, a * 0.5 + 0.5, b * 0.5 + 0.5, 1.0); }\n", c.expr);
      ID3DBlob *psb = CompileCs(src, kTargets[t]);
      ID3D11PixelShader *ps = nullptr;
      hr = psb ? dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps) : E_FAIL;
      if (FAILED(hr)) { Out("psmath case=%s target=%s result=FAIL stage=shader hr=0x%08lx", c.name, kTargets[t], (unsigned long)hr); ++failedCases; Rel(ps); Rel(psb); continue; }
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
      if (FAILED(hr)) { Out("psmath case=%s target=%s result=FAIL stage=map hr=0x%08lx", c.name, kTargets[t], (unsigned long)hr); ++failedCases; Rel(ps); Rel(psb); continue; }
      unsigned bad = 0, maxErr = 0, badIn = 0; int fx = -1, fy = -1, fg = 0, fw = 0;
      for (UINT y = 0; y < H; ++y)
        for (UINT x = 0; x < W; ++x) {
          const BYTE *px = (const BYTE *)m.pData + y * m.RowPitch + x * 4;
          double a = (x + 0.5) / 32.0 - 1.0, b = (y + 0.5) / 32.0 - 1.0;
          int want = (int)floor(PmSat(c.want(a, b)) * 255.0 + 0.5);
          unsigned err = (unsigned)abs((int)px[0] - want);
          // green and blue carry the inputs back: tells an input problem from an arithmetic problem
          if (abs((int)px[1] - (int)floor((a * 0.5 + 0.5) * 255.0 + 0.5)) > 2 || abs((int)px[2] - (int)floor((b * 0.5 + 0.5) * 255.0 + 0.5)) > 2) ++badIn;
          if (err > maxErr) maxErr = err;
          if (err > 3) { if (!bad) { fx = (int)x; fy = (int)y; fg = px[0]; fw = want; } ++bad; }
        }
      // a coarse 8x8 picture of red (hex digit = value/16) for the failing cases
      if (bad) {
        for (UINT gy = 0; gy < 8; ++gy) {
          char row[9];
          for (UINT gx = 0; gx < 8; ++gx) row[gx] = "0123456789abcdef"[((const BYTE *)m.pData + (gy * 8 + 4) * m.RowPitch + (gx * 8 + 4) * 4)[0] >> 4];
          row[8] = 0;
          Out("psmath map case=%s target=%s row=%u got=%s", c.name, kTargets[t], gy, row);
        }
        ++failedCases;
      }
      Out("psmath case=%s target=%s bad=%u of=%u max_err=%u bad_inputs=%u first_bad=%d,%d got=%d want=%d result=%s", c.name, kTargets[t], bad,
          W * H, maxErr, badIn, fx, fy, fg, fw, bad ? "FAIL" : "PASS");
      ctx->Unmap(st, 0);
      ++ran;
      Rel(ps); Rel(psb);
    }
  }
  Out("psmath end ran=%u failed_cases=%u removed_reason=0x%08lx result=%s", ran, failedCases,
      (unsigned long)dev->GetDeviceRemovedReason(), failedCases ? "FAIL" : "PASS");
  Rel(st); Rel(rtv); Rel(rt); Rel(vs); Rel(vsb); Rel(ctx); Rel(dev);
}
