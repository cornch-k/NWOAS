#!/usr/bin/env python3
# Decode FbDumpDxe serial RLE dump -> PNG. The log contains the FBDUMP block 2-3x (inline HVLOG
# forwarding + m1n1 ring-buffer dumps), so we extract complete START..END blocks and pick the
# one with the most data. Pixel uint32 = 0x{res}{R}{G}{B} (PixelFormat 1 BGR).
import sys, re

def main():
    log = open(sys.argv[1], 'rb').read().decode('utf-8', 'replace')
    blocks = []
    cur = None
    for ln in log.splitlines():
        if 'FBDUMP START' in ln:
            m = re.search(r'FBDUMP START w=(\d+) h=(\d+)', ln)
            cur = {'w': int(m.group(1)), 'h': int(m.group(2)), 'hex': ''} if m else None
        elif 'FBDUMP END' in ln:
            if cur is not None:
                blocks.append(cur)
                cur = None
        elif cur is not None:
            mm = re.search(r'HVLOG: FB ([0-9a-fA-F]+)', ln)
            if mm:
                cur['hex'] += mm.group(1)
    if not blocks:
        print("no complete FBDUMP block (START..END) found"); sys.exit(2)
    b = max(blocks, key=lambda x: len(x['hex']))
    w, h, hexdata = b['w'], b['h'], re.sub(r'[^0-9a-fA-F]', '', b['hex'])
    pixels = []
    for i in range(0, len(hexdata) - 11, 12):
        cnt = int(hexdata[i:i+4], 16)
        val = int(hexdata[i+4:i+12], 16)
        if cnt == 0:
            continue
        R = (val >> 16) & 0xFF; G = (val >> 8) & 0xFF; B = val & 0xFF
        pixels.extend([(R, G, B)] * cnt)
        if len(pixels) > w * h + 4096:
            break
    print(f"blocks_found={len(blocks)}  chosen: w={w} h={h}  decoded_pixels={len(pixels)}  expected={w*h}")
    try:
        from PIL import Image
    except ImportError:
        import subprocess; subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", "--user", "Pillow"]); from PIL import Image
    img = Image.new('RGB', (w, h))
    data = pixels[:w*h] + [(0, 0, 0)] * max(0, w*h - len(pixels))
    img.putdata(data)
    out = sys.argv[2] if len(sys.argv) > 2 else "/private/tmp/claude-501/-Volumes-X31-NWOAS/08e1ec24-7e98-4f1e-91a5-4c4a8348b6ab/scratchpad/fb_error.png"
    img.resize((w*3, h*3), Image.NEAREST).save(out)
    print("saved", out)

if __name__ == "__main__":
    main()
