// nwgpuprobe "tex" tests (included by nwgpuprobe.cpp). D3D12 only.
// Why: on 2758 + APPLE_AGX_D3D12_CPU_POOL_MAP=0 Cinebench draws plain
// rectangles but no text glyphs and no button art, i.e. textured draws.
//   tex_sample   : DEFAULT texture (committed, and placed in a texture heap),
//                  filled from an UPLOAD buffer by CopyTextureRegion, then
//                  sampled by a pixel shader into an RGBA8 target; formats
//                  R8, A8, RGBA8, BGRA8, BC1, BC3, BC7. The upload buffer is
//                  committed, or placed at +16 MiB in a 32 MiB UPLOAD heap.
//   upload_persist: an UPLOAD buffer mapped once and never unmapped, written
//                  with a new value before each of 5 submissions; read by
//                  (a) CopyBufferRegion to READBACK and (b) a root CBV in a
//                  pixel shader.
//   blend_text   : white target, R8 checker texture used as alpha of a black
//                  quad with SRC_ALPHA/INV_SRC_ALPHA (how glyph atlases are
//                  drawn).
// Each result line: "NWGP tex ... result=PASS|FAIL ...".

static const char *kTexVs =
    "struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "O main(uint id : SV_VertexID) { O o; float2 t = float2((id << 1) & 2, id & 2);\n"
    "  o.uv = t; o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n";
static const char *kTexPs =
    "Texture2D<float4> t0 : register(t0); SamplerState s0 : register(s0);\n"
    "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return t0.Sample(s0, uv); }\n";
static const char *kCbPs =
    "cbuffer C : register(b0) { float4 c; };\n"
    "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return c; }\n";
static const char *kBlendPs =
    "Texture2D<float4> t0 : register(t0); SamplerState s0 : register(s0);\n"
    "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return float4(0, 0, 0, t0.Sample(s0, uv).r); }\n";

static ID3DBlob *CompileStage(const char *src, const char *target) {
  ID3DBlob *code = nullptr, *err = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), "nwgp", nullptr, nullptr, "main", target, 0, 0, &code, &err);
  if (FAILED(hr)) {
    Out("tex compile target=%s hr=0x%08lx msg=\"%.160s\"", target, (unsigned long)hr,
        err ? (const char *)err->GetBufferPointer() : "");
    Rel(err);
    return nullptr;
  }
  Rel(err);
  return code;
}

struct BitW { BYTE *p; UINT pos; void put(UINT v, UINT n) { for (UINT i = 0; i < n; ++i, ++pos) if ((v >> i) & 1u) p[pos >> 3] |= (BYTE)(1u << (pos & 7)); } };

// One 4x4 block (or texel row data) per format that decodes to a known colour.
struct TexFmt { const char *name; DXGI_FORMAT fmt; UINT block; UINT bpb; BYTE want[4]; };
static const TexFmt kFormats[] = {
    {"r8", DXGI_FORMAT_R8_UNORM, 1, 1, {200, 0, 0, 255}},
    {"a8", DXGI_FORMAT_A8_UNORM, 1, 1, {0, 0, 0, 200}},
    {"rgba8", DXGI_FORMAT_R8G8B8A8_UNORM, 1, 4, {10, 120, 230, 255}},
    {"bgra8", DXGI_FORMAT_B8G8R8A8_UNORM, 1, 4, {10, 120, 230, 255}},
    {"bc1", DXGI_FORMAT_BC1_UNORM, 4, 8, {255, 0, 0, 255}},
    {"bc3", DXGI_FORMAT_BC3_UNORM, 4, 16, {0, 255, 0, 255}},
    {"bc7", DXGI_FORMAT_BC7_UNORM, 4, 16, {255, 0, 255, 255}},
};

static void FillElement(const TexFmt &f, BYTE *e) {
  memset(e, 0, f.bpb);
  switch (f.fmt) {
  case DXGI_FORMAT_R8_UNORM: e[0] = 200; break;
  case DXGI_FORMAT_A8_UNORM: e[0] = 200; break;
  case DXGI_FORMAT_R8G8B8A8_UNORM: e[0] = 10; e[1] = 120; e[2] = 230; e[3] = 255; break;
  case DXGI_FORMAT_B8G8R8A8_UNORM: e[0] = 230; e[1] = 120; e[2] = 10; e[3] = 255; break;
  case DXGI_FORMAT_BC1_UNORM: e[0] = 0x00; e[1] = 0xf8; e[2] = 0x00; e[3] = 0xf8; break;  // c0=c1=red565, idx 0
  case DXGI_FORMAT_BC3_UNORM:                                                        // alpha 255, colour green
    e[0] = 255; e[1] = 255; e[8] = 0xe0; e[9] = 0x07; e[10] = 0xe0; e[11] = 0x07; break;
  case DXGI_FORMAT_BC7_UNORM: {                                                      // mode 6, all endpoints (255,0,255,255)
    BitW w = {e, 0};
    w.put(1u << 6, 7);
    w.put(0x7f, 7); w.put(0x7f, 7);  // R0 R1
    w.put(0, 7); w.put(0, 7);        // G0 G1
    w.put(0x7f, 7); w.put(0x7f, 7);  // B0 B1
    w.put(0x7f, 7); w.put(0x7f, 7);  // A0 A1
    w.put(1, 1); w.put(1, 1);        // P0 P1 (G stays 0: (0<<1)|1 = 1)
    break;
  }
  default: break;
  }
}

struct TexPipe {
  ID3D12RootSignature *rs = nullptr;
  ID3D12PipelineState *tex = nullptr, *cb = nullptr, *blend = nullptr;
};

static bool MakeTexPipe(D12 &s, TexPipe &p) {
  ID3DBlob *vs = CompileStage(kTexVs, "vs_5_1"), *ps = CompileStage(kTexPs, "ps_5_1"),
           *cps = CompileStage(kCbPs, "ps_5_1"), *bps = CompileStage(kBlendPs, "ps_5_1");
  ID3DBlob *sig = nullptr, *err = nullptr;
  HRESULT hr = vs && ps && cps && bps ? S_OK : E_FAIL;
  D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
  D3D12_ROOT_PARAMETER rp[2] = {};
  rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rp[0].DescriptorTable.NumDescriptorRanges = 1;
  rp[0].DescriptorTable.pDescriptorRanges = &range;
  rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC ss = {};
  ss.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  ss.MaxLOD = D3D12_FLOAT32_MAX;
  ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC rsd = {2, rp, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (SUCCEEDED(hr)) hr = s.dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&p.rs));
  for (int k = 0; k < 3 && SUCCEEDED(hr); ++k) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = p.rs;
    d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    ID3DBlob *pix = k == 0 ? ps : k == 1 ? cps : bps;
    d.PS = {pix->GetBufferPointer(), pix->GetBufferSize()};
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (k == 2) {
      auto &b = d.BlendState.RenderTarget[0];
      b.BlendEnable = TRUE;
      b.SrcBlend = D3D12_BLEND_SRC_ALPHA; b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; b.BlendOp = D3D12_BLEND_OP_ADD;
      b.SrcBlendAlpha = D3D12_BLEND_ONE; b.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    hr = s.dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(k == 0 ? &p.tex : k == 1 ? &p.cb : &p.blend));
  }
  Out("tex pipeline hr=0x%08lx", (unsigned long)hr);
  Rel(vs); Rel(ps); Rel(cps); Rel(bps); Rel(sig); Rel(err);
  return SUCCEEDED(hr);
}

static void FreeTexPipe(TexPipe &p) { Rel(p.tex); Rel(p.cb); Rel(p.blend); Rel(p.rs); }

static void Barrier(ID3D12GraphicsCommandList *cl, ID3D12Resource *r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
  D3D12_RESOURCE_BARRIER x = {};
  x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  x.Transition.pResource = r;
  x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  x.Transition.StateBefore = a;
  x.Transition.StateAfter = b;
  cl->ResourceBarrier(1, &x);
}

// Render target 64x64 RGBA8 + readback; draws with the given PSO, reads pixels.
struct Target { ID3D12Resource *rt = nullptr, *rb = nullptr; ID3D12DescriptorHeap *rtv = nullptr, *srv = nullptr; };
static const UINT TW = 64, TH = 64;

static bool MakeTarget(D12 &s, Target &t) {
  D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = TW; rd.Height = TH; rd.DepthOrArraySize = 1;
  rd.MipLevels = 1; rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  HRESULT hr = s.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                              IID_PPV_ARGS(&t.rt));
  if (SUCCEEDED(hr)) hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, TW * TH * 4, D3D12_RESOURCE_FLAG_NONE,
                                    D3D12_RESOURCE_STATE_COPY_DEST, &t.rb);
  D3D12_DESCRIPTOR_HEAP_DESC dh = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
  if (SUCCEEDED(hr)) hr = s.dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&t.rtv));
  D3D12_DESCRIPTOR_HEAP_DESC sh = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
  if (SUCCEEDED(hr)) hr = s.dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&t.srv));
  if (SUCCEEDED(hr)) s.dev->CreateRenderTargetView(t.rt, nullptr, t.rtv->GetCPUDescriptorHandleForHeapStart());
  if (FAILED(hr)) Out("tex target hr=0x%08lx", (unsigned long)hr);
  return SUCCEEDED(hr);
}
static void FreeTarget(Target &t) { Rel(t.rt); Rel(t.rb); Rel(t.rtv); Rel(t.srv); }

// Records the draw (cl must be open), copies the target to readback, executes, waits, returns mapped pixels.
static const BYTE *DrawAndRead(D12 &s, Target &t, const TexPipe &p, ID3D12PipelineState *pso, const float clear[4],
                               D3D12_GPU_VIRTUAL_ADDRESS cbv, const char *what) {
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = t.rtv->GetCPUDescriptorHandleForHeapStart();
  s.cl->ClearRenderTargetView(rtv, clear, 0, nullptr);
  s.cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  D3D12_VIEWPORT vp = {0, 0, (float)TW, (float)TH, 0, 1};
  D3D12_RECT sc = {0, 0, (LONG)TW, (LONG)TH};
  s.cl->RSSetViewports(1, &vp);
  s.cl->RSSetScissorRects(1, &sc);
  s.cl->SetGraphicsRootSignature(p.rs);
  ID3D12DescriptorHeap *heaps[] = {t.srv};
  s.cl->SetDescriptorHeaps(1, heaps);
  s.cl->SetGraphicsRootDescriptorTable(0, t.srv->GetGPUDescriptorHandleForHeapStart());
  if (cbv) s.cl->SetGraphicsRootConstantBufferView(1, cbv);
  s.cl->SetPipelineState(pso);
  s.cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  s.cl->DrawInstanced(3, 1, 0, 0);
  Barrier(s.cl, t.rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
  dst.pResource = t.rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, TW, TH, 1, TW * 4};
  src.pResource = t.rt; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  s.cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  Barrier(s.cl, t.rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  HRESULT hr = s.cl->Close();
  if (FAILED(hr)) { Out("tex %s result=FAIL stage=close hr=0x%08lx", what, (unsigned long)hr); return nullptr; }
  ID3D12CommandList *lists[] = {s.cl};
  s.q->ExecuteCommandLists(1, lists);
  if (!D12Wait(s, what)) return nullptr;
  void *m = nullptr;
  D3D12_RANGE r = {0, TW * TH * 4};
  if (FAILED(t.rb->Map(0, &r, &m))) { Out("tex %s result=FAIL stage=map", what); return nullptr; }
  return (const BYTE *)m;
}
static void Unread(Target &t) { D3D12_RANGE none = {0, 0}; t.rb->Unmap(0, &none); }

static UINT CountBad(const BYTE *px, const BYTE want[4], UINT tol) {
  UINT bad = 0;
  for (UINT i = 0; i < TW * TH; ++i)
    for (int c = 0; c < 4; ++c)
      if (abs((int)px[i * 4 + c] - (int)want[c]) > (int)tol) { ++bad; break; }
  return bad;
}

static void TexSampleOne(D12 &s, const TexPipe &p, Target &t, const TexFmt &f, bool placedTex, bool placedUpload) {
  char what[64];
  _snprintf_s(what, sizeof(what), _TRUNCATE, "tex_sample fmt=%s tex=%s upload=%s", f.name,
              placedTex ? "placed" : "committed", placedUpload ? "placed16m" : "committed");
  ID3D12Resource *tex = nullptr, *up = nullptr;
  ID3D12Heap *th = nullptr, *uh = nullptr;
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = TW; rd.Height = TH; rd.DepthOrArraySize = 1;
  rd.MipLevels = 1; rd.Format = f.fmt; rd.SampleDesc.Count = 1;
  HRESULT hr;
  if (placedTex) {
    D3D12_HEAP_DESC hd = {};
    hd.SizeInBytes = 4 * MiB; hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    hr = s.dev->CreateHeap(&hd, IID_PPV_ARGS(&th));
    if (SUCCEEDED(hr)) hr = s.dev->CreatePlacedResource(th, 0, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
  } else {
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
    hr = s.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        IID_PPV_ARGS(&tex));
  }
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
  UINT rows = 0; UINT64 rowBytes = 0, total = 0;
  if (SUCCEEDED(hr)) s.dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowBytes, &total);
  const UINT64 upOffset = placedUpload ? 16 * MiB : 0;
  if (SUCCEEDED(hr)) {
    if (placedUpload) {
      D3D12_HEAP_DESC hd = {};
      hd.SizeInBytes = 32 * MiB; hd.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
      hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
      hr = s.dev->CreateHeap(&hd, IID_PPV_ARGS(&uh));
      D3D12_RESOURCE_DESC bd = {};
      bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1;
      bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      if (SUCCEEDED(hr)) hr = s.dev->CreatePlacedResource(uh, upOffset, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                          IID_PPV_ARGS(&up));
    } else
      hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_UPLOAD, total, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &up);
  }
  if (FAILED(hr)) { Out("%s result=FAIL stage=create hr=0x%08lx", what, (unsigned long)hr); goto done; }
  {
    BYTE *m = nullptr;
    D3D12_RANGE none = {0, 0};
    hr = up->Map(0, &none, (void **)&m);
    if (FAILED(hr)) { Out("%s result=FAIL stage=map hr=0x%08lx", what, (unsigned long)hr); goto done; }
    BYTE elem[16];
    FillElement(f, elem);
    const UINT across = TW / f.block;
    for (UINT r = 0; r < rows; ++r)
      for (UINT x = 0; x < across; ++x) memcpy(m + fp.Offset + (UINT64)r * fp.Footprint.RowPitch + x * f.bpb, elem, f.bpb);
    up->Unmap(0, nullptr);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = f.fmt; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    s.dev->CreateShaderResourceView(tex, &sv, t.srv->GetCPUDescriptorHandleForHeapStart());
    s.ca->Reset();
    s.cl->Reset(s.ca, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = tex; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
    s.cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Barrier(s.cl, tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    const float clear[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    const BYTE *px = DrawAndRead(s, t, p, p.tex, clear, 0, what);
    if (!px) goto done;
    UINT bad = CountBad(px, f.want, 3);
    Out("%s result=%s got=%02x%02x%02x%02x want=%02x%02x%02x%02x bad_pixels=%u", what, bad ? "FAIL" : "PASS", px[0], px[1],
        px[2], px[3], f.want[0], f.want[1], f.want[2], f.want[3], bad);
    Unread(t);
  }
done:
  Rel(up); Rel(uh); Rel(tex); Rel(th);
}

static void UploadPersist(D12 &s, const TexPipe &p, Target &t) {
  {  // the CBV pixel shader reads no texture; keep the table pointing at a null SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    s.dev->CreateShaderResourceView(nullptr, &sv, t.srv->GetCPUDescriptorHandleForHeapStart());
  }
  ID3D12Resource *up = nullptr, *rb = nullptr;
  HRESULT hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_UPLOAD, 65536, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &up);
  if (SUCCEEDED(hr)) hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, 256, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, &rb);
  float *m = nullptr;
  D3D12_RANGE none = {0, 0};
  if (SUCCEEDED(hr)) hr = up->Map(0, &none, (void **)&m);  // never unmapped until the end
  if (FAILED(hr)) { Out("tex upload_persist result=FAIL stage=create hr=0x%08lx", (unsigned long)hr); Rel(up); Rel(rb); return; }
  UINT copyBad = 0, cbBad = 0;
  for (UINT frame = 1; frame <= 5; ++frame) {
    const float v = 0.2f * frame - 0.1f;  // 0.1 .. 0.9
    m[0] = v; m[1] = 1.0f - v; m[2] = 0.5f; m[3] = 1.0f;
    // (a) copy path
    s.ca->Reset();
    s.cl->Reset(s.ca, nullptr);
    s.cl->CopyBufferRegion(rb, 0, up, 0, 16);
    if (FAILED(s.cl->Close())) { ++copyBad; continue; }
    ID3D12CommandList *lists[] = {s.cl};
    s.q->ExecuteCommandLists(1, lists);
    if (!D12Wait(s, "upload_persist_copy")) { ++copyBad; continue; }
    float *r = nullptr;
    D3D12_RANGE rr = {0, 16};
    if (SUCCEEDED(rb->Map(0, &rr, (void **)&r))) {
      if (r[0] != v || r[1] != 1.0f - v) {
        ++copyBad;
        Out("tex upload_persist path=copy frame=%u got=%.3f,%.3f want=%.3f,%.3f", frame, r[0], r[1], v, 1.0f - v);
      }
      rb->Unmap(0, &none);
    } else ++copyBad;
    // (b) root CBV read by a pixel shader
    s.ca->Reset();
    s.cl->Reset(s.ca, nullptr);
    const float clear[4] = {0, 0, 0, 0};
    const BYTE *px = DrawAndRead(s, t, p, p.cb, clear, up->GetGPUVirtualAddress(), "upload_persist_cbv");
    if (!px) { ++cbBad; continue; }
    const BYTE want[4] = {(BYTE)(v * 255.0f + 0.5f), (BYTE)((1.0f - v) * 255.0f + 0.5f), 128, 255};
    if (CountBad(px, want, 2)) {
      ++cbBad;
      Out("tex upload_persist path=cbv frame=%u got=%02x%02x%02x%02x want=%02x%02x%02x%02x", frame, px[0], px[1], px[2],
          px[3], want[0], want[1], want[2], want[3]);
    }
    Unread(t);
  }
  Out("tex upload_persist result=%s copy_bad_frames=%u cbv_bad_frames=%u frames=5", copyBad || cbBad ? "FAIL" : "PASS",
      copyBad, cbBad);
  up->Unmap(0, nullptr);
  Rel(up); Rel(rb);
}

static void BlendText(D12 &s, const TexPipe &p, Target &t) {
  // R8 checker 8x8 cells: 255 = "ink", 0 = background.
  ID3D12Resource *tex = nullptr, *up = nullptr;
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = TW; rd.Height = TH; rd.DepthOrArraySize = 1;
  rd.MipLevels = 1; rd.Format = DXGI_FORMAT_R8_UNORM; rd.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
  HRESULT hr = s.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&tex));
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
  UINT rows = 0; UINT64 rowBytes = 0, total = 0;
  if (SUCCEEDED(hr)) s.dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowBytes, &total);
  if (SUCCEEDED(hr)) hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_UPLOAD, total, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &up);
  BYTE *m = nullptr;
  D3D12_RANGE none = {0, 0};
  if (SUCCEEDED(hr)) hr = up->Map(0, &none, (void **)&m);
  if (FAILED(hr)) { Out("tex blend_text result=FAIL stage=create hr=0x%08lx", (unsigned long)hr); Rel(tex); Rel(up); return; }
  for (UINT y = 0; y < TH; ++y)
    for (UINT x = 0; x < TW; ++x) m[fp.Offset + y * fp.Footprint.RowPitch + x] = (((x >> 3) ^ (y >> 3)) & 1) ? 255 : 0;
  up->Unmap(0, nullptr);
  D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
  sv.Format = rd.Format; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
  s.dev->CreateShaderResourceView(tex, &sv, t.srv->GetCPUDescriptorHandleForHeapStart());
  s.ca->Reset();
  s.cl->Reset(s.ca, nullptr);
  D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
  dst.pResource = tex; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
  s.cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  Barrier(s.cl, tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  const float white[4] = {1, 1, 1, 1};
  const BYTE *px = DrawAndRead(s, t, p, p.blend, white, 0, "blend_text");
  if (px) {
    UINT bad = 0, ink = 0;
    for (UINT y = 0; y < TH; ++y)
      for (UINT x = 0; x < TW; ++x) {
        const BYTE *c = px + (y * TW + x) * 4;
        const bool isInk = (((x >> 3) ^ (y >> 3)) & 1) != 0;
        const int want = isInk ? 0 : 255;
        if (abs(c[0] - want) > 3 || abs(c[1] - want) > 3 || abs(c[2] - want) > 3) ++bad;
        if (c[0] < 16) ++ink;
      }
    Out("tex blend_text result=%s ink_pixels=%u want_ink=%u bad_pixels=%u first=%02x%02x%02x%02x", bad ? "FAIL" : "PASS", ink,
        TW * TH / 2, bad, px[0], px[1], px[2], px[3]);
    Unread(t);
  }
  Rel(tex); Rel(up);
}

static void TestTex() {
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  TexPipe p;
  Target t;
  if (MakeTexPipe(s, p) && MakeTarget(s, t)) {
    for (const TexFmt &f : kFormats) {
      TexSampleOne(s, p, t, f, false, false);
      TexSampleOne(s, p, t, f, true, false);
      TexSampleOne(s, p, t, f, false, true);
    }
    UploadPersist(s, p, t);
    BlendText(s, p, t);
  }
  FreeTarget(t);
  FreeTexPipe(p);
  D12Close(s);
}
