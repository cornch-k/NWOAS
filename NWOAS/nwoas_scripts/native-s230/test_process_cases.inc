
static void io_fresh(void){fresh();physical_result=1;physical_calls=link_calls=0;link_mode=0;
 submit(5,0xf0001,3,0x60000);ack_admin();submit(1,0xf0001,0x10001,0x70000);ack_admin();}
static struct nwoas_nvme_guest_cmd *io_cmd(u8 op,u32 ns,u64 lba){
 struct nwoas_nvme_guest_cmd *c=(void *)(ram+0x70000+nwoas_nvme_fp.sq_tail*64);
 memset(c,0,sizeof(*c));c->opcode=op;c->nsid=ns;c->cid=123;c->cdw10=(u32)lba;c->cdw11=lba>>32;c->prp1=0x80000;return c;}
static void io_go(void){write_reg(0x1008,(nwoas_nvme_fp.sq_tail+1)%nwoas_nvme_fp.sq_size);}
static u16 io_status(void){struct nwoas_nvme_guest_cqe *c=(void *)(ram+0x60000);assert(c->cid==123 && c->sq_id==1 && c->sq_head==1);assert((c->status&1)==1);return c->status>>1;}
int main(void){
 io_fresh();io_cmd(2,1,10);io_go();assert(physical_calls==1 && io_status()==0 && fast_level && nwoas_nvme_fp.commands==1);write_reg(0x100c,1);assert(!fast_level);
 io_fresh();io_cmd(1,1,10);io_go();assert(physical_calls==0 && io_status()==NWOAS_NVME_SC_WRITE_RO && !(read_reg(0x1c)&2));
 io_fresh();io_cmd(2,1,NWOAS_NVME_NAMESPACE_LBAS);io_go();assert(physical_calls==0 && io_status()==NWOAS_NVME_SC_LBA_RANGE);
 io_fresh();io_cmd(1,1,NWOAS_NVME_WRITE_FIRST_LBA)->cdw12=BIT(30);io_go();assert(physical_calls==1 && flush_count==1 && io_status()==0);
 io_fresh();physical_result=NVME_GUEST_REFUSED;io_cmd(2,1,10);io_go();assert(io_status()==NWOAS_NVME_SC_INVALID_FIELD && !nwoas_nvme_fp.faulted);
 io_fresh();physical_result=NVME_GUEST_FAILED;io_cmd(2,1,10);io_go();assert(nwoas_nvme_fp.faulted && (read_reg(0x1c)&2) && !fast_level && !nwoas_nvme_fp.cq_pending && !nwoas_nvme_fp.commands);write_reg(0x14,0);assert(read_reg(0x1c)&2);
 io_fresh();fail_flush=true;io_cmd(0,1,0);io_go();assert(nwoas_nvme_fp.faulted && (read_reg(0x1c)&2) && !fast_level && !nwoas_nvme_fp.cq_pending);
 io_fresh();io_cmd(2,2,0);io_go();assert(link_calls==1 && physical_calls==0 && io_status()==0);assert(((struct nwoas_nvme_guest_cqe *)(ram+0x60000))->result==0x12345678);
 io_fresh();link_mode=1;io_cmd(2,2,0);io_go();assert(!nwoas_nvme_fp.cq_pending && !nwoas_nvme_fp.commands && nwoas_nvme_fp.deferred && !(read_reg(0x1c)&2));
 io_fresh();mismatch=true;io_cmd(2,1,10);io_go();assert(!physical_calls && nwoas_nvme_fp.faulted && (read_reg(0x1c)&2));
 io_fresh();for(unsigned i=0;i<15;i++){io_cmd(2,1,10);io_go();}assert(physical_calls==15 && nwoas_nvme_fp.cq_pending==15);
 io_cmd(2,1,10);io_go();assert(physical_calls==15 && nwoas_nvme_fp.deferred);write_reg(0x100c,1);assert(physical_calls==15);struct exc_info ctx={0};nwoas_nvme_fastpath_poll(&ctx);assert(physical_calls==16 && nwoas_nvme_fp.commands==16 && nwoas_nvme_fp.cq_phase==0 && nwoas_nvme_fp.sq_head==0);
 io_fresh();submit(9,6,0,0);ack_admin();unsigned flushed=flush_count;io_cmd(1,1,NWOAS_NVME_WRITE_FIRST_LBA);io_go();assert(flush_count==flushed+1 && physical_calls==1 && io_status()==0);
 io_fresh();fail_flush=true;io_cmd(1,1,NWOAS_NVME_WRITE_FIRST_LBA)->cdw12=BIT(30);io_go();assert(physical_calls==1 && nwoas_nvme_fp.faulted && nwoas_nvme_fp.sq_head==0 && nwoas_nvme_fp.errors==1 && !nwoas_nvme_fp.cq_pending);for(unsigned i=0;i<16;i++)assert(ram[0x60000+i]==0);
 for(int mode=2;mode<=4;mode++){io_fresh();link_mode=mode;io_cmd(2,2,0);io_go();assert(io_status()==NWOAS_NVME_SC_READ_ERROR && !nwoas_nvme_fp.link_busy);}
 io_fresh();link_mode=2;io_cmd(1,2,0);io_go();assert(io_status()==NWOAS_NVME_SC_WRITE_ERROR && !nwoas_nvme_fp.link_busy);
 io_fresh();assert(!nwoas_nvme_guest_ptr(0x13fff,2) && nwoas_nvme_guest_ptr(0x13ffe,2));
 io_fresh();io_cmd(2,1,10);memcpy(ram+0x70040,ram+0x70000,64);write_reg(0x1008,2);assert(physical_calls==1 && nwoas_nvme_fp.commands==1 && nwoas_nvme_fp.deferred);nwoas_nvme_fastpath_poll(&ctx);assert(physical_calls==2 && nwoas_nvme_fp.commands==2 && !nwoas_nvme_fp.deferred);
 for(unsigned v=0;v<9;v++){
  io_fresh();struct nwoas_nvme_guest_cmd *c=io_cmd(2,1,10);u16 want=NWOAS_NVME_SC_INVALID_FIELD;unsigned calls=0;
  switch(v){
  case 0:c->cdw10=NWOAS_NVME_NAMESPACE_LBAS-1;c->cdw12=1;want=NWOAS_NVME_SC_LBA_RANGE;break;
  case 1:c->opcode=1;c->cdw10=NWOAS_NVME_WRITE_LAST_LBA+1;want=NWOAS_NVME_SC_WRITE_RO;break;
  case 2:c->cdw12=BIT(16);break;
  case 3:c->flags=1;break;
  case 4:c->metadata=1;break;
  case 5:c->nsid=3;want=NWOAS_NVME_SC_INVALID_NSID;break;
  case 6:c->opcode=3;want=NWOAS_NVME_SC_INVALID_OPCODE;break;
  case 7:c->opcode=0;c->nsid=0xffffffff;c->cdw10=0;want=0;break;
  case 8:c->cdw10=NWOAS_NVME_NAMESPACE_LBAS-1;want=0;calls=1;break;
  }
  io_go();assert(io_status()==want && physical_calls==calls && !nwoas_nvme_fp.faulted);if(v==7)assert(flush_count==1);
 }
 puts("PASS production execute/process/poll/IRQ/link fixtures: read/write policy, FUA, recoverable refusal, terminal physical/flush errors, NS2 result/cancel, mapping fault, CQ backpressure and deferred process");
}
