// nwgpuprobe_mem.inc (probe11, S265): how much GPU memory the driver really gives an application once
// the hypervisor pool window (segment 3) is on, and whether what it gives holds the right bytes.
//   pool      : the KMD's own pool receipts (Device Parameters Wom1M2PoolProbe / Wom1M2PoolDemand)
//   memladder : D3D12 committed DEFAULT buffers 256..3072 MiB, one at a time; every 64 MiB chunk is written
//               from an UPLOAD buffer with an offset-dependent pattern, copied back to a READBACK buffer
//               and checked (every 4 KiB word + the first 1 MiB of each chunk in full), so aliasing,
//               lost pages and wrong placement show up as mismatches; DXGI local usage after each size
//   fill2     : 256 MiB DEFAULT heaps kept alive until creation fails (cap 20 = 5 GiB); a placed buffer
//               on every heap gets a 64 KiB tag at its start and end, all tags are read back at the end
//   d3d11mem  : D3D11 DEFAULT buffers 64..1024 MiB, same pattern check through a 16 MiB STAGING buffer
// Never part of "all". Every line "NWGP <test> ...".
#pragma comment(lib, "advapi32.lib")

// "memcap=<MiB>" on the command line skips larger sizes (VM self-check only; 0 = no cap).
static UINT g_memcap = 0;
static bool MemSkip(UINT mib) { return g_memcap && mib > g_memcap; }

static bool MemRegBin(const wchar_t *name, std::string &out) {
  DWORD bytes = 0;
  const wchar_t *key = L"SYSTEM\\CurrentControlSet\\Enum\\ACPI\\APPL0002\\0\\Device Parameters";
  LONG rc = RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_BINARY | RRF_SUBKEY_WOW6464KEY, nullptr, nullptr,
                         &bytes);
  if (rc != ERROR_SUCCESS || bytes == 0 || bytes > 65536) return false;
  out.assign(bytes, '\0');
  rc = RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_BINARY | RRF_SUBKEY_WOW6464KEY, nullptr, &out[0], &bytes);
  if (rc != ERROR_SUCCESS) return false;
  out.resize(bytes);
  return true;
}
static UINT64 MemU64(const std::string &b, size_t off) {
  UINT64 v = 0;
  if (off + 8 <= b.size()) memcpy(&v, b.data() + off, 8);
  return v;
}
static UINT32 MemU32(const std::string &b, size_t off) {
  UINT32 v = 0;
  if (off + 4 <= b.size()) memcpy(&v, b.data() + off, 4);
  return v;
}

// render_pool_probe.h ADMISSION_POOL_PROBE v2: 16-byte header, 8 attempts of 64 bytes; attempt 2 = the
// hypervisor window (SkipBytes = window IPA, RequestedBytes = segment 3 bytes in demand mode,
// Misaligned = last window result, Status 10 = DEMAND, 7 = ABSENT, 11 = DEMAND_TOO_SMALL, 5/6 = map/topology).
// render_pool_demand.h ADMISSION_POOL_DEMAND_RECEIPT: Version, Bytes, GranuleBytes, LastWindowResult,
// SegmentBytes, then the stats (CommittedGranules, PeakGranules, Commits, CommitFailures, ...).
static void TestPoolReceipt() {
  std::string p, d;
  if (!MemRegBin(L"Wom1M2PoolProbe", p)) {
    Out("pool probe present=0");
  } else {
    const size_t w = 16 + 2 * 64;
    UINT32 st = MemU32(p, w + 60);
    Out("pool probe present=1 bytes=%u version=%u window_status=%u (%s) window_ipa=0x%llx segment_mib=%llu "
        "mapped_mib=%llu last_window_result=%u",
        (unsigned)p.size(), MemU32(p, 0), st,
        st == 10 ? "demand_on" : st == 7 ? "absent" : st == 11 ? "too_small" : st == 0 ? "not_tried" : "failed",
        MemU64(p, w), MemU64(p, w + 8) / MiB, MemU64(p, w + 16) / MiB, MemU32(p, w + 52));
  }
  if (!MemRegBin(L"Wom1M2PoolDemand", d)) {
    Out("pool demand present=0");
  } else {
    UINT64 g = MemU32(d, 8);
    Out("pool demand present=1 bytes=%u version=%u granule=%llu last_window_result=%u segment_mib=%llu "
        "committed_mib=%llu peak_mib=%llu commits=%llu commit_failures=%llu",
        (unsigned)d.size(), MemU32(d, 0), g, MemU32(d, 12), MemU64(d, 16) / MiB, MemU64(d, 24) * g / MiB,
        MemU64(d, 32) * g / MiB, MemU64(d, 40), MemU64(d, 48));
  }
}

static UINT32 MemPat(UINT64 off, UINT32 seed) {
  UINT64 x = (off >> 2) * 0x9E3779B97F4A7C15ull ^ ((UINT64)seed << 32 | seed);
  x ^= x >> 29;
  x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 32;
  return (UINT32)x;
}
static void MemFill(UINT32 *p, UINT64 base, UINT64 bytes, UINT32 seed) {
  for (UINT64 i = 0; i < bytes / 4; ++i) p[i] = MemPat(base + i * 4, seed);
}
// Every 4 KiB word plus the first 1 MiB in full; returns mismatches, first bad offset in *bad.
static UINT64 MemCheck(const volatile UINT32 *p, UINT64 base, UINT64 bytes, UINT32 seed, UINT64 *bad, UINT32 *got,
                       UINT32 *want) {
  UINT64 n = 0;
  for (UINT64 off = 0; off < bytes; off += (off < MiB ? 4 : 4096)) {
    UINT32 v = p[off / 4], e = MemPat(base + off, seed);
    if (v != e) {
      if (!n) { *bad = base + off; *got = v; *want = e; }
      ++n;
    }
  }
  return n;
}

static bool MemLocal(LUID luid, DXGI_QUERY_VIDEO_MEMORY_INFO *loc, DXGI_QUERY_VIDEO_MEMORY_INFO *non) {
  IDXGIFactory4 *f = nullptr;
  IDXGIAdapter3 *a = nullptr;
  bool ok = false;
  *loc = {};
  *non = {};
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f))) && SUCCEEDED(f->EnumAdapterByLuid(luid, IID_PPV_ARGS(&a)))) {
    ok = SUCCEEDED(a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, loc)) &&
         SUCCEEDED(a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, non));
  }
  Rel(a);
  Rel(f);
  return ok;
}
static void MemBudgetLine(const char *test, const char *when, LUID luid) {
  DXGI_QUERY_VIDEO_MEMORY_INFO l, n;
  bool ok = MemLocal(luid, &l, &n);
  Out("%s budget when=%s ok=%u local_budget_mib=%llu local_usage_mib=%llu nonlocal_budget_mib=%llu "
      "nonlocal_usage_mib=%llu",
      test, when, ok ? 1u : 0u, (unsigned long long)(l.Budget / MiB), (unsigned long long)(l.CurrentUsage / MiB),
      (unsigned long long)(n.Budget / MiB), (unsigned long long)(n.CurrentUsage / MiB));
}

static bool D12Copy(D12 &s, ID3D12Resource *dst, UINT64 dstOff, ID3D12Resource *src, UINT64 srcOff, UINT64 bytes,
                    const char *what) {
  if (FAILED(s.ca->Reset()) || FAILED(s.cl->Reset(s.ca, nullptr))) return false;
  s.cl->CopyBufferRegion(dst, dstOff, src, srcOff, bytes);
  if (FAILED(s.cl->Close())) return false;
  ID3D12CommandList *l = s.cl;
  s.q->ExecuteCommandLists(1, &l);
  return D12Wait(s, what);
}

static const UINT64 kMemChunk = 64 * MiB;

static void TestMemLadder() {
  static const UINT kSizes[] = {256, 512, 1024, 1536, 2048, 2560, 3072};
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  LUID luid = s.dev->GetAdapterLuid();
  MemBudgetLine("memladder", "start", luid);
  ID3D12Resource *up = nullptr, *rb = nullptr;
  HRESULT hu = D12Buffer(s.dev, D3D12_HEAP_TYPE_UPLOAD, kMemChunk, D3D12_RESOURCE_FLAG_NONE,
                         D3D12_RESOURCE_STATE_GENERIC_READ, &up);
  HRESULT hr2 = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, kMemChunk, D3D12_RESOURCE_FLAG_NONE,
                          D3D12_RESOURCE_STATE_COPY_DEST, &rb);
  Out("memladder staging upload_hr=0x%08lx readback_hr=0x%08lx chunk_mib=64", (unsigned long)hu, (unsigned long)hr2);
  if (FAILED(hu) || FAILED(hr2)) { Rel(up); Rel(rb); D12Close(s); return; }
  UINT largestPass = 0, firstFail = 0;
  for (UINT si = 0; si < ARRAYSIZE(kSizes); ++si) {
    const UINT mib = kSizes[si];
    if (MemSkip(mib)) continue;
    const UINT64 bytes = (UINT64)mib * MiB;
    const UINT32 seed = 0x5265u + mib;
    ULONGLONG t0 = GetTickCount64();
    ID3D12Resource *big = nullptr;
    HRESULT hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COMMON, &big);
    DXGI_QUERY_VIDEO_MEMORY_INFO l, n;
    MemLocal(luid, &l, &n);
    UINT64 mism = 0, bad = 0, chunksOk = 0;
    UINT32 got = 0, want = 0;
    bool gpuOk = SUCCEEDED(hr);
    const char *stage = SUCCEEDED(hr) ? "ok" : "create";
    for (UINT64 c = 0; gpuOk && c < bytes / kMemChunk; ++c) {
      void *p = nullptr;
      D3D12_RANGE none = {0, 0};
      if (FAILED(up->Map(0, &none, &p)) || !p) { gpuOk = false; stage = "map_upload"; break; }
      MemFill((UINT32 *)p, c * kMemChunk, kMemChunk, seed);
      up->Unmap(0, nullptr);
      if (!D12Copy(s, big, c * kMemChunk, up, 0, kMemChunk, "memladder_write")) { gpuOk = false; stage = "write"; }
    }
    for (UINT64 c = 0; gpuOk && c < bytes / kMemChunk; ++c) {
      if (!D12Copy(s, rb, 0, big, c * kMemChunk, kMemChunk, "memladder_read")) { gpuOk = false; stage = "read"; break; }
      void *p = nullptr;
      D3D12_RANGE all = {0, (SIZE_T)kMemChunk};
      if (FAILED(rb->Map(0, &all, &p)) || !p) { gpuOk = false; stage = "map_readback"; break; }
      UINT64 b1 = 0;
      UINT32 g1 = 0, w1 = 0;
      UINT64 m = MemCheck((const volatile UINT32 *)p, c * kMemChunk, kMemChunk, seed, &b1, &g1, &w1);
      D3D12_RANGE nothing = {0, 0};
      rb->Unmap(0, &nothing);
      if (!m) ++chunksOk;
      if (m && !mism) { bad = b1; got = g1; want = w1; }  // keep the first bad chunk's word
      mism += m;
    }
    HRESULT removed = s.dev->GetDeviceRemovedReason();
    bool pass = SUCCEEDED(hr) && gpuOk && mism == 0 && removed == S_OK;
    if (pass) largestPass = mib; else if (!firstFail) firstFail = mib;
    Out("memladder d3d12 size_mib=%u create_hr=0x%08lx stage=%s chunks_ok=%llu/%llu mismatches=%llu first_bad=0x%llx "
        "got=0x%08x want=0x%08x local_usage_mib=%llu local_budget_mib=%llu removed=0x%08lx ms=%llu result=%s",
        mib, (unsigned long)hr, stage, chunksOk, bytes / kMemChunk, mism, bad, got, want,
        (unsigned long long)(l.CurrentUsage / MiB), (unsigned long long)(l.Budget / MiB), (unsigned long)removed,
        (unsigned long long)(GetTickCount64() - t0), pass ? "PASS" : "FAIL");
    Rel(big);
    if (removed != S_OK) break;
  }
  {  // CPU read speed of a READBACK mapping (the 11-16 local-reserve figure was 5.7-34.6 ms per 8 MiB)
    void *p = nullptr;
    D3D12_RANGE all = {0, (SIZE_T)kMemChunk};
    if (SUCCEEDED(rb->Map(0, &all, &p)) && p) {
      UINT32 *tmp = (UINT32 *)VirtualAlloc(nullptr, (SIZE_T)kMemChunk, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
      if (tmp) {
        LARGE_INTEGER f, a, b;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        memcpy(tmp, p, (size_t)kMemChunk);
        QueryPerformanceCounter(&b);
        double ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
        Out("memladder readback_cpu_read mib=64 ms=%.1f ms_per_8mib=%.2f cpu=%lu", ms, ms / 8.0,
            (unsigned long)GetCurrentProcessorNumber());
        VirtualFree(tmp, 0, MEM_RELEASE);
      }
      D3D12_RANGE nothing = {0, 0};
      rb->Unmap(0, &nothing);
    }
  }
  Rel(up);
  Rel(rb);
  MemBudgetLine("memladder", "end", luid);
  Out("memladder summary largest_pass_mib=%u first_fail_mib=%u", largestPass, firstFail);
  D12Close(s);
}

static void TestFill2() {
  const UINT kMax = 20;  // 5 GiB cap
  const UINT64 kHeap = 256 * MiB, kTag = 64 * 1024;
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  LUID luid = s.dev->GetAdapterLuid();
  MemBudgetLine("fill2", "start", luid);
  ID3D12Heap *heaps[kMax] = {};
  ID3D12Resource *bufs[kMax] = {};
  ID3D12Resource *up = nullptr, *rb = nullptr;
  HRESULT hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_UPLOAD, 2 * kTag, D3D12_RESOURCE_FLAG_NONE,
                         D3D12_RESOURCE_STATE_GENERIC_READ, &up);
  if (SUCCEEDED(hr))
    hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, 2 * kTag, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST,
                   &rb);
  if (FAILED(hr)) { Out("fill2 staging hr=0x%08lx", (unsigned long)hr); Rel(up); Rel(rb); D12Close(s); return; }
  UINT n = 0, tagged = 0;
  HRESULT stop = S_OK;
  const UINT limit = g_memcap ? (g_memcap / 256u < kMax ? g_memcap / 256u : kMax) : kMax;
  for (; n < limit; ++n) {
    D3D12_HEAP_DESC hd = {};
    hd.SizeInBytes = kHeap;
    hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    stop = s.dev->CreateHeap(&hd, IID_PPV_ARGS(&heaps[n]));
    if (FAILED(stop)) break;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = kHeap;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    stop = s.dev->CreatePlacedResource(heaps[n], 0, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&bufs[n]));
    if (FAILED(stop)) { Rel(heaps[n]); break; }
    void *p = nullptr;
    D3D12_RANGE none = {0, 0};
    bool ok = SUCCEEDED(up->Map(0, &none, &p)) && p;
    if (ok) {
      MemFill((UINT32 *)p, (UINT64)n * kHeap, kTag, 0xF1110000u);
      MemFill((UINT32 *)((char *)p + kTag), (UINT64)n * kHeap + kHeap - kTag, kTag, 0xF1110000u);
      up->Unmap(0, nullptr);
      ok = D12Copy(s, bufs[n], 0, up, 0, kTag, "fill2_tag0") &&
           D12Copy(s, bufs[n], kHeap - kTag, up, kTag, kTag, "fill2_tag1");
    }
    if (ok) ++tagged;
    DXGI_QUERY_VIDEO_MEMORY_INFO l, nl;
    MemLocal(luid, &l, &nl);
    Out("fill2 heap=%u total_mib=%llu tagged=%u local_usage_mib=%llu local_budget_mib=%llu", n + 1,
        (unsigned long long)((n + 1) * (kHeap / MiB)), ok ? 1u : 0u, (unsigned long long)(l.CurrentUsage / MiB),
        (unsigned long long)(l.Budget / MiB));
    if (!ok) { ++n; break; }
  }
  UINT tagsOk = 0;
  for (UINT i = 0; i < n; ++i) {
    if (!bufs[i]) continue;
    bool ok = D12Copy(s, rb, 0, bufs[i], 0, kTag, "fill2_read0") &&
              D12Copy(s, rb, kTag, bufs[i], kHeap - kTag, kTag, "fill2_read1");
    void *p = nullptr;
    D3D12_RANGE all = {0, (SIZE_T)(2 * kTag)};
    if (ok && SUCCEEDED(rb->Map(0, &all, &p)) && p) {
      UINT64 bad = 0;
      UINT32 g = 0, w = 0;
      UINT64 m = MemCheck((const volatile UINT32 *)p, (UINT64)i * kHeap, kTag, 0xF1110000u, &bad, &g, &w) +
                 MemCheck((const volatile UINT32 *)((char *)p + kTag), (UINT64)i * kHeap + kHeap - kTag, kTag,
                          0xF1110000u, &bad, &g, &w);
      D3D12_RANGE nothing = {0, 0};
      rb->Unmap(0, &nothing);
      if (!m) ++tagsOk;
      else Out("fill2 tag_bad heap=%u mismatches=%llu first_bad=0x%llx got=0x%08x want=0x%08x", i + 1, m, bad, g, w);
    }
  }
  HRESULT removed = s.dev->GetDeviceRemovedReason();
  Out("fill2 heaps_256mib=%u total_mib=%u stop_hr=0x%08lx tags_ok=%u/%u removed=0x%08lx cap_mib=%u", n, n * 256u,
      (unsigned long)stop, tagsOk, tagged, (unsigned long)removed, limit * 256u);
  for (UINT i = 0; i < kMax; ++i) { Rel(bufs[i]); Rel(heaps[i]); }
  Rel(up);
  Rel(rb);
  MemBudgetLine("fill2", "released", luid);
  D12Close(s);
}

static void TestD3d11Mem() {
  static const UINT kSizes[] = {64, 128, 256, 512, 1024};
  const UINT64 kStage = 16 * MiB;
  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx);
  Out("d3d11mem device arch=%s hr=0x%08lx", Arch(), (unsigned long)hr);
  if (FAILED(hr)) return;
  D3D11_BUFFER_DESC sd = {};
  sd.ByteWidth = (UINT)kStage;
  sd.Usage = D3D11_USAGE_STAGING;
  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
  ID3D11Buffer *st = nullptr;
  hr = dev->CreateBuffer(&sd, nullptr, &st);
  Out("d3d11mem staging hr=0x%08lx mib=16", (unsigned long)hr);
  UINT largestPass = 0, firstFail = 0;
  for (UINT si = 0; SUCCEEDED(hr) && si < ARRAYSIZE(kSizes); ++si) {
    const UINT mib = kSizes[si];
    if (MemSkip(mib)) continue;
    const UINT64 bytes = (UINT64)mib * MiB;
    const UINT32 seed = 0xD311u + mib;
    ULONGLONG t0 = GetTickCount64();
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)bytes;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    ID3D11Buffer *big = nullptr;
    HRESULT hc = dev->CreateBuffer(&bd, nullptr, &big);
    UINT64 mism = 0, bad = 0, ok = 0;
    UINT32 got = 0, want = 0;
    const char *stage = SUCCEEDED(hc) ? "ok" : "create";
    bool broken = false;
    for (UINT64 c = 0; SUCCEEDED(hc) && !broken && c < bytes / kStage; ++c) {
      D3D11_MAPPED_SUBRESOURCE m = {};
      if (FAILED(ctx->Map(st, 0, D3D11_MAP_WRITE, 0, &m))) { stage = "map_write"; broken = true; break; }
      MemFill((UINT32 *)m.pData, c * kStage, kStage, seed);
      ctx->Unmap(st, 0);
      D3D11_BOX box = {0, 0, 0, (UINT)kStage, 1, 1};
      ctx->CopySubresourceRegion(big, 0, (UINT)(c * kStage), 0, 0, st, 0, &box);
    }
    for (UINT64 c = 0; SUCCEEDED(hc) && !broken && c < bytes / kStage; ++c) {
      D3D11_BOX box = {(UINT)(c * kStage), 0, 0, (UINT)((c + 1) * kStage), 1, 1};
      ctx->CopySubresourceRegion(st, 0, 0, 0, 0, big, 0, &box);
      D3D11_MAPPED_SUBRESOURCE m = {};
      if (FAILED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) { stage = "map_read"; broken = true; break; }
      UINT64 b1 = 0;
      UINT32 g1 = 0, w1 = 0;
      UINT64 x = MemCheck((const volatile UINT32 *)m.pData, c * kStage, kStage, seed, &b1, &g1, &w1);
      ctx->Unmap(st, 0);
      if (!x) ++ok;
      if (x && !mism) { bad = b1; got = g1; want = w1; }
      mism += x;
    }
    HRESULT removed = dev->GetDeviceRemovedReason();
    bool pass = SUCCEEDED(hc) && !broken && mism == 0 && removed == S_OK;
    if (pass) largestPass = mib; else if (!firstFail) firstFail = mib;
    Out("d3d11mem size_mib=%u create_hr=0x%08lx stage=%s chunks_ok=%llu/%llu mismatches=%llu first_bad=0x%llx got=0x%08x "
        "want=0x%08x removed=0x%08lx ms=%llu result=%s",
        mib, (unsigned long)hc, stage, ok, bytes / kStage, mism, bad, got, want, (unsigned long)removed,
        (unsigned long long)(GetTickCount64() - t0), pass ? "PASS" : "FAIL");
    Rel(big);
    if (removed != S_OK) break;
  }
  Out("d3d11mem summary largest_pass_mib=%u first_fail_mib=%u", largestPass, firstFail);
  Rel(st);
  Rel(ctx);
  Rel(dev);
}
