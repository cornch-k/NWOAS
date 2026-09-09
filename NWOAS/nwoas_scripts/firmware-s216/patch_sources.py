"""Modify only the caller's already-snapshotted temporary S161 candidate."""
from pathlib import Path
import difflib

def patch(repo,out):
 pkg=repo/'Silicon/Apple/AppleSiliconPkg'
 adt=pkg/'PrePi/AdtParser.c'
 mem=repo/'Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c'
 header=pkg/'Include/Library/NwoasHandoffRanges.h'
 assert not header.exists()
 header.write_bytes((out/'NwoasHandoffRanges.h').read_bytes())
 for p in [adt,mem]:
  before=p.read_text();s=before
  needle='#include <Library/NwoasGuestRam.h>'
  assert s.count(needle)==1;s=s.replace(needle,'#include <Library/NwoasHandoffRanges.h>\n'+needle)
  if p==adt:
   old='''  struct boot_args *BootArgs = (struct boot_args *)BootArgsAddr;

  CopyMem(PcdBootArgsDest, BootArgsAddr, sizeof(struct boot_args));
  CopyMem(PcdAdtDest, (VOID*)BootArgs->devtree - BootArgs->virt_base + BootArgs->phys_base, BootArgs->devtree_size);'''
   new='''  // Save the source first: a destination copy must not invalidate later reads.
  struct boot_args SavedBootArgs;
  CopyMem (&SavedBootArgs, BootArgsAddr, sizeof (SavedBootArgs));
  struct boot_args *BootArgs = &SavedBootArgs;
  unsigned long long ReserveSize, AdtSource;
  if (sizeof (SavedBootArgs) > 0x4000 ||
      !NwoasHandoffRange (BootArgs->phys_base, BootArgs->mem_size,
                         PcdGet64 (PcdFdBaseAddress), PcdGet32 (PcdFdSize),
                         (UINT64)PcdBootArgsDest, (UINT64)PcdAdtDest,
                         BootArgs->devtree_size, &ReserveSize) ||
      !NwoasHandoffAdtSource (BootArgs->virt_base, (UINT64)BootArgs->devtree,
                             BootArgs->phys_base, BootArgs->mem_size,
                             BootArgs->devtree_size, &AdtSource)) {
    DEBUG ((DEBUG_ERROR, "HVLOG: S216 invalid/overlapping handoff copy FD=%llx BA=%llx ADT=%llx size=%x -- halt\\n",
            PcdGet64 (PcdFdBaseAddress), (UINT64)PcdBootArgsDest,
            (UINT64)PcdAdtDest, BootArgs->devtree_size));
    CpuDeadLoop ();
    return FALSE;
  }
  // ADT first: its source may otherwise be overwritten by the BootArgs copy.
  CopyMem(PcdAdtDest, (VOID *)(UINTN)AdtSource, BootArgs->devtree_size);
  CopyMem(PcdBootArgsDest, BootArgs, sizeof(struct boot_args));'''
   assert s.count(old)==1;s=s.replace(old,new)
  else:
   # Existing helper has one success break. Give the new caller a checked result.
   a=s.index('STATIC VOID ReserveMemoryRegion');b=s.index('//Borrowed from ArmPlatformPkg',a)
   fn=s[a:b];assert fn.count('      break;')==1
   fn=fn.replace('STATIC VOID ReserveMemoryRegion','STATIC BOOLEAN ReserveMemoryRegion').replace('      break;','      return TRUE;')
   j=fn.rfind('}');fn=fn[:j]+'  return FALSE;\n'+fn[j:];s=s[:a]+fn+s[b:]
   marker='  //reserve secondary stacks carveouts passed into cpm-impl-reg'
   insert='''  // S216: DT libraries retain these fixed copies beyond PrePi. Keep them
  // out of DXE allocation and the OS map, independently of FD placement.
  {
    struct boot_args *BootArgs = (struct boot_args *)(UINTN)PcdGet64 (PcdBootArgsPointer);
    unsigned long long ReserveSize;
    if (!NwoasHandoffRange (PcdGet64 (PcdSystemMemoryBase), PcdGet64 (PcdSystemMemorySize),
                           PcdGet64 (PcdFdBaseAddress), PcdGet32 (PcdFdSize),
                           PcdGet64 (PcdBootArgsPointer), PcdGet64 (PcdAdtPointer),
                           BootArgs->devtree_size, &ReserveSize) ||
        !ReserveMemoryRegion (PcdGet64 (PcdBootArgsPointer), (UINT32)ReserveSize)) {
      DEBUG ((DEBUG_ERROR, "HVLOG: S216 could not reserve handoff copies -- halt\\n"));
      CpuDeadLoop ();
      return EFI_DEVICE_ERROR;
    }
    DEBUG ((DEBUG_ERROR, "HVLOG: S216 reserved handoff base=%llx bytes=%llx\\n",
            PcdGet64 (PcdBootArgsPointer), ReserveSize));
  }

'''
   assert s.count(marker)==1;s=s.replace(marker,insert+marker)
  p.write_text(s)
  name='adt' if p==adt else 'memory'
  (out/(name+'-handoff.patch')).write_text(''.join(difflib.unified_diff(before.splitlines(True),s.splitlines(True),fromfile='a/'+str(p.relative_to(repo)),tofile='b/'+str(p.relative_to(repo)))))

 inf=mem.with_suffix('.inf')
 before=inf.read_text()
 assert 'PcdBootArgsPointer' not in before and 'PcdAdtPointer' not in before
 # The fixed platform PCD values are already declared in the DSC.
 after=before+'\n[FixedPcd]\n  gAppleSiliconPkgTokenSpaceGuid.PcdBootArgsPointer\n  gAppleSiliconPkgTokenSpaceGuid.PcdAdtPointer\n'
 inf.write_text(after)
 (out/'memory-inf-handoff.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+str(inf.relative_to(repo)),tofile='b/'+str(inf.relative_to(repo)))))
