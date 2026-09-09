        if(misr != 0 && eisr != 0){
            for(int lr = 0; lr < 8; lr++){
                if(eisr & BIT(lr)){
                    u64 lr_val = hv_vgic3_read_lr(lr);
                    u64 intd = (lr_val >> ICH_LR_VIRTUAL_SHIFT) & ICH_LR_VIRTUAL_MASK;
                    nwoas_nvme_note_eoi(intd, ctx->elr);
                    hv_vgic3_write_lr(lr, 0);
                    if(intd > 31)
                        aic_set_mask(intd, false);//TODO: check distributor
                }
            }
        }
