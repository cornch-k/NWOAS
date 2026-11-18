// nwgpuprobe "ui" tests (included after nwgpuprobe_tex.inc). D3D12 only.
// Why: on 2759 Cinebench draws text but not its buttons/widget art. These
// tests cover the draw features a UI renderer uses that "tex" does not:
// vertex+index buffers with an input layout, instancing, a sampler from a
// descriptor heap with linear filtering, root constants, descriptor-table
// indexing, scissor, 4x MSAA + ResolveSubresource, RGBA16F and R11G11B10
// targets, mip level selection (explicit and by derivatives), texture
// arrays, SV_Position + discard + fwidth (rounded shapes), dual-source
// blending. Every test draws into a 64x64 target and reads it back.

static const char *kUiCommon =
    "Texture2D<float4> tx[4] : register(t0); Texture2DArray<float4> ta : register(t4);\n"
    "SamplerState sp : register(s0); SamplerState sh : register(s1);\n"
    "cbuffer RC : register(b1) { float4 rc; uint4 ri; };\n"
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; float4 c : COLOR0; };\n";
static const char *kUiVsFull =
    "V main(uint id : SV_VertexID) { V o; float2 t = float2((id << 1) & 2, id & 2);\n"
    "  o.uv = t; o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); o.c = 1; return o; }\n";
static const char *kUiVsIa =
    "struct I { float2 pos : POSITION; float4 col : COLOR; uint inst : SV_InstanceID; };\n"
    "V main(I i) { V o; o.p = float4(i.pos.x * 0.5 + (i.inst ? 0.5 : -0.5), i.pos.y, 0, 1);\n"
    "  o.uv = i.pos * 0.5 + 0.5; o.c = i.col; return o; }\n";

struct UiTest { const char *name; const char *vs; const char *ps; };
static const UiTest kUi[] = {
    {"ia_indexed_instanced", kUiVsIa, "float4 main(V v) : SV_Target { return v.c; }"},
    {"heap_sampler_linear", kUiVsFull, "float4 main(V v) : SV_Target { return tx[0].Sample(sh, v.uv); }"},
    {"root_constants", kUiVsFull, "float4 main(V v) : SV_Target { return rc; }"},
    {"table_index", kUiVsFull, "float4 main(V v) : SV_Target { return tx[ri.x].Sample(sp, v.uv); }"},
    {"scissor", kUiVsFull, "float4 main(V v) : SV_Target { return float4(1, 0, 0, 1); }"},
    {"msaa4x_resolve", kUiVsFull, "float4 main(V v) : SV_Target { return float4(0, 1, 0, 1); }"},
    {"rt_rgba16f", kUiVsFull, "float4 main(V v) : SV_Target { return float4(0.25, 0.5, 0.75, 1); }"},
    {"rt_r11g11b10", kUiVsFull, "float4 main(V v) : SV_Target { return float4(0.25, 0.5, 0.75, 1); }"},
    {"mip_level1", kUiVsFull, "float4 main(V v) : SV_Target { return tx[1].SampleLevel(sp, v.uv, 1); }"},
    {"mip_by_derivative", kUiVsFull, "float4 main(V v) : SV_Target { return tx[1].Sample(sp, v.uv); }"},
    {"texture_array_slice1", kUiVsFull, "float4 main(V v) : SV_Target { return ta.Sample(sp, float3(v.uv, 1)); }"},
    {"sdf_discard_fwidth", kUiVsFull,
     "float4 main(V v) : SV_Target { float d = length(v.p.xy - 32.0) - 20.0; if (d > 0.5) discard;\n"
     "  float a = saturate(0.5 - d / max(fwidth(d), 1e-4)); return float4(1, 1, 1, a); }"},
    {"dual_source_blend", kUiVsFull,
     "struct O { float4 c0 : SV_Target0; float4 c1 : SV_Target1; };\n"
     "O main(V v) { O o; o.c0 = float4(1, 1, 1, 1); o.c1 = float4(0.5, 0.25, 1, 1); return o; }"},
};

struct UiRes {
  ID3D12RootSignature *rs = nullptr;
  ID3D12DescriptorHeap *srv = nullptr, *smp = nullptr;
  ID3D12Resource *tex[5] = {};  // 0 white, 1 mip chain red/green, 2 grey, 3 blue, 4 array red/blue
  ID3D12Resource *vb = nullptr, *ib = nullptr;
};

static HRESULT UiRootSig(D12 &s, UiRes &u) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, 0},
                                      {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 1, 0, 0}};
  D3D12_ROOT_PARAMETER rp[3] = {};
  rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rp[0].DescriptorTable = {1, &ranges[0]};
  rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rp[1].DescriptorTable = {1, &ranges[1]};
  rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  rp[2].Constants = {1, 0, 8};
  for (auto &p : rp) p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_STATIC_SAMPLER_DESC ss = {};
  ss.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  ss.MaxLOD = D3D12_FLOAT32_MAX;
  ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC d = {3, rp, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  ID3DBlob *sig = nullptr, *err = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (SUCCEEDED(hr)) hr = s.dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&u.rs));
  Rel(sig); Rel(err);
  return hr;
}

// Creates a DEFAULT texture with `mips` levels and `layers` slices of RGBA8, each
// (level,layer) a solid colour from `colors[layer * mips + level]`, uploaded and
// left in PIXEL_SHADER_RESOURCE.
static HRESULT UiTexture(D12 &s, UINT mips, UINT layers, const UINT32 *colors, ID3D12Resource **out) {
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = 64; rd.Height = 64; rd.DepthOrArraySize = (UINT16)layers;
  rd.MipLevels = (UINT16)mips; rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
  HRESULT hr = s.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(out));
  if (FAILED(hr)) return hr;
  const UINT n = mips * layers;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[16] = {};
  UINT rows[16] = {}; UINT64 rb[16] = {}, total = 0;
  s.dev->GetCopyableFootprints(&rd, 0, n, 0, fp, rows, rb, &total);
  ID3D12Resource *up = nullptr;
  hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_UPLOAD, total, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &up);
  BYTE *m = nullptr;
  D3D12_RANGE none = {0, 0};
  if (SUCCEEDED(hr)) hr = up->Map(0, &none, (void **)&m);
  if (FAILED(hr)) { Rel(up); return hr; }
  for (UINT i = 0; i < n; ++i)
    for (UINT r = 0; r < rows[i]; ++r)
      for (UINT x = 0; x < fp[i].Footprint.Width; ++x)
        memcpy(m + fp[i].Offset + (UINT64)r * fp[i].Footprint.RowPitch + x * 4, &colors[i], 4);
  up->Unmap(0, nullptr);
  s.ca->Reset();
  s.cl->Reset(s.ca, nullptr);
  for (UINT i = 0; i < n; ++i) {
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = *out; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = i;
    src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp[i];
    s.cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  }
  Barrier(s.cl, *out, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  hr = s.cl->Close();
  if (SUCCEEDED(hr)) {
    ID3D12CommandList *lists[] = {s.cl};
    s.q->ExecuteCommandLists(1, lists);
    if (!D12Wait(s, "ui_texture")) hr = E_FAIL;
  }
  Rel(up);
  return hr;
}

static bool UiSetup(D12 &s, UiRes &u) {
  HRESULT hr = UiRootSig(s, u);
  D3D12_DESCRIPTOR_HEAP_DESC sh = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
  if (SUCCEEDED(hr)) hr = s.dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&u.srv));
  D3D12_DESCRIPTOR_HEAP_DESC sm = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
  if (SUCCEEDED(hr)) hr = s.dev->CreateDescriptorHeap(&sm, IID_PPV_ARGS(&u.smp));
  // colours are little-endian RGBA bytes
  const UINT32 white = 0xffffffffu, red = 0xff0000ffu, green = 0xff00ff00u, grey = 0xff808080u, blue = 0xffff0000u;
  const UINT32 c0[] = {white}, c1[] = {red, green, red, red, red, red, red}, c2[] = {grey}, c3[] = {blue},
               c4[] = {red, blue};
  if (SUCCEEDED(hr)) hr = UiTexture(s, 1, 1, c0, &u.tex[0]);
  if (SUCCEEDED(hr)) hr = UiTexture(s, 7, 1, c1, &u.tex[1]);
  if (SUCCEEDED(hr)) hr = UiTexture(s, 1, 1, c2, &u.tex[2]);
  if (SUCCEEDED(hr)) hr = UiTexture(s, 1, 1, c3, &u.tex[3]);
  if (SUCCEEDED(hr)) hr = UiTexture(s, 1, 2, c4, &u.tex[4]);
  if (SUCCEEDED(hr)) {
    const UINT inc = s.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = u.srv->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 4; ++i, h.ptr += inc) s.dev->CreateShaderResourceView(u.tex[i], nullptr, h);
    D3D12_SHADER_RESOURCE_VIEW_DESC av = {};
    av.Format = DXGI_FORMAT_R8G8B8A8_UNORM; av.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    av.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    av.Texture2DArray.MipLevels = 1; av.Texture2DArray.ArraySize = 2;
    s.dev->CreateShaderResourceView(u.tex[4], &av, h);
    D3D12_SAMPLER_DESC sd = {};
    sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sd.MaxLOD = D3D12_FLOAT32_MAX;
    s.dev->CreateSampler(&sd, u.smp->GetCPUDescriptorHandleForHeapStart());
    // two instances of a unit quad: 4 vertices (pos, colour), 6 16-bit indices
    struct Vtx { float x, y, r, g, b, a; } v[4] = {{-1, -1, 0, 0, 1, 1}, {-1, 1, 0, 0, 1, 1}, {1, 1, 0, 0, 1, 1}, {1, -1, 0, 0, 1, 1}};
    const UINT16 idx[6] = {0, 1, 2, 0, 2, 3};
    hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_UPLOAD, sizeof(v), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &u.vb);
    if (SUCCEEDED(hr)) hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_UPLOAD, sizeof(idx), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &u.ib);
    void *m = nullptr;
    D3D12_RANGE none = {0, 0};
    if (SUCCEEDED(hr) && SUCCEEDED(u.vb->Map(0, &none, &m))) { memcpy(m, v, sizeof(v)); u.vb->Unmap(0, nullptr); }
    if (SUCCEEDED(hr) && SUCCEEDED(u.ib->Map(0, &none, &m))) { memcpy(m, idx, sizeof(idx)); u.ib->Unmap(0, nullptr); }
  }
  Out("ui setup hr=0x%08lx", (unsigned long)hr);
  return SUCCEEDED(hr);
}

static void UiFree(UiRes &u) {
  for (auto &t : u.tex) Rel(t);
  Rel(u.vb); Rel(u.ib); Rel(u.srv); Rel(u.smp); Rel(u.rs);
}

// Expected colour check helpers on an RGBA8 64x64 readback.
static bool Px(const BYTE *px, UINT x, UINT y, int r, int g, int b, int tol) {
  const BYTE *c = px + (y * 64 + x) * 4;
  return abs(c[0] - r) <= tol && abs(c[1] - g) <= tol && abs(c[2] - b) <= tol;
}

static void UiRun(D12 &s, UiRes &u, const UiTest &t) {
  const bool msaa = !strcmp(t.name, "msaa4x_resolve");
  const DXGI_FORMAT rtFmt = !strcmp(t.name, "rt_rgba16f") ? DXGI_FORMAT_R16G16B16A16_FLOAT
                          : !strcmp(t.name, "rt_r11g11b10") ? DXGI_FORMAT_R11G11B10_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
  const UINT bpp = rtFmt == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
  ID3DBlob *vs = nullptr, *ps = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12Resource *rt = nullptr, *res = nullptr, *rb = nullptr;
  ID3D12DescriptorHeap *rtvh = nullptr;
  std::string vsrc = std::string(kUiCommon) + t.vs, psrc = std::string(kUiCommon) + t.ps;
  vs = CompileStage(vsrc.c_str(), "vs_5_1");
  ps = CompileStage(psrc.c_str(), "ps_5_1");
  HRESULT hr = vs && ps ? S_OK : E_FAIL;
  D3D12_INPUT_ELEMENT_DESC il[2] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
  if (SUCCEEDED(hr)) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = u.rs;
    d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    if (t.vs == kUiVsIa) d.InputLayout = {il, 2};
    auto &b = d.BlendState.RenderTarget[0];
    b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (!strcmp(t.name, "sdf_discard_fwidth")) {
      b.BlendEnable = TRUE; b.SrcBlend = D3D12_BLEND_SRC_ALPHA; b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; b.BlendOp = D3D12_BLEND_OP_ADD;
      b.SrcBlendAlpha = D3D12_BLEND_ONE; b.DestBlendAlpha = D3D12_BLEND_ZERO; b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    } else if (!strcmp(t.name, "dual_source_blend")) {
      b.BlendEnable = TRUE; b.SrcBlend = D3D12_BLEND_SRC1_COLOR; b.DestBlend = D3D12_BLEND_ZERO; b.BlendOp = D3D12_BLEND_OP_ADD;
      b.SrcBlendAlpha = D3D12_BLEND_ONE; b.DestBlendAlpha = D3D12_BLEND_ZERO; b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = rtFmt;
    d.SampleDesc.Count = msaa ? 4 : 1;
    hr = s.dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) Out("ui %s result=FAIL stage=pso hr=0x%08lx", t.name, (unsigned long)hr);
  }
  D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = 64; rd.Height = 64; rd.DepthOrArraySize = 1;
  rd.MipLevels = 1; rd.Format = rtFmt; rd.SampleDesc.Count = msaa ? 4 : 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (SUCCEEDED(hr)) hr = s.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&rt));
  if (SUCCEEDED(hr) && msaa) {
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = s.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr, IID_PPV_ARGS(&res));
  }
  if (SUCCEEDED(hr)) hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, 64 * 64 * bpp, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, &rb);
  D3D12_DESCRIPTOR_HEAP_DESC dh = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
  if (SUCCEEDED(hr)) hr = s.dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&rtvh));
  if (FAILED(hr)) { if (pso) Out("ui %s result=FAIL stage=create hr=0x%08lx", t.name, (unsigned long)hr); goto done; }
  {
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvh->GetCPUDescriptorHandleForHeapStart();
    s.dev->CreateRenderTargetView(rt, nullptr, rtv);
    s.ca->Reset();
    s.cl->Reset(s.ca, pso);
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    s.cl->ClearRenderTargetView(rtv, clear, 0, nullptr);
    s.cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const bool minify = !strcmp(t.name, "mip_by_derivative");
    D3D12_VIEWPORT vp = {0, 0, minify ? 32.0f : 64.0f, minify ? 32.0f : 64.0f, 0, 1};
    D3D12_RECT sc = !strcmp(t.name, "scissor") ? D3D12_RECT{16, 16, 48, 48} : D3D12_RECT{0, 0, 64, 64};
    s.cl->RSSetViewports(1, &vp);
    s.cl->RSSetScissorRects(1, &sc);
    s.cl->SetGraphicsRootSignature(u.rs);
    ID3D12DescriptorHeap *heaps[] = {u.srv, u.smp};
    s.cl->SetDescriptorHeaps(2, heaps);
    s.cl->SetGraphicsRootDescriptorTable(0, u.srv->GetGPUDescriptorHandleForHeapStart());
    s.cl->SetGraphicsRootDescriptorTable(1, u.smp->GetGPUDescriptorHandleForHeapStart());
    const float rcv[4] = {0.2f, 0.4f, 0.6f, 1.0f};
    const UINT ri[4] = {3, 0, 0, 0};
    s.cl->SetGraphicsRoot32BitConstants(2, 4, rcv, 0);
    s.cl->SetGraphicsRoot32BitConstants(2, 4, ri, 4);
    s.cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (t.vs == kUiVsIa) {
      D3D12_VERTEX_BUFFER_VIEW vbv = {u.vb->GetGPUVirtualAddress(), 4 * 24, 24};
      D3D12_INDEX_BUFFER_VIEW ibv = {u.ib->GetGPUVirtualAddress(), 12, DXGI_FORMAT_R16_UINT};
      s.cl->IASetVertexBuffers(0, 1, &vbv);
      s.cl->IASetIndexBuffer(&ibv);
      s.cl->DrawIndexedInstanced(6, 2, 0, 0, 0);
    } else
      s.cl->DrawInstanced(3, 1, 0, 0);
    ID3D12Resource *src = rt;
    if (msaa) {
      Barrier(s.cl, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
      s.cl->ResolveSubresource(res, 0, rt, 0, rtFmt);
      Barrier(s.cl, res, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
      src = res;
    } else
      Barrier(s.cl, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, sl = {};
    dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint = {rtFmt, 64, 64, 1, 64 * bpp};
    sl.pResource = src; sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    s.cl->CopyTextureRegion(&dst, 0, 0, 0, &sl, nullptr);
    hr = s.cl->Close();
    if (FAILED(hr)) { Out("ui %s result=FAIL stage=close hr=0x%08lx", t.name, (unsigned long)hr); goto done; }
    ID3D12CommandList *lists[] = {s.cl};
    s.q->ExecuteCommandLists(1, lists);
    if (!D12Wait(s, t.name)) goto done;
    BYTE *m = nullptr;
    D3D12_RANGE r = {0, 64 * 64 * bpp};
    if (FAILED(rb->Map(0, &r, (void **)&m))) { Out("ui %s result=FAIL stage=map", t.name); goto done; }
    bool ok = false;
    char got[64] = "";
    if (rtFmt == DXGI_FORMAT_R16G16B16A16_FLOAT) {
      const UINT16 *h = (const UINT16 *)(m + (32 * 64 + 32) * 8);
      ok = h[0] == 0x3400 && h[1] == 0x3800 && h[2] == 0x3a00 && h[3] == 0x3c00;  // 0.25 0.5 0.75 1
      _snprintf_s(got, sizeof(got), _TRUNCATE, "%04x%04x%04x%04x", h[0], h[1], h[2], h[3]);
    } else if (rtFmt == DXGI_FORMAT_R11G11B10_FLOAT) {
      const UINT32 v = *(const UINT32 *)(m + (32 * 64 + 32) * 4);
      // 0.25 -> r11 0x1a0, 0.5 -> g11 0x1c0, 0.75 -> b10 0x1e0 >> ... compare decoded within 1 %
      const UINT ri11 = v & 0x7ff, gi11 = (v >> 11) & 0x7ff, bi10 = v >> 22;
      auto dec = [](UINT bits, UINT mant) { UINT e = bits >> mant, f = bits & ((1u << mant) - 1);
        return e ? ldexp(1.0 + f / (double)(1u << mant), (int)e - 15) : ldexp(f / (double)(1u << mant), -14); };
      const double R = dec(ri11, 6), G = dec(gi11, 6), B = dec(bi10, 5);
      ok = fabs(R - 0.25) < 0.01 && fabs(G - 0.5) < 0.01 && fabs(B - 0.75) < 0.01;
      _snprintf_s(got, sizeof(got), _TRUNCATE, "%.3f,%.3f,%.3f", R, G, B);
    } else {
      const char *n = t.name;
      if (!strcmp(n, "ia_indexed_instanced")) ok = Px(m, 8, 8, 0, 0, 255, 2) && Px(m, 56, 56, 0, 0, 255, 2);
      else if (!strcmp(n, "heap_sampler_linear")) ok = Px(m, 32, 32, 255, 255, 255, 2);
      else if (!strcmp(n, "root_constants")) ok = Px(m, 32, 32, 51, 102, 153, 2);
      else if (!strcmp(n, "table_index")) ok = Px(m, 32, 32, 0, 0, 255, 2);
      else if (!strcmp(n, "scissor")) ok = Px(m, 32, 32, 255, 0, 0, 2) && Px(m, 4, 4, 0, 0, 0, 2) && Px(m, 60, 60, 0, 0, 0, 2);
      else if (!strcmp(n, "msaa4x_resolve")) ok = Px(m, 32, 32, 0, 255, 0, 2);
      else if (!strcmp(n, "mip_level1")) ok = Px(m, 32, 32, 0, 255, 0, 2);
      else if (!strcmp(n, "mip_by_derivative")) ok = Px(m, 16, 16, 0, 255, 0, 2);
      else if (!strcmp(n, "texture_array_slice1")) ok = Px(m, 32, 32, 0, 0, 255, 2);
      else if (!strcmp(n, "sdf_discard_fwidth")) ok = Px(m, 32, 32, 255, 255, 255, 2) && Px(m, 2, 2, 0, 0, 0, 2);
      else if (!strcmp(n, "dual_source_blend")) ok = Px(m, 32, 32, 128, 64, 255, 2);
      const BYTE *c = m + (32 * 64 + 32) * 4;
      _snprintf_s(got, sizeof(got), _TRUNCATE, "%02x%02x%02x%02x", c[0], c[1], c[2], c[3]);
    }
    Out("ui %s result=%s centre=%s", t.name, ok ? "PASS" : "FAIL", got);
    D3D12_RANGE none = {0, 0};
    rb->Unmap(0, &none);
  }
done:
  Rel(pso); Rel(rt); Rel(res); Rel(rb); Rel(rtvh); Rel(vs); Rel(ps);
}

static void TestUi() {
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  UiRes u;
  if (UiSetup(s, u))
    for (const UiTest &t : kUi) UiRun(s, u, t);
  UiFree(u);
  D12Close(s);
}
