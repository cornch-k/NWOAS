        if(misr != 0 && eisr != 0){
            for(int lr = 0; lr < 8; lr++){
                if(eisr & BIT(lr)){
                    u64 lr_val = hv_vgic3_read_lr(lr);
                    u64 intd = (lr_val >> ICH_LR_VIRTUAL_SHIFT) & ICH_LR_VIRTUAL_MASK;
                    /* S208: the enabled-vGIC hv_exc_irq path never calls
                     * hv_get_context(), so ctx->elr is uninitialized stack here
                     * (only sync/abort paths populate it). Read the architectural
                     * ELR_EL2 directly so the EOI PC diagnostic is accurate. This
                     * changes only the recorded PC; gap/mask/queue/delivery are
                     * untouched. */
                    nwoas_nvme_note_eoi(intd, hv_get_elr());
                    hv_vgic3_write_lr(lr, 0);
                    if(intd > 31)
                        aic_set_mask(intd, false);//TODO: check distributor
                }
            }
        }
