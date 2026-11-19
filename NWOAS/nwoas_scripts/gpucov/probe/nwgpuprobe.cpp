// nwgpuprobe: GPU coverage probe for NWOAS (Windows 11 ARM64 on Mac mini M1).
// Reports what the installed display driver really does: DXGI adapters and
// budgets, D3D12/D3D11 device creation and caps, heap/buffer/texture size
// ladders, render-target clear + readback, compute dispatch + readback,
// hardware video decode profiles, and swapchain present (needs the user
// session). Every result is one ASCII line "NWGP <test> <key=value...>".
//
// Usage: nwgpuprobe.exe [all|dxgi|d3d12|d3d11|present|tex|ui|interop|d3d11ui|modeswitch|chromium|umd|draw11|fill] [warp] ...
//   umd, draw11 (probe10, P37): which AppleAgx UMD image this process runs on + one D3D11 draw; not in "all".
//   pool, memladder, fill2, d3d11mem (probe11, S265): GPU memory limit with the pool window; not in "all"
//   (nwgpuprobe_mem.inc).
//   memladder2 [ladder=a,b,..], fill3, rcpt (probe12, P38 / 2774): fresh device per size, create/removal timing,
//   first/last-chunk probe copies, per-heap tag readback, KMD receipts (nwgpuprobe_mem2.inc); not in "all".
//   probe13 (after 2774 K43c): memladder2 [settle=<ms>] adds a "pre" line per size, the removed reason after each
//   probe copy, and more KMD receipts in rcpt (nwgpuprobe_mem2.inc header).
//   all (default) = dxgi d3d12 d3d11 present tex ui interop. "fill" allocates 64 MiB DEFAULT
//   heaps until failure (capped at 1 GiB) and is never part of "all".
//
// Build (VM): see build-probe.cmd. Only Windows SDK headers/libs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <utility>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

template <class T> static void Rel(T *&p) { if (p) { p->Release(); p = nullptr; } }

static void Out(const char *fmt, ...) {
  char line[1024];
  va_list ap;
  va_start(ap, fmt);
  _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
  va_end(ap);
  fputs("NWGP ", stdout);
  fputs(line, stdout);
  fputc('\n', stdout);
  fflush(stdout);
}

static const char *Arch() {
#if defined(_M_ARM64EC)
  return "arm64ec";
#elif defined(_M_ARM64)
  return "arm64";
#elif defined(_M_X64)
  return "x64";
#elif defined(_M_IX86)
  return "x86";
#else
  return "unknown";
#endif
}

static void Narrow(const WCHAR *w, char *a, size_t n) {
  size_t i = 0;
  for (; w[i] && i + 1 < n; ++i) a[i] = (w[i] >= 32 && w[i] < 127) ? (char)w[i] : '?';
  a[i] = 0;
}

static const UINT64 MiB = 1024ull * 1024ull;
static bool g_warp = false;  // "warp": run the D3D12 tests on WARP
static const UINT kLadderMiB[] = {1, 8, 15, 16, 17, 24, 32, 64, 128, 256, 512};

// ---------------------------------------------------------------- DXGI
static IDXGIAdapter1 *PickHardwareAdapter(IDXGIFactory1 *f) {
  IDXGIAdapter1 *a = nullptr;
  for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 d = {};
    a->GetDesc1(&d);
    if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) return a;
    Rel(a);
  }
  return nullptr;
}

static void TestDxgi() {
  IDXGIFactory1 *f = nullptr;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&f));
  Out("dxgi factory hr=0x%08lx", (unsigned long)hr);
  if (FAILED(hr)) return;
  IDXGIAdapter1 *a = nullptr;
  for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 d = {};
    a->GetDesc1(&d);
    char name[128];
    Narrow(d.Description, name, sizeof(name));
    Out("dxgi adapter=%u name=\"%s\" vendor=0x%04x device=0x%04x software=%u dedicated_vram_mib=%llu dedicated_sys_mib=%llu shared_sys_mib=%llu",
        i, name, d.VendorId, d.DeviceId, (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? 1u : 0u,
        (unsigned long long)(d.DedicatedVideoMemory / MiB), (unsigned long long)(d.DedicatedSystemMemory / MiB),
        (unsigned long long)(d.SharedSystemMemory / MiB));
    IDXGIAdapter3 *a3 = nullptr;
    if (SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&a3)))) {
      for (int g = 0; g < 2; ++g) {
        DXGI_QUERY_VIDEO_MEMORY_INFO m = {};
        HRESULT q = a3->QueryVideoMemoryInfo(0, g ? DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &m);
        Out("dxgi adapter=%u group=%s hr=0x%08lx budget_mib=%llu usage_mib=%llu avail_reserve_mib=%llu", i,
            g ? "nonlocal" : "local", (unsigned long)q, (unsigned long long)(m.Budget / MiB),
            (unsigned long long)(m.CurrentUsage / MiB), (unsigned long long)(m.AvailableForReservation / MiB));
      }
      Rel(a3);
    }
    Rel(a);
  }
  Rel(f);
}

// ---------------------------------------------------------------- D3D12
struct D12 {
  ID3D12Device *dev = nullptr;
  ID3D12CommandQueue *q = nullptr;
  ID3D12CommandAllocator *ca = nullptr;
  ID3D12GraphicsCommandList *cl = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE ev = nullptr;
  UINT64 fv = 0;
};

static bool D12Wait(D12 &s, const char *what) {
  ++s.fv;
  HRESULT hr = s.q->Signal(s.fence, s.fv);
  if (FAILED(hr)) { Out("d3d12 %s signal hr=0x%08lx", what, (unsigned long)hr); return false; }
  if (s.fence->GetCompletedValue() < s.fv) {
    s.fence->SetEventOnCompletion(s.fv, s.ev);
    if (WaitForSingleObject(s.ev, 5000) != WAIT_OBJECT_0) {
      Out("d3d12 %s result=TIMEOUT removed=0x%08lx", what, (unsigned long)s.dev->GetDeviceRemovedReason());
      return false;
    }
  }
  return true;
}

static HRESULT D12Buffer(ID3D12Device *dev, D3D12_HEAP_TYPE type, UINT64 bytes, D3D12_RESOURCE_FLAGS flags,
                         D3D12_RESOURCE_STATES state, ID3D12Resource **out) {
  D3D12_HEAP_PROPERTIES hp = {};
  hp.Type = type;
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = bytes;
  rd.Height = 1;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  rd.Flags = flags;
  return dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(out));
}

static ID3DBlob *CompileCs(const char *src, const char *target) {
  ID3DBlob *code = nullptr, *err = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), "nwgp", nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                          &code, &err);
  if (FAILED(hr)) {
    Out("compile target=%s hr=0x%08lx msg=\"%.200s\"", target, (unsigned long)hr,
        err ? (const char *)err->GetBufferPointer() : "");
    Rel(err);
    return nullptr;
  }
  Rel(err);
  return code;
}

static const char *kCsSource =
    "RWByteAddressBuffer o : register(u0);\n"
    "[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) { o.Store(id.x * 4, id.x * 3 + 1); }\n";

static void D12Caps(ID3D12Device *dev) {
  static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                             D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D12_FEATURE_DATA_FEATURE_LEVELS fl = {};
  fl.NumFeatureLevels = ARRAYSIZE(levels);
  fl.pFeatureLevelsRequested = levels;
  HRESULT hr = dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl));
  Out("d3d12 caps feature_level hr=0x%08lx max=0x%x", (unsigned long)hr, (unsigned)fl.MaxSupportedFeatureLevel);
  D3D12_FEATURE_DATA_SHADER_MODEL sm = {};
  static const D3D_SHADER_MODEL models[] = {(D3D_SHADER_MODEL)0x67, (D3D_SHADER_MODEL)0x66, D3D_SHADER_MODEL_6_5,
                                            D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2,
                                            D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0, D3D_SHADER_MODEL_5_1};
  hr = E_FAIL;
  for (D3D_SHADER_MODEL m : models) {
    sm.HighestShaderModel = m;
    hr = dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm));
    if (SUCCEEDED(hr)) break;
  }
  Out("d3d12 caps shader_model hr=0x%08lx highest=0x%x", (unsigned long)hr, (unsigned)sm.HighestShaderModel);
  D3D12_FEATURE_DATA_D3D12_OPTIONS o = {};
  hr = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o));
  Out("d3d12 caps options hr=0x%08lx binding_tier=%d tiled_tier=%d heap_tier=%d typed_uav_ext=%d rov=%d cons_raster=%d",
      (unsigned long)hr, (int)o.ResourceBindingTier, (int)o.TiledResourcesTier, (int)o.ResourceHeapTier,
      (int)o.TypedUAVLoadAdditionalFormats, (int)o.ROVsSupported, (int)o.ConservativeRasterizationTier);
  D3D12_FEATURE_DATA_ARCHITECTURE1 ar = {};
  hr = dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &ar, sizeof(ar));
  Out("d3d12 caps architecture hr=0x%08lx uma=%d cache_coherent_uma=%d isolated_mmu=%d tile_based=%d", (unsigned long)hr,
      (int)ar.UMA, (int)ar.CacheCoherentUMA, (int)ar.IsolatedMMU, (int)ar.TileBasedRenderer);
  D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT va = {};
  hr = dev->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &va, sizeof(va));
  Out("d3d12 caps gpuva hr=0x%08lx bits_per_resource=%u bits_per_process=%u", (unsigned long)hr,
      va.MaxGPUVirtualAddressBitsPerResource, va.MaxGPUVirtualAddressBitsPerProcess);
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5 = {};
  hr = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5));
  Out("d3d12 caps options5 hr=0x%08lx raytracing_tier=%d render_passes_tier=%d", (unsigned long)hr,
      (int)o5.RaytracingTier, (int)o5.RenderPassesTier);
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7 = {};
  hr = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof(o7));
  Out("d3d12 caps options7 hr=0x%08lx mesh_shader_tier=%d sampler_feedback_tier=%d", (unsigned long)hr,
      (int)o7.MeshShaderTier, (int)o7.SamplerFeedbackTier);
}

static void D12HeapLadder(ID3D12Device *dev) {
  struct Kind { const char *name; D3D12_HEAP_TYPE type; D3D12_HEAP_FLAGS flags; } kinds[] = {
      {"default_buffers", D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS},
      {"default_textures", D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES},
      {"default_rtds", D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES},
      {"upload_buffers", D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS},
      {"readback_buffers", D3D12_HEAP_TYPE_READBACK, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS},
      {"default_all_needs_heap_tier2", D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_NONE},
  };
  for (const Kind &k : kinds) {
    UINT largestOk = 0, firstFail = 0;
    HRESULT failHr = S_OK;
    for (UINT mib : kLadderMiB) {
      D3D12_HEAP_DESC hd = {};
      hd.SizeInBytes = mib * MiB;
      hd.Properties.Type = k.type;
      hd.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      hd.Flags = k.flags;
      ID3D12Heap *h = nullptr;
      HRESULT hr = dev->CreateHeap(&hd, IID_PPV_ARGS(&h));
      Out("d3d12 heap kind=%s mib=%u hr=0x%08lx", k.name, mib, (unsigned long)hr);
      if (SUCCEEDED(hr)) largestOk = mib;
      else if (!firstFail) { firstFail = mib; failHr = hr; }
      Rel(h);
    }
    Out("d3d12 heap_summary kind=%s largest_ok_mib=%u first_fail_mib=%u first_fail_hr=0x%08lx", k.name, largestOk,
        firstFail, (unsigned long)failHr);
  }
  struct BKind { const char *name; D3D12_HEAP_TYPE type; D3D12_RESOURCE_STATES state; } bkinds[] = {
      {"default", D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON},
      {"upload", D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ},
      {"readback", D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST},
  };
  for (const BKind &k : bkinds) {
    UINT largestOk = 0, firstFail = 0;
    for (UINT mib : kLadderMiB) {
      ID3D12Resource *r = nullptr;
      HRESULT hr = D12Buffer(dev, k.type, mib * MiB, D3D12_RESOURCE_FLAG_NONE, k.state, &r);
      Out("d3d12 committed_buffer kind=%s mib=%u hr=0x%08lx", k.name, mib, (unsigned long)hr);
      if (SUCCEEDED(hr)) largestOk = mib; else if (!firstFail) firstFail = mib;
      Rel(r);
    }
    Out("d3d12 committed_buffer_summary kind=%s largest_ok_mib=%u first_fail_mib=%u", k.name, largestOk, firstFail);
  }
  static const UINT dims[] = {1024, 2048, 2896, 4096, 8192, 16384};
  for (int rt = 0; rt < 2; ++rt) {
    for (UINT d : dims) {
      D3D12_HEAP_PROPERTIES hp = {};
      hp.Type = D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC rd = {};
      rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      rd.Width = d;
      rd.Height = d;
      rd.DepthOrArraySize = 1;
      rd.MipLevels = 1;
      rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      rd.SampleDesc.Count = 1;
      rd.Flags = rt ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
      ID3D12Resource *r = nullptr;
      HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                rt ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COMMON,
                                                nullptr, IID_PPV_ARGS(&r));
      Out("d3d12 committed_texture rgba8=%ux%u rt=%d mib=%llu hr=0x%08lx", d, d, rt,
          (unsigned long long)(4ull * d * d / MiB), (unsigned long)hr);
      Rel(r);
    }
  }
}

static void D12Clear(D12 &s) {
  const UINT W = 256, H = 256;
  D3D12_HEAP_PROPERTIES hp = {};
  hp.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = W; rd.Height = H; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  ID3D12Resource *tex = nullptr, *rb = nullptr;
  ID3D12DescriptorHeap *rtv = nullptr;
  HRESULT hr = s.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                              IID_PPV_ARGS(&tex));
  if (SUCCEEDED(hr)) hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, W * H * 4, D3D12_RESOURCE_FLAG_NONE,
                                    D3D12_RESOURCE_STATE_COPY_DEST, &rb);
  D3D12_DESCRIPTOR_HEAP_DESC dh = {};
  dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; dh.NumDescriptors = 1;
  if (SUCCEEDED(hr)) hr = s.dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&rtv));
  if (FAILED(hr)) { Out("d3d12 clear_readback result=FAIL stage=create hr=0x%08lx", (unsigned long)hr); goto done; }
  {
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtv->GetCPUDescriptorHandleForHeapStart();
    s.dev->CreateRenderTargetView(tex, nullptr, h);
    s.ca->Reset();
    s.cl->Reset(s.ca, nullptr);
    const float color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    s.cl->ClearRenderTargetView(h, color, 0, nullptr);
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = tex;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    s.cl->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = rd.Format;
    dst.PlacedFootprint.Footprint.Width = W; dst.PlacedFootprint.Footprint.Height = H;
    dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = W * 4;
    src.pResource = tex; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    s.cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    hr = s.cl->Close();
    if (FAILED(hr)) { Out("d3d12 clear_readback result=FAIL stage=close hr=0x%08lx", (unsigned long)hr); goto done; }
    ID3D12CommandList *lists[] = {s.cl};
    s.q->ExecuteCommandLists(1, lists);
    if (!D12Wait(s, "clear_readback")) goto done;
    void *p = nullptr;
    D3D12_RANGE r = {0, W * H * 4};
    hr = rb->Map(0, &r, &p);
    if (FAILED(hr)) { Out("d3d12 clear_readback result=FAIL stage=map hr=0x%08lx", (unsigned long)hr); goto done; }
    const BYTE *px = (const BYTE *)p;
    UINT bad = 0;
    for (UINT i = 0; i < W * H; ++i) {
      const BYTE *c = px + i * 4;
      if (abs(c[0] - 64) > 2 || abs(c[1] - 128) > 2 || abs(c[2] - 191) > 2 || c[3] != 255) ++bad;
    }
    Out("d3d12 clear_readback result=%s first=%02x%02x%02x%02x bad_pixels=%u", bad ? "FAIL" : "PASS", px[0], px[1], px[2],
        px[3], bad);
    D3D12_RANGE none = {0, 0};
    rb->Unmap(0, &none);
  }
done:
  Rel(rtv); Rel(rb); Rel(tex);
}

static void D12Compute(D12 &s) {
  const UINT N = 4096;
  ID3DBlob *cs = CompileCs(kCsSource, "cs_5_1");
  ID3DBlob *sig = nullptr, *err = nullptr;
  ID3D12RootSignature *rs = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12Resource *buf = nullptr, *rb = nullptr;
  HRESULT hr = cs ? S_OK : E_FAIL;
  D3D12_ROOT_PARAMETER rp = {};
  rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC rsd = {};
  rsd.NumParameters = 1; rsd.pParameters = &rp;
  if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (SUCCEEDED(hr)) hr = s.dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rs));
  if (SUCCEEDED(hr)) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = rs;
    pd.CS.pShaderBytecode = cs->GetBufferPointer();
    pd.CS.BytecodeLength = cs->GetBufferSize();
    hr = s.dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso));
  }
  if (SUCCEEDED(hr)) hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_DEFAULT, N * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &buf);
  if (SUCCEEDED(hr)) hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, N * 4, D3D12_RESOURCE_FLAG_NONE,
                                    D3D12_RESOURCE_STATE_COPY_DEST, &rb);
  if (FAILED(hr)) { Out("d3d12 compute result=FAIL stage=create hr=0x%08lx", (unsigned long)hr); goto done; }
  {
    s.ca->Reset();
    s.cl->Reset(s.ca, pso);
    s.cl->SetComputeRootSignature(rs);
    s.cl->SetComputeRootUnorderedAccessView(0, buf->GetGPUVirtualAddress());
    s.cl->Dispatch(N / 64, 1, 1);
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = buf;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    s.cl->ResourceBarrier(1, &b);
    s.cl->CopyResource(rb, buf);
    hr = s.cl->Close();
    if (FAILED(hr)) { Out("d3d12 compute result=FAIL stage=close hr=0x%08lx", (unsigned long)hr); goto done; }
    ID3D12CommandList *lists[] = {s.cl};
    s.q->ExecuteCommandLists(1, lists);
    if (!D12Wait(s, "compute")) goto done;
    void *p = nullptr;
    D3D12_RANGE r = {0, N * 4};
    hr = rb->Map(0, &r, &p);
    if (FAILED(hr)) { Out("d3d12 compute result=FAIL stage=map hr=0x%08lx", (unsigned long)hr); goto done; }
    const UINT *v = (const UINT *)p;
    UINT bad = 0;
    for (UINT i = 0; i < N; ++i) if (v[i] != i * 3 + 1) ++bad;
    Out("d3d12 compute result=%s elements=%u bad=%u v1=%u vlast=%u", bad ? "FAIL" : "PASS", N, bad, v[1], v[N - 1]);
    D3D12_RANGE none = {0, 0};
    rb->Unmap(0, &none);
  }
done:
  Rel(rb); Rel(buf); Rel(pso); Rel(rs); Rel(sig); Rel(err); Rel(cs);
}

static bool D12Open(D12 &s) {
  IDXGIFactory1 *f = nullptr;
  IDXGIAdapter1 *a = nullptr;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) {
    IDXGIFactory4 *f4 = nullptr;
    if (g_warp && SUCCEEDED(f->QueryInterface(IID_PPV_ARGS(&f4)))) {
      f4->EnumWarpAdapter(IID_PPV_ARGS(&a));  // reference rasterizer, to check the tests themselves
      Rel(f4);
    } else
      a = PickHardwareAdapter(f);
  }
  HRESULT hr = D3D12CreateDevice(a, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&s.dev));
  char name[128] = "none";
  if (a) { DXGI_ADAPTER_DESC1 d = {}; a->GetDesc1(&d); Narrow(d.Description, name, sizeof(name)); }
  Out("d3d12 device arch=%s adapter=\"%s\" hr=0x%08lx", Arch(), name, (unsigned long)hr);
  Rel(a); Rel(f);
  if (FAILED(hr)) return false;
  D3D12_COMMAND_QUEUE_DESC qd = {};
  qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  hr = s.dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&s.q));
  if (SUCCEEDED(hr)) hr = s.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.ca));
  if (SUCCEEDED(hr)) hr = s.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.ca, nullptr, IID_PPV_ARGS(&s.cl));
  if (SUCCEEDED(hr)) hr = s.cl->Close();
  if (SUCCEEDED(hr)) hr = s.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s.fence));
  s.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  Out("d3d12 queue hr=0x%08lx", (unsigned long)hr);
  return SUCCEEDED(hr);
}

static void D12Close(D12 &s) {
  if (s.dev) Out("d3d12 end removed_reason=0x%08lx", (unsigned long)s.dev->GetDeviceRemovedReason());
  Rel(s.fence); Rel(s.cl); Rel(s.ca); Rel(s.q); Rel(s.dev);
  if (s.ev) CloseHandle(s.ev);
  s.ev = nullptr;
}

static void TestD3d12() {
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  D12Caps(s.dev);
  D12HeapLadder(s.dev);
  D12Clear(s);
  D12Compute(s);
  D12Close(s);
}

static void TestFill() {
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  ID3D12Heap *heaps[16] = {};
  UINT n = 0;
  HRESULT hr = S_OK;
  for (; n < 16; ++n) {
    D3D12_HEAP_DESC hd = {};
    hd.SizeInBytes = 64 * MiB;
    hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    hr = s.dev->CreateHeap(&hd, IID_PPV_ARGS(&heaps[n]));
    if (FAILED(hr)) break;
  }
  Out("fill heaps_64mib=%u total_mib=%u stop_hr=0x%08lx cap_mib=1024", n, n * 64, (unsigned long)hr);
  for (UINT i = 0; i < 16; ++i) Rel(heaps[i]);
  D12Close(s);
}

#include "nwgpuprobe_tex.inc"
#include "nwgpuprobe_ui.inc"

// ---------------------------------------------------------------- D3D11
static void D11VideoProfiles(ID3D11Device *dev) {
  ID3D11VideoDevice *vd = nullptr;
  HRESULT hr = dev->QueryInterface(IID_PPV_ARGS(&vd));
  if (FAILED(hr)) { Out("d3d11 video_device hr=0x%08lx profiles=0", (unsigned long)hr); return; }
  UINT n = vd->GetVideoDecoderProfileCount();
  static const struct { GUID g; const char *name; } known[] = {
      {{0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}}, "h264_vld_nofgt"},
      {{0x5b11d51b, 0x2f4c, 0x4452, {0xbc, 0xc3, 0x09, 0xf2, 0xa1, 0x16, 0x0c, 0xc0}}, "hevc_main"},
      {{0x463707f8, 0xa1d0, 0x4585, {0x87, 0x6d, 0x83, 0xaa, 0x6d, 0x60, 0xb8, 0x9e}}, "vp9_profile0"},
      {{0xb8be4ccb, 0xcf53, 0x46ba, {0x8d, 0x59, 0xd6, 0xb8, 0xa6, 0xda, 0x5d, 0x2a}}, "av1_profile0"},
  };
  Out("d3d11 video_device hr=0x%08lx profiles=%u", (unsigned long)hr, n);
  for (UINT i = 0; i < n && i < 32; ++i) {
    GUID g = {};
    if (FAILED(vd->GetVideoDecoderProfile(i, &g))) continue;
    const char *name = "other";
    for (auto &k : known) if (IsEqualGUID(g, k.g)) name = k.name;
    Out("d3d11 video_profile index=%u name=%s guid=%08lx-%04x-%04x", i, name, (unsigned long)g.Data1, g.Data2, g.Data3);
  }
  Rel(vd);
}

static void TestD3d11() {
  static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
                                             D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                             D3D_FEATURE_LEVEL_9_3};
  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  D3D_FEATURE_LEVEL got = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, ARRAYSIZE(levels),
                                 D3D11_SDK_VERSION, &dev, &got, &ctx);
  char name[128] = "none";
  if (SUCCEEDED(hr)) {
    IDXGIDevice *xd = nullptr;
    IDXGIAdapter *a = nullptr;
    if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&xd))) && SUCCEEDED(xd->GetAdapter(&a))) {
      DXGI_ADAPTER_DESC d = {};
      a->GetDesc(&d);
      Narrow(d.Description, name, sizeof(name));
    }
    Rel(a); Rel(xd);
  }
  Out("d3d11 device arch=%s adapter=\"%s\" hr=0x%08lx feature_level=0x%x", Arch(), name, (unsigned long)hr, (unsigned)got);
  if (FAILED(hr)) return;
  D3D11_FEATURE_DATA_D3D11_OPTIONS o = {};
  hr = dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof(o));
  Out("d3d11 caps options hr=0x%08lx map_nooverwrite_dynamic_cb=%d cb_offsetting=%d", (unsigned long)hr,
      (int)o.MapNoOverwriteOnDynamicConstantBuffer, (int)o.ConstantBufferOffsetting);
  D3D11_FEATURE_DATA_THREADING th = {};
  hr = dev->CheckFeatureSupport(D3D11_FEATURE_THREADING, &th, sizeof(th));
  Out("d3d11 caps threading hr=0x%08lx concurrent_creates=%d command_lists=%d", (unsigned long)hr,
      (int)th.DriverConcurrentCreates, (int)th.DriverCommandLists);
  // texture and buffer ladders
  static const UINT dims[] = {1024, 2048, 2896, 4096, 8192, 16384};
  for (UINT d : dims) {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = d; td.Height = d; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *t = nullptr;
    hr = dev->CreateTexture2D(&td, nullptr, &t);
    Out("d3d11 texture rgba8=%ux%u mib=%llu hr=0x%08lx", d, d, (unsigned long long)(4ull * d * d / MiB),
        (unsigned long)hr);
    Rel(t);
  }
  for (UINT mib : kLadderMiB) {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = mib * (UINT)MiB;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    ID3D11Buffer *b = nullptr;
    hr = dev->CreateBuffer(&bd, nullptr, &b);
    Out("d3d11 buffer mib=%u hr=0x%08lx", mib, (unsigned long)hr);
    Rel(b);
  }
  // clear + readback
  {
    const UINT W = 256, H = 256;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *rt = nullptr, *st = nullptr;
    ID3D11RenderTargetView *rtv = nullptr;
    hr = dev->CreateTexture2D(&td, nullptr, &rt);
    if (SUCCEEDED(hr)) hr = dev->CreateRenderTargetView(rt, nullptr, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(hr)) hr = dev->CreateTexture2D(&td, nullptr, &st);
    if (SUCCEEDED(hr)) {
      const float color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
      ctx->ClearRenderTargetView(rtv, color);
      ctx->CopyResource(st, rt);
      D3D11_MAPPED_SUBRESOURCE m = {};
      hr = ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
      if (SUCCEEDED(hr)) {
        UINT bad = 0;
        const BYTE *row0 = (const BYTE *)m.pData;
        for (UINT y = 0; y < H; ++y) {
          const BYTE *row = (const BYTE *)m.pData + y * m.RowPitch;
          for (UINT x = 0; x < W; ++x) {
            const BYTE *c = row + x * 4;
            if (abs(c[0] - 64) > 2 || abs(c[1] - 128) > 2 || abs(c[2] - 191) > 2 || c[3] != 255) ++bad;
          }
        }
        Out("d3d11 clear_readback result=%s first=%02x%02x%02x%02x bad_pixels=%u", bad ? "FAIL" : "PASS", row0[0],
            row0[1], row0[2], row0[3], bad);
        ctx->Unmap(st, 0);
      } else Out("d3d11 clear_readback result=FAIL stage=map hr=0x%08lx", (unsigned long)hr);
    } else Out("d3d11 clear_readback result=FAIL stage=create hr=0x%08lx", (unsigned long)hr);
    Rel(st); Rel(rtv); Rel(rt);
  }
  // compute
  {
    const UINT N = 4096;
    ID3DBlob *cs = CompileCs(kCsSource, "cs_5_0");
    ID3D11ComputeShader *sh = nullptr;
    ID3D11Buffer *buf = nullptr, *st = nullptr;
    ID3D11UnorderedAccessView *uav = nullptr;
    hr = cs ? dev->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &sh) : E_FAIL;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = N * 4; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    if (SUCCEEDED(hr)) hr = dev->CreateBuffer(&bd, nullptr, &buf);
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = N; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    if (SUCCEEDED(hr)) hr = dev->CreateUnorderedAccessView(buf, &ud, &uav);
    bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; bd.MiscFlags = 0;
    if (SUCCEEDED(hr)) hr = dev->CreateBuffer(&bd, nullptr, &st);
    if (SUCCEEDED(hr)) {
      ctx->CSSetShader(sh, nullptr, 0);
      ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
      ctx->Dispatch(N / 64, 1, 1);
      ID3D11UnorderedAccessView *nul = nullptr;
      ctx->CSSetUnorderedAccessViews(0, 1, &nul, nullptr);
      ctx->CopyResource(st, buf);
      D3D11_MAPPED_SUBRESOURCE m = {};
      hr = ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
      if (SUCCEEDED(hr)) {
        const UINT *v = (const UINT *)m.pData;
        UINT bad = 0;
        for (UINT i = 0; i < N; ++i) if (v[i] != i * 3 + 1) ++bad;
        Out("d3d11 compute result=%s elements=%u bad=%u v1=%u vlast=%u", bad ? "FAIL" : "PASS", N, bad, v[1], v[N - 1]);
        ctx->Unmap(st, 0);
      } else Out("d3d11 compute result=FAIL stage=map hr=0x%08lx", (unsigned long)hr);
    } else Out("d3d11 compute result=FAIL stage=create hr=0x%08lx", (unsigned long)hr);
    Rel(st); Rel(uav); Rel(buf); Rel(sh); Rel(cs);
  }
  D11VideoProfiles(dev);
  Out("d3d11 end removed_reason=0x%08lx", (unsigned long)dev->GetDeviceRemovedReason());
  Rel(ctx); Rel(dev);
}

// ---------------------------------------------------------------- present
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static void Pump() {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}

// Reads the composed desktop at the window's client centre (GDI), i.e. what
// the user actually sees after DWM composition.
static COLORREF ScreenPixel(HWND w) {
  RECT rc;
  GetClientRect(w, &rc);
  POINT p = {(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
  ClientToScreen(w, &p);
  HDC dc = GetDC(nullptr);
  COLORREF c = GetPixel(dc, p.x, p.y);
  ReleaseDC(nullptr, dc);
  return c;
}

static HWND MakeWindow(const WCHAR *title) {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"NwGpuProbe";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&wc);
  HWND w = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 640, 400,
                           nullptr, nullptr, wc.hInstance, nullptr);
  ShowWindow(w, SW_SHOW);
  SetForegroundWindow(w);
  Pump();
  return w;
}

static void TestPresent11(HWND w) {
  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx);
  IDXGIDevice *xd = nullptr;
  IDXGIAdapter *a = nullptr;
  IDXGIFactory2 *f = nullptr;
  IDXGISwapChain1 *sc = nullptr;
  ID3D11Texture2D *bb = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  if (SUCCEEDED(hr)) hr = dev->QueryInterface(IID_PPV_ARGS(&xd));
  if (SUCCEEDED(hr)) hr = xd->GetAdapter(&a);
  if (SUCCEEDED(hr)) hr = a->GetParent(IID_PPV_ARGS(&f));
  DXGI_SWAP_CHAIN_DESC1 d = {};
  d.Width = 0; d.Height = 0; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
  d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  if (SUCCEEDED(hr)) hr = f->CreateSwapChainForHwnd(dev, w, &d, nullptr, nullptr, &sc);
  Out("present11 swapchain hr=0x%08lx", (unsigned long)hr);
  if (SUCCEEDED(hr)) {
    const UINT frames = 180;
    LARGE_INTEGER f0, t0, t1;
    QueryPerformanceFrequency(&f0);
    QueryPerformanceCounter(&t0);
    HRESULT ph = S_OK;
    UINT presented = 0;
    for (UINT i = 0; i < frames; ++i) {
      if (FAILED(sc->GetBuffer(0, IID_PPV_ARGS(&bb)))) break;
      if (FAILED(dev->CreateRenderTargetView(bb, nullptr, &rtv))) { Rel(bb); break; }
      // last 30 frames: solid magenta, so the composed screen can be checked
      float c[4] = {(i & 1) ? 0.1f : 0.9f, 0.3f, (i & 1) ? 0.9f : 0.1f, 1.0f};
      if (i >= frames - 30) { c[0] = 1.0f; c[1] = 0.0f; c[2] = 1.0f; }
      ctx->ClearRenderTargetView(rtv, c);
      Rel(rtv); Rel(bb);
      ph = sc->Present(1, 0);
      if (FAILED(ph)) break;
      ++presented;
      Pump();
    }
    QueryPerformanceCounter(&t1);
    double secs = (double)(t1.QuadPart - t0.QuadPart) / (double)f0.QuadPart;
    DXGI_FRAME_STATISTICS fs = {};
    HRESULT sh = sc->GetFrameStatistics(&fs);
    Sleep(300);
    Pump();
    COLORREF px = ScreenPixel(w);
    BOOL magenta = GetRValue(px) > 200 && GetGValue(px) < 60 && GetBValue(px) > 200;
    Out("present11 frames=%u present_hr=0x%08lx seconds=%.2f fps=%.1f stats_hr=0x%08lx present_count=%u screen_rgb=%02x%02x%02x screen_check=%s",
        presented, (unsigned long)ph, secs, secs > 0 ? presented / secs : 0.0, (unsigned long)sh, fs.PresentCount,
        GetRValue(px), GetGValue(px), GetBValue(px), magenta ? "PASS" : "FAIL");
  }
  Rel(rtv); Rel(bb); Rel(sc); Rel(f); Rel(a); Rel(xd); Rel(ctx); Rel(dev);
}

static void TestPresent12(HWND w) {
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  IDXGIFactory4 *f = nullptr;
  IDXGISwapChain1 *sc1 = nullptr;
  IDXGISwapChain3 *sc = nullptr;
  ID3D12DescriptorHeap *rtvh = nullptr;
  HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&f));
  DXGI_SWAP_CHAIN_DESC1 d = {};
  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  d.BufferCount = 2; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  if (SUCCEEDED(hr)) hr = f->CreateSwapChainForHwnd(s.q, w, &d, nullptr, nullptr, &sc1);
  if (SUCCEEDED(hr)) hr = sc1->QueryInterface(IID_PPV_ARGS(&sc));
  D3D12_DESCRIPTOR_HEAP_DESC dh = {};
  dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; dh.NumDescriptors = 2;
  if (SUCCEEDED(hr)) hr = s.dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&rtvh));
  Out("present12 swapchain hr=0x%08lx", (unsigned long)hr);
  if (SUCCEEDED(hr)) {
    UINT inc = s.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ID3D12Resource *bufs[2] = {};
    for (UINT i = 0; i < 2 && SUCCEEDED(hr); ++i) {
      hr = sc->GetBuffer(i, IID_PPV_ARGS(&bufs[i]));
      D3D12_CPU_DESCRIPTOR_HANDLE h = rtvh->GetCPUDescriptorHandleForHeapStart();
      h.ptr += i * inc;
      if (SUCCEEDED(hr)) s.dev->CreateRenderTargetView(bufs[i], nullptr, h);
    }
    const UINT frames = 120;
    UINT presented = 0;
    HRESULT ph = hr;
    LARGE_INTEGER f0, t0, t1;
    QueryPerformanceFrequency(&f0);
    QueryPerformanceCounter(&t0);
    for (UINT i = 0; i < frames && SUCCEEDED(hr); ++i) {
      UINT bi = sc->GetCurrentBackBufferIndex();
      D3D12_CPU_DESCRIPTOR_HANDLE h = rtvh->GetCPUDescriptorHandleForHeapStart();
      h.ptr += bi * inc;
      s.ca->Reset();
      s.cl->Reset(s.ca, nullptr);
      D3D12_RESOURCE_BARRIER b = {};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = bufs[bi];
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
      s.cl->ResourceBarrier(1, &b);
      float c[4] = {0.0f, (i & 1) ? 0.8f : 0.2f, 0.4f, 1.0f};
      if (i >= frames - 30) { c[0] = 0.0f; c[1] = 1.0f; c[2] = 1.0f; } // cyan at the end
      s.cl->ClearRenderTargetView(h, c, 0, nullptr);
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
      s.cl->ResourceBarrier(1, &b);
      hr = s.cl->Close();
      if (FAILED(hr)) break;
      ID3D12CommandList *lists[] = {s.cl};
      s.q->ExecuteCommandLists(1, lists);
      ph = sc->Present(1, 0);
      if (FAILED(ph)) break;
      if (!D12Wait(s, "present12")) { ph = E_FAIL; break; }
      ++presented;
      Pump();
    }
    QueryPerformanceCounter(&t1);
    double secs = (double)(t1.QuadPart - t0.QuadPart) / (double)f0.QuadPart;
    Sleep(300);
    Pump();
    COLORREF px = ScreenPixel(w);
    BOOL cyan = GetRValue(px) < 60 && GetGValue(px) > 200 && GetBValue(px) > 200;
    Out("present12 frames=%u present_hr=0x%08lx seconds=%.2f fps=%.1f screen_rgb=%02x%02x%02x screen_check=%s", presented,
        (unsigned long)ph, secs, secs > 0 ? presented / secs : 0.0, GetRValue(px), GetGValue(px), GetBValue(px),
        cyan ? "PASS" : "FAIL");
    Rel(bufs[0]); Rel(bufs[1]);
  }
  Rel(rtvh); Rel(sc); Rel(sc1); Rel(f);
  D12Close(s);
}

#include "nwgpuprobe_comp.inc"
#include "nwgpuprobe_interop.inc"
#include "nwgpuprobe_d3d11ui.inc"
#include "nwgpuprobe_chromium.inc"
#include "nwgpuprobe_x64umd.inc"
#include "nwgpuprobe_mem.inc"
#include "nwgpuprobe_mem2.inc"  // probe12 (P38): memladder2, fill3, rcpt

static void TestPresent() {
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  Out("present session=%lu", (unsigned long)session);
  if (session == 0) { Out("present result=SKIP reason=session0_needs_user_session"); return; }
  HWND w = MakeWindow(L"NWOAS GPU probe");
  if (!w) { Out("present result=FAIL reason=window gle=%lu", GetLastError()); return; }
  TestPresent11(w);
  TestPresent12(w);
  Present12Variant(w, false);
  Present12Variant(w, true);
  DestroyWindow(w);
  Pump();
}

int main(int argc, char **argv) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  Out("begin arch=%s pid=%lu time=%04u-%02u-%02uT%02u:%02u:%02u", Arch(), GetCurrentProcessId(), st.wYear, st.wMonth,
      st.wDay, st.wHour, st.wMinute, st.wSecond);
  bool all = argc < 2;
  for (int i = 1; i < argc; ++i) if (!_stricmp(argv[i], "warp")) g_warp = true;
  for (int i = 1; i < argc; ++i) if (!_strnicmp(argv[i], "memcap=", 7)) g_memcap = (UINT)strtoul(argv[i] + 7, nullptr, 10);
  for (int i = 1; i < argc; ++i)
    if (!_strnicmp(argv[i], "ladder=", 7)) {  // probe12: memladder2 sizes in MiB, comma separated
      g_ladderCount = 0;
      for (char *q = argv[i] + 7; *q && g_ladderCount < ARRAYSIZE(g_ladder);) {
        UINT v = (UINT)strtoul(q, &q, 10);
        if (v) g_ladder[g_ladderCount++] = v;
        if (*q == ',') ++q; else break;
      }
    }
  for (int i = 1; i < argc; ++i)  // probe13: memladder2 waits up to <ms> for the previous size's memory
    if (!_strnicmp(argv[i], "settle=", 7)) g_settleMs = (UINT)strtoul(argv[i] + 7, nullptr, 10);
  for (int i = 1; i < argc; ++i) if (!_stricmp(argv[i], "all")) all = true;
  auto want = [&](const char *n) {
    if (all && _stricmp(n, "fill")) return true;
    for (int i = 1; i < argc; ++i) if (!_stricmp(argv[i], n)) return true;
    return false;
  };
  if (want("dxgi")) TestDxgi();
  if (want("d3d12")) TestD3d12();
  if (want("d3d11")) TestD3d11();
  if (want("present")) TestPresent();
  if (want("tex")) TestTex();
  if (want("ui")) TestUi();
  if (want("interop")) TestInterop();
  if (want("d3d11ui")) TestD3d11Ui();
  if (want("modeswitch") && !all) TestModeSwitch();
  if (want("chromium")) TestChromium();
  if (want("layered")) { Layered(false); Layered(true); }
  if (want("umd") && !all) TestUmdModules();
  if (want("draw11") && !all) TestDraw11();
  if (want("fill")) TestFill();
  if (want("pool") && !all) TestPoolReceipt();
  if (want("memladder") && !all) TestMemLadder();
  if (want("fill2") && !all) TestFill2();
  if (want("d3d11mem") && !all) TestD3d11Mem();
  if (want("memladder2") && !all) TestMemLadder2();
  if (want("fill3") && !all) TestFill3();
  if (want("rcpt") && !all) MemReceipts("rcpt", "now");
  Out("end");
  return 0;
}
