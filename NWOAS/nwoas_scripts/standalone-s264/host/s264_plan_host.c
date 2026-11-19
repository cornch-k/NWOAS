/* SPDX-License-Identifier: MIT */
/* NWOAS S264 host model: runs the SAME planner the hypervisor runs (src/nwoas_s264_plan.c,
 * compiled with -DS264_HOST) on files instead of live memory, and writes the same result
 * blob as S264_OP_DRYRUN.  No hardware involved.
 *
 * usage: s264_plan_host --adt adt.bin --ba ba.bin --ba-rev N --heapblock-top HEX
 *                       --iodev N --midr HEX --amx HEX --actlr HEX
 *                       [--carveout BASE:SIZE]... [--payload-size N] [--flags HEX]
 *                       --out result.bin [--wifi-dump regs.bin]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nwoas_s264_plan.h"

static u8 *slurp(const char *path, u32 *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *b = malloc((size_t)n + 4096);
    memset(b, 0, (size_t)n + 4096);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (u32)n;
    return b;
}

int main(int argc, char **argv)
{
    static struct s264_inputs in;
    static struct s264_config cfg;
    static struct s264_plan pl;
    static struct s264_op ops[640];
    static u64 carve[128];
    const char *adt_path = NULL, *ba_path = NULL, *out_path = NULL, *wifi_path = NULL;
    s264_default_config(&cfg);
    in.iodev = S264_IODEV_USB0;
#ifdef S264_POL_LOCAL /* s264C plan header only: the s264B source (build-s264.sh) still compiles */
    /* s264C: --lower SPSR VBAR CODE  -> "ok NEW_SPSR NEW_ELR" or "refused" (s264_lower_calc) */
    if (argc == 5 && !strcmp(argv[1], "--lower")) {
        u64 ns = 0, ne = 0;
        if (s264_lower_calc(strtoull(argv[2], NULL, 0), strtoull(argv[3], NULL, 0),
                            (u32)strtoul(argv[4], NULL, 0), &ns, &ne))
            printf("ok 0x%llx 0x%llx\n", (unsigned long long)ns, (unsigned long long)ne);
        else
            printf("refused\n");
        return 0;
    }
#endif
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (!strcmp(a, "--adt"))
            adt_path = v, i++;
        else if (!strcmp(a, "--ba"))
            ba_path = v, i++;
        else if (!strcmp(a, "--out"))
            out_path = v, i++;
        else if (!strcmp(a, "--wifi-dump"))
            wifi_path = v, i++;
        else if (!strcmp(a, "--ba-rev"))
            in.ba_rev = (u32)strtoul(v, NULL, 0), i++;
        else if (!strcmp(a, "--heapblock-top"))
            in.heapblock_top = strtoull(v, NULL, 0), i++;
        else if (!strcmp(a, "--iodev"))
            in.iodev = (u32)strtoul(v, NULL, 0), i++;
        else if (!strcmp(a, "--midr"))
            in.midr = strtoull(v, NULL, 0), i++;
        else if (!strcmp(a, "--amx"))
            in.amx_config_cur = strtoull(v, NULL, 0), i++;
        else if (!strcmp(a, "--actlr"))
            in.actlr_cur = strtoull(v, NULL, 0), i++;
        else if (!strcmp(a, "--payload-size"))
            cfg.payload_size = strtoull(v, NULL, 0), i++;
        else if (!strcmp(a, "--flags"))
            cfg.flags = (u32)strtoul(v, NULL, 0), i++;
#ifdef S264_POL_LOCAL
        else if (!strcmp(a, "--nvme-qd")) /* s264C: depth | flags << 32 */
            cfg.nvme_qd = strtoull(v, NULL, 0), i++;
#endif
#ifdef S264_CFG_F_AUDIO_B
        else if (!strcmp(a, "--audio-prot")) /* s264D: 0 sense, 1 moderate, 2 full */
            cfg.audio_prot = strtoull(v, NULL, 0), i++;
#endif
#ifdef S264_CFG_F_PWR
        else if (!strcmp(a, "--pwr")) /* s264F: power_module.py env encoding (nwoas_s264_plan.h) */
            cfg.pwr = strtoull(v, NULL, 0), i++;
#endif
        else if (!strcmp(a, "--carveout")) {
            char *end;
            if (in.n_carveouts >= 64)
                return 2;
            carve[2 * in.n_carveouts] = strtoull(v, &end, 0);
            carve[2 * in.n_carveouts + 1] = strtoull(end + 1, NULL, 0);
            in.n_carveouts++;
            i++;
        } else {
            fprintf(stderr, "unknown argument %s\n", a);
            return 2;
        }
    }
    if (!adt_path || !ba_path || !out_path)
        return 2;
    u32 alen = 0, blen = 0;
    in.adt = slurp(adt_path, &alen);
    in.ba_raw = slurp(ba_path, &blen);
    if (!in.adt || !in.ba_raw || blen < 384) {
        fprintf(stderr, "cannot read inputs\n");
        return 2;
    }
    in.adt_size = alen;
    in.carveouts = carve;
    pl.ops = ops;
    pl.cap_ops = 640;
    pl.adt_cap = 0x400000;
    pl.adt = malloc(pl.adt_cap);
    int r = s264_plan_build(&in, &cfg, &pl);
    for (u32 i = 0; i < pl.n_ops; i++)
        if (pl.ops[i].kind == S264_OP_STEP && pl.n_steps < 40) {
            pl.steps[pl.n_steps].id = pl.ops[i].id;
            pl.steps[pl.n_steps].status = S264_ST_DRY;
            pl.steps[pl.n_steps].val = 0;
            pl.n_steps++;
        }
    static u8 out[0x800000];
    u32 n = s264_result_write(&in, &cfg, &pl, 0, out, sizeof(out));
    FILE *f = fopen(out_path, "wb");
    if (!f || fwrite(out, 1, n, f) != n)
        return 2;
    fclose(f);
    if (wifi_path) {
        /* control page as the guest would read it, 32-bit reads 0x0..0x1ffc in order */
        static struct s264_wifi_page wp;
        static u32 regs[0x800];
        int wr = s264_wifi_page_init(in.adt, in.adt_size, &wp);
        for (u32 off = 0; off < 0x2000; off += 4)
            regs[off / 4] = s264_wifi_reg_read(&wp, off);
        FILE *wf = fopen(wifi_path, "wb");
        if (!wf || fwrite(regs, 4, 0x800, wf) != 0x800 || wr)
            return 3;
        fclose(wf);
    }
    printf("s264_plan_host: err=%d ops=%u adt=%u bytes pmgr=%u result=%u bytes\n", r, pl.n_ops,
           pl.adt_len, pl.n_pmgr, n);
    return r ? 1 : 0;
}
