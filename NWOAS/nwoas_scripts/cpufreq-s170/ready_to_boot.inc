/* S170 one-shot normal operating-point selection after inner payload init. */
STATIC UINT64 NwoasPstateRead(VOID *Context, UINT64 Address)
{
  (VOID)Context;
  return MmioRead64((UINTN)Address);
}
STATIC VOID NwoasPstateWrite(VOID *Context, UINT64 Address, UINT64 Value)
{
  (VOID)Context;
  MmioWrite64((UINTN)Address, Value);
}
STATIC VOID NwoasPstateDelay(VOID *Context, UINT32 Microseconds)
{
  (VOID)Context;
  MicroSecondDelay(Microseconds);
}
STATIC VOID NwoasPstateReadyToBoot(VOID)
{
  dt_node_t *Chosen = dt_get("chosen");
  size_t Length = 0;
  VOID *Property;
  NWOAS_CPU_PSTATE_IO Io = {NwoasPstateRead, NwoasPstateWrite, NwoasPstateDelay, NULL, 0};
  NWOAS_CPU_PSTATE_RESULT Result;
  NWOAS_PSTATE_STATUS Status;
  if (Chosen == NULL) {
    DEBUG((DEBUG_ERROR, "HVLOG: S170 no chosen node; CPU state unchanged\n"));
    return;
  }
  Property = dt_prop(Chosen, "chip-id", &Length);
  if ((Property == NULL) || (Length != sizeof(UINT32))) {
    DEBUG((DEBUG_ERROR, "HVLOG: S170 no valid chip-id; CPU state unchanged\n"));
    return;
  }
  Io.chip_id = ReadUnaligned32((CONST UINT32 *)Property);
  Status = nwoas_cpu_pstate_enable_p12(&Io, &Result);
  DEBUG((DEBUG_ERROR,
    "HVLOG: S170 ReadyToBoot chip=%x status=%u initial=%u rollback=%u before=%lx requested=%lx final=%lx status_reg=%lx current=%u target=%u protected=%u\n",
    Io.chip_id, (UINT32)Status, (UINT32)Result.initial_status,
    (UINT32)Result.rollback_status, Result.baseline_cmd, Result.requested_cmd,
    Result.final_cmd, Result.final_status, Result.final_current,
    Result.final_target, (UINT32)Result.protected_preserved));
}
