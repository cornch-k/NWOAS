// S127: the platform connect list covers USB only. Bind the registered RAM-
// backed NVMe PCI facade explicitly before boot options are enumerated.
STATIC VOID
NwoasConnectNvme (VOID)
{
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0, Index;
  EFI_STATUS Status;
  NON_DISCOVERABLE_DEVICE *Device;
  Status = gBS->LocateHandleBuffer (ByProtocol,
             &gEdkiiNonDiscoverableDeviceProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "HVLOG: S127 NVMe locate %r\n", Status));
    return;
  }
  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (Handles[Index],
               &gEdkiiNonDiscoverableDeviceProtocolGuid, (VOID **)&Device);
    if (!EFI_ERROR (Status) &&
        CompareGuid (Device->Type, &gEdkiiNonDiscoverableNvmeDeviceGuid)) {
      DEBUG ((DEBUG_ERROR, "HVLOG: S127 NVMe connect begin\n"));
      Status = gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
      DEBUG ((DEBUG_ERROR, "HVLOG: S127 NVMe connect result %r\n", Status));
    }
  }
  FreePool (Handles);
}
