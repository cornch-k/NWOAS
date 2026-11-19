// nwgpuprobe_mem2.inc (probe12, P38 / driver 2774): pinpoints where single D3D12 buffers above 2 GiB and
// segment-3 filling fail (S265 K43 on 2773: 2048 MiB buffer -> DXGI_ERROR_DRIVER_INTERNAL_ERROR before any
// copy; 16 x 256 MiB heaps -> DXGI_ERROR_DEVICE_RESET at the 17th).
//   rcpt       : KMD receipts under Device Parameters (pool demand v15/v16 incl. the P38 admission cap,
//                P38 Wom1PagingExecFailure, Wom1G3PagingFailure, Wom1GpuReset, Wom1G3Poison, broker failures,
//                Wom1DdiStatus failed slots), printed as decoded fields + leading hex
//   memladder2 : one fresh D3D12 device per size (default 1792,2048,2304,2560,3072 MiB; "ladder=a,b,.."),
//                create time, removed reason right after create and 500 ms later, then a probe copy of the
//                first and last 64 MiB chunk, then every chunk written/read/checked; receipts after a failure
//   fill3      : 256 MiB heaps until creation fails (cap 20), each heap's tags written AND read back at
//                once (so the first bad heap is named), all tags re-read at the end; receipts at the end
// Never part of "all". Lines "NWGP <test> ...". Needs nwgpuprobe_mem.inc (MemRegBin, MemPat, MemFill,
// MemCheck, MemLocal, MemBudgetLine, D12Copy, kMemChunk) included before it.

// probe13 (after 2774 K43c: 2304/2560 FAIL at probe_readback_map, 1792/2048/3072 PASS, no KMD failure receipt):
//   memladder2 "pre" line per size (this process's DXGI local usage, KMD pool reserved/committed, ms since the
//   previous size's device was closed), the removed reason after EACH probe copy (write = UPLOAD->big,
//   read = big->READBACK), "settle=<ms>": before each size wait up to <ms> until this process's local usage is
//   back to its value before the first size (+16 MiB); rcpt adds Wom1G4SubmitFailure, Wom1G4OmittedLeaves,
//   Wom1G3UnpublishedGroups, Wom1G3NativePteRefusal and the age of the Wom1DdiStatus snapshot / first records.
static UINT g_ladder[16] = {1792, 2048, 2304, 2560, 3072};
static UINT g_ladderCount = 5;
static UINT g_settleMs = 0;
// probe14 (after 2774 K43u/K43v: the first 64 MiB staging buffer was outside the pool before the first copy at
// 2304/2560): "order=bigfirst" creates the big DEFAULT buffer BEFORE the UPLOAD/READBACK staging buffers.
static bool g_bigFirst = false;

static double MemQpcAgeMs(UINT64 qpc, UINT64 freq) {
  LARGE_INTEGER now, f;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&f);
  if (!qpc || !freq || (UINT64)f.QuadPart != freq || (UINT64)now.QuadPart < qpc) return -1.0;
  return (double)((UINT64)now.QuadPart - qpc) * 1000.0 / (double)freq;
}

static void MemHex(const char *test, const char *name, const std::string &b, size_t n) {
  char hex[3 * 128 + 1] = {};
  size_t m = b.size() < n ? b.size() : n;
  if (m > 128) m = 128;
  for (size_t i = 0; i < m; ++i) sprintf_s(hex + 3 * i, sizeof(hex) - 3 * i, "%02x ", (unsigned char)b[i]);
  Out("%s rcpt %s bytes=%u hex=%s", test, name, (unsigned)b.size(), hex);
}

static void MemReceipts(const char *test, const char *when) {
  std::string d;
  Out("%s rcpt when=%s", test, when);
  if (MemRegBin(L"Wom1M2PoolDemand", d)) {
    // render_pool_demand.h ADMISSION_POOL_DEMAND_RECEIPT (v15 752 B; v16 768 B adds AdmitGranules@752,
    // SegmentRefusals@760). Stats at 24; LastReserveFailure at 312 (Reason +48); RefusalCounts[4] at 400.
    const UINT64 g = MemU32(d, 8) ? MemU32(d, 8) : 65536;
    Out("%s rcpt pool_demand version=%u bytes=%u last_window_result=%u segment_mib=%llu committed_mib=%llu "
        "peak_mib=%llu commit_failures=%llu places=%llu kills=%llu unaligned=%llu commit_us_max=%llu "
        "new_slab_failures=%llu reserved_mib=%llu reserve_failures=%llu floor_refusals=%llu "
        "last_fail_request_mib=%llu last_fail_reserved_mib=%llu last_fail_reason=%u refusals=%llu/%llu/%llu/%llu "
        "headroom_refusals=%llu admit_mib=%llu segment_refusals=%llu",
        test, MemU32(d, 0), (unsigned)d.size(), MemU32(d, 12), MemU64(d, 16) / MiB, MemU64(d, 24) * g / MiB,
        MemU64(d, 32) * g / MiB, MemU64(d, 48), MemU64(d, 80), MemU64(d, 96), MemU64(d, 120), MemU64(d, 152),
        MemU64(d, 232), MemU64(d, 272) / MiB, MemU64(d, 288), MemU64(d, 304), MemU64(d, 312) / MiB,
        MemU64(d, 320) / MiB, MemU32(d, 312 + 48), MemU64(d, 400), MemU64(d, 408), MemU64(d, 416),
        MemU64(d, 424), MemU64(d, 744), d.size() >= 768 ? MemU64(d, 752) * g / MiB : 0ull,
        d.size() >= 768 ? MemU64(d, 760) : 0ull);
  } else {
    Out("%s rcpt pool_demand present=0", test);
  }
  if (MemRegBin(L"Wom1PagingExecFailure", d)) {
    // render_paging.h ADMISSION_PAGING_EXEC_FAILURE v1 (112 B)
    Out("%s rcpt exec_failure version=%u failures=%u last_status=0x%08x kind=%u index=%u dst_seg=%u src_seg=%u "
        "dst_ipa=0x%llx src_ipa=0x%llx dst_va=0x%llx src_va=0x%llx record_bytes=%u records=%u fence=%u node=%u "
        "contract_completions=%u contract_status=0x%08x contract_op=%u",
        test, MemU32(d, 0), MemU32(d, 8), MemU32(d, 12), MemU32(d, 16), MemU32(d, 20), MemU32(d, 24), MemU32(d, 28),
        MemU64(d, 32), MemU64(d, 40), MemU64(d, 48), MemU64(d, 56), MemU32(d, 64), MemU32(d, 68), MemU32(d, 72),
        MemU32(d, 76), MemU32(d, 88), MemU32(d, 92), MemU32(d, 96));
  } else {
    Out("%s rcpt exec_failure present=0", test);
  }
  static const wchar_t *kHexNames[] = {L"Wom1G3PagingFailure", L"Wom1GpuReset", L"Wom1G3Poison",
                                       L"Wom1G3BrokerFailures", L"Wom1G3PagingStatus", L"Wom1G3PagingProfile",
                                       // probe13: KMD submit refusal, omitted/unpublished leaves, native PTE refusal
                                       L"Wom1G4SubmitFailure", L"Wom1G4OmittedLeaves", L"Wom1G3UnpublishedGroups",
                                       L"Wom1G3NativePteRefusal"};
  for (const wchar_t *n : kHexNames) {
    char name[64];
    sprintf_s(name, "%ls", n);
    if (MemRegBin(n, d)) MemHex(test, name, d, 64);
    else Out("%s rcpt %s present=0", test, name);
  }
  if (MemRegBin(L"Wom1DdiStatus", d)) {
    // render_ddi_status.h: 32-byte header, then 48-byte slots {Count, Qpc, Allocation, Object, Operation, Status}
    // (31 CreateAllocation, 38 SubmitCommand, 39 BuildPagingBuffer, 47 ResetFromTimeout, 58 ResetEngine,
    // 112+op internal paging status)
    // header {Version, Bytes, Slots, SlotBytes, Qpc@16, Frequency@24}; slot {Count, Qpc@8, Allocation@16, ...}.
    // The KMD rewrites this value only when a slot gets its FIRST failure, so counts can be old: snapshot_age_ms.
    const UINT64 freq = MemU64(d, 24);
    unsigned shown = 0;
    for (size_t off = 32, slot = 0; off + 48 <= d.size() && shown < 16; off += 48, ++slot) {
      UINT64 count = MemU64(d, off);
      if (!count) continue;
      Out("%s rcpt ddi_status slot=%u count=%llu op=%u status=0x%08x alloc=0x%llx first_age_ms=%.0f", test,
          (unsigned)slot, count, MemU32(d, off + 32), MemU32(d, off + 36), MemU64(d, off + 16),
          MemQpcAgeMs(MemU64(d, off + 8), freq));
      ++shown;
    }
    Out("%s rcpt ddi_status bytes=%u failed_slots_shown=%u snapshot_age_ms=%.0f", test, (unsigned)d.size(), shown,
        MemQpcAgeMs(MemU64(d, 16), freq));
  } else {
    Out("%s rcpt ddi_status present=0", test);
  }
}

static void TestMemLadder2() {
  UINT largestPass = 0, firstFail = 0;
  ULONGLONG prevClose = 0;
  UINT64 baseUsage = ~0ull;
  for (UINT si = 0; si < g_ladderCount; ++si) {
    const UINT mib = g_ladder[si];
    if (MemSkip(mib)) continue;
    D12 s;
    if (!D12Open(s)) { D12Close(s); Out("memladder2 size_mib=%u device=FAIL", mib); break; }
    LUID luid = s.dev->GetAdapterLuid();
    {  // probe13: what is still held before this size (previous device's buffers, pool reservation/commit)
      DXGI_QUERY_VIDEO_MEMORY_INFO pl, pn;
      MemLocal(luid, &pl, &pn);
      if (baseUsage == ~0ull) baseUsage = pl.CurrentUsage;
      ULONGLONG settleMs = 0;
      if (g_settleMs && prevClose) {
        const ULONGLONG t = GetTickCount64();
        while (pl.CurrentUsage > baseUsage + 16 * MiB && GetTickCount64() - t < g_settleMs) {
          Sleep(100);
          MemLocal(luid, &pl, &pn);
        }
        settleMs = GetTickCount64() - t;
      }
      std::string d;
      const bool pd = MemRegBin(L"Wom1M2PoolDemand", d);
      const UINT64 g = pd && MemU32(d, 8) ? MemU32(d, 8) : 65536;
      Out("memladder2 pre size_mib=%u since_prev_close_ms=%llu settle_ms=%llu local_usage_mib=%llu base_usage_mib=%llu "
          "pool_reserved_mib=%llu pool_committed_mib=%llu",
          mib, prevClose ? GetTickCount64() - prevClose : 0ull, settleMs,
          (unsigned long long)(pl.CurrentUsage / MiB), (unsigned long long)(baseUsage / MiB),
          pd ? MemU64(d, 272) / MiB : 0ull, pd ? MemU64(d, 24) * g / MiB : 0ull);
    }
    ID3D12Resource *up = nullptr, *rb = nullptr, *big = nullptr;
    const UINT64 bytes = (UINT64)mib * MiB, chunks = bytes / kMemChunk;
    const UINT32 seed = 0x5266u + mib;
    ULONGLONG t0 = 0, createMs = 0;
    HRESULT hr = E_FAIL;
    auto makeBig = [&]() {
      t0 = GetTickCount64();
      hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON,
                     &big);
      createMs = GetTickCount64() - t0;
    };
    if (g_bigFirst) makeBig();  // probe14
    HRESULT hu = D12Buffer(s.dev, D3D12_HEAP_TYPE_UPLOAD, kMemChunk, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_GENERIC_READ, &up);
    HRESULT hb = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, kMemChunk, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COPY_DEST, &rb);
    if (FAILED(hu) || FAILED(hb)) {
      Out("memladder2 size_mib=%u staging upload_hr=0x%08lx readback_hr=0x%08lx order=%s", mib, (unsigned long)hu,
          (unsigned long)hb, g_bigFirst ? "bigfirst" : "stagingfirst");
      Rel(big); Rel(up); Rel(rb); D12Close(s);
      if (!firstFail) firstFail = mib;
      continue;
    }
    if (!g_bigFirst) makeBig();
    HRESULT r0 = s.dev->GetDeviceRemovedReason();
    Sleep(500);
    HRESULT r1 = s.dev->GetDeviceRemovedReason();
    DXGI_QUERY_VIDEO_MEMORY_INFO l, n;
    MemLocal(luid, &l, &n);
    const char *stage = SUCCEEDED(hr) ? "ok" : "create";
    bool gpuOk = SUCCEEDED(hr) && r1 == S_OK;
    if (SUCCEEDED(hr) && r1 != S_OK) stage = "removed_after_create";
    UINT64 mism = 0, bad = 0, chunksOk = 0;
    UINT32 got = 0, want = 0;
    // probe: first and last chunk only, so "copy into the upper half" and "any copy" are told apart
    const UINT64 probeChunks[2] = {0, chunks ? chunks - 1 : 0};
    for (int pi = 0; gpuOk && pi < 2; ++pi) {
      const UINT64 c = probeChunks[pi];
      void *p = nullptr;
      D3D12_RANGE none = {0, 0};
      if (FAILED(up->Map(0, &none, &p)) || !p) { gpuOk = false; stage = pi ? "probe_last_map" : "probe_first_map"; break; }
      MemFill((UINT32 *)p, c * kMemChunk, kMemChunk, seed);
      up->Unmap(0, nullptr);
      if (!D12Copy(s, big, c * kMemChunk, up, 0, kMemChunk, "memladder2_probe_write")) {
        gpuOk = false; stage = pi ? "probe_last_copy" : "probe_first_copy"; break;
      }
      // probe13: D12Wait does not see a removal (the fence completes), so ask after each copy
      if (s.dev->GetDeviceRemovedReason() != S_OK) {
        gpuOk = false; stage = pi ? "probe_last_write_removed" : "probe_first_write_removed"; break;
      }
      if (!D12Copy(s, rb, 0, big, c * kMemChunk, kMemChunk, "memladder2_probe_read")) {
        gpuOk = false; stage = pi ? "probe_last_copy" : "probe_first_copy"; break;
      }
      if (s.dev->GetDeviceRemovedReason() != S_OK) {
        gpuOk = false; stage = pi ? "probe_last_read_removed" : "probe_first_read_removed"; break;
      }
      D3D12_RANGE all = {0, (SIZE_T)kMemChunk};
      if (FAILED(rb->Map(0, &all, &p)) || !p) { gpuOk = false; stage = "probe_readback_map"; break; }
      UINT64 b1 = 0; UINT32 g1 = 0, w1 = 0;
      UINT64 m = MemCheck((const volatile UINT32 *)p, c * kMemChunk, kMemChunk, seed, &b1, &g1, &w1);
      D3D12_RANGE nothing = {0, 0};
      rb->Unmap(0, &nothing);
      Out("memladder2 size_mib=%u probe chunk=%llu offset_mib=%llu mismatches=%llu first_bad=0x%llx", mib, c,
          c * 64, m, b1);
      if (m) { gpuOk = false; stage = pi ? "probe_last_content" : "probe_first_content"; mism += m; bad = b1; got = g1; want = w1; }
    }
    for (UINT64 c = 0; gpuOk && c < chunks; ++c) {
      void *p = nullptr;
      D3D12_RANGE none = {0, 0};
      if (FAILED(up->Map(0, &none, &p)) || !p) { gpuOk = false; stage = "map_upload"; break; }
      MemFill((UINT32 *)p, c * kMemChunk, kMemChunk, seed);
      up->Unmap(0, nullptr);
      if (!D12Copy(s, big, c * kMemChunk, up, 0, kMemChunk, "memladder2_write")) { gpuOk = false; stage = "write"; }
    }
    for (UINT64 c = 0; gpuOk && c < chunks; ++c) {
      if (!D12Copy(s, rb, 0, big, c * kMemChunk, kMemChunk, "memladder2_read")) { gpuOk = false; stage = "read"; break; }
      void *p = nullptr;
      D3D12_RANGE all = {0, (SIZE_T)kMemChunk};
      if (FAILED(rb->Map(0, &all, &p)) || !p) { gpuOk = false; stage = "map_readback"; break; }
      UINT64 b1 = 0; UINT32 g1 = 0, w1 = 0;
      UINT64 m = MemCheck((const volatile UINT32 *)p, c * kMemChunk, kMemChunk, seed, &b1, &g1, &w1);
      D3D12_RANGE nothing = {0, 0};
      rb->Unmap(0, &nothing);
      if (!m) ++chunksOk;
      if (m && !mism) { bad = b1; got = g1; want = w1; }
      mism += m;
    }
    HRESULT removed = s.dev->GetDeviceRemovedReason();
    bool pass = SUCCEEDED(hr) && gpuOk && mism == 0 && removed == S_OK && chunksOk == chunks;
    if (pass) largestPass = mib; else if (!firstFail) firstFail = mib;
    Out("memladder2 d3d12 size_mib=%u create_hr=0x%08lx create_ms=%llu removed_at_create=0x%08lx "
        "removed_500ms=0x%08lx stage=%s chunks_ok=%llu/%llu mismatches=%llu first_bad=0x%llx got=0x%08x want=0x%08x "
        "local_usage_mib=%llu local_budget_mib=%llu removed=0x%08lx ms=%llu result=%s order=%s",
        mib, (unsigned long)hr, createMs, (unsigned long)r0, (unsigned long)r1, stage, chunksOk, chunks, mism, bad,
        got, want, (unsigned long long)(l.CurrentUsage / MiB), (unsigned long long)(l.Budget / MiB),
        (unsigned long)removed, (unsigned long long)(GetTickCount64() - t0), pass ? "PASS" : "FAIL",
        g_bigFirst ? "bigfirst" : "stagingfirst");
    Rel(big);
    Rel(up);
    Rel(rb);
    D12Close(s);
    prevClose = GetTickCount64();
    if (!pass) {
      Sleep(1500);  // let the paging receipt work item write its values
      char tag[48];
      sprintf_s(tag, "memladder2_fail_%u", mib);
      MemReceipts("memladder2", tag);
    }
  }
  Out("memladder2 summary largest_pass_mib=%u first_fail_mib=%u", largestPass, firstFail);
}

static void TestFill3() {
  const UINT kMax = 20;
  const UINT64 kHeap = 256 * MiB, kTag = 64 * 1024;
  D12 s;
  if (!D12Open(s)) { D12Close(s); return; }
  LUID luid = s.dev->GetAdapterLuid();
  MemBudgetLine("fill3", "start", luid);
  ID3D12Heap *heaps[kMax] = {};
  ID3D12Resource *bufs[kMax] = {};
  ID3D12Resource *up = nullptr, *rb = nullptr;
  HRESULT hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_UPLOAD, 2 * kTag, D3D12_RESOURCE_FLAG_NONE,
                         D3D12_RESOURCE_STATE_GENERIC_READ, &up);
  if (SUCCEEDED(hr))
    hr = D12Buffer(s.dev, D3D12_HEAP_TYPE_READBACK, 2 * kTag, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST,
                   &rb);
  if (FAILED(hr)) { Out("fill3 staging hr=0x%08lx", (unsigned long)hr); Rel(up); Rel(rb); D12Close(s); return; }
  auto tagCheck = [&](UINT i) -> int {  // 1 ok, 0 mismatch, -1 copy/map failure
    if (!D12Copy(s, rb, 0, bufs[i], 0, kTag, "fill3_read0") ||
        !D12Copy(s, rb, kTag, bufs[i], kHeap - kTag, kTag, "fill3_read1"))
      return -1;
    void *p = nullptr;
    D3D12_RANGE all = {0, (SIZE_T)(2 * kTag)};
    if (FAILED(rb->Map(0, &all, &p)) || !p) return -1;
    UINT64 bad = 0; UINT32 g = 0, w = 0;
    UINT64 m = MemCheck((const volatile UINT32 *)p, (UINT64)i * kHeap, kTag, 0xF1120000u, &bad, &g, &w) +
               MemCheck((const volatile UINT32 *)((char *)p + kTag), (UINT64)i * kHeap + kHeap - kTag, kTag,
                        0xF1120000u, &bad, &g, &w);
    D3D12_RANGE nothing = {0, 0};
    rb->Unmap(0, &nothing);
    if (m) Out("fill3 tag_bad heap=%u mismatches=%llu first_bad=0x%llx got=0x%08x want=0x%08x", i + 1, m, bad, g, w);
    return m ? 0 : 1;
  };
  UINT n = 0, tagsNow = 0;
  HRESULT stop = S_OK;
  const UINT limit = g_memcap ? (g_memcap / 256u < kMax ? g_memcap / 256u : kMax) : kMax;
  for (; n < limit; ++n) {
    D3D12_HEAP_DESC hd = {};
    hd.SizeInBytes = kHeap;
    hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    ULONGLONG t0 = GetTickCount64();
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
    ULONGLONG createMs = GetTickCount64() - t0;
    void *p = nullptr;
    D3D12_RANGE none = {0, 0};
    bool ok = SUCCEEDED(up->Map(0, &none, &p)) && p;
    if (ok) {
      MemFill((UINT32 *)p, (UINT64)n * kHeap, kTag, 0xF1120000u);
      MemFill((UINT32 *)((char *)p + kTag), (UINT64)n * kHeap + kHeap - kTag, kTag, 0xF1120000u);
      up->Unmap(0, nullptr);
      ok = D12Copy(s, bufs[n], 0, up, 0, kTag, "fill3_tag0") &&
           D12Copy(s, bufs[n], kHeap - kTag, up, kTag, kTag, "fill3_tag1");
    }
    int now = ok ? tagCheck(n) : -1;
    if (now == 1) ++tagsNow;
    DXGI_QUERY_VIDEO_MEMORY_INFO l, nl;
    MemLocal(luid, &l, &nl);
    Out("fill3 heap=%u total_mib=%llu create_ms=%llu tag_now=%d local_usage_mib=%llu local_budget_mib=%llu "
        "removed=0x%08lx", n + 1, (unsigned long long)((n + 1) * (kHeap / MiB)), createMs, now,
        (unsigned long long)(l.CurrentUsage / MiB), (unsigned long long)(l.Budget / MiB),
        (unsigned long)s.dev->GetDeviceRemovedReason());
    if (now != 1) { ++n; break; }
  }
  UINT tagsEnd = 0, checked = 0;
  for (UINT i = 0; i < n; ++i) {
    if (!bufs[i]) continue;
    ++checked;
    if (tagCheck(i) == 1) ++tagsEnd;
  }
  HRESULT removed = s.dev->GetDeviceRemovedReason();
  Out("fill3 heaps_256mib=%u total_mib=%u stop_hr=0x%08lx tags_now=%u/%u tags_end=%u/%u removed=0x%08lx cap_mib=%u", n,
      n * 256u, (unsigned long)stop, tagsNow, n, tagsEnd, checked, (unsigned long)removed, limit * 256u);
  for (UINT i = 0; i < kMax; ++i) { Rel(bufs[i]); Rel(heaps[i]); }
  Rel(up);
  Rel(rb);
  MemBudgetLine("fill3", "released", luid);
  D12Close(s);
  Sleep(1500);
  MemReceipts("fill3", "end");
}
