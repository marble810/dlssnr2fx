# Same-process cache isolation: identical params, same frame, different inputs.
# Usage:
#   set DLSSNR_EXR=F:/Design_Sync/dlssnr2fx/classroom/Export/export0002.exr
#   set DLSSNR_OUT_DIR=%CD%\nr_out
#   Nuke15.1.exe --tg test\nuke_cache_case.py
import os
import sys

import nuke

nuke.ofxMenu('')

exr = os.environ.get('DLSSNR_EXR', r'F:/Design_Sync/dlssnr2fx/classroom/Export/export0002.exr')
out_dir = os.environ.get('DLSSNR_OUT_DIR', os.path.abspath('nr_out'))
os.makedirs(out_dir, exist_ok=True)
out_a = os.path.join(out_dir, 'nuke_cache_a.exr')
out_b = os.path.join(out_dir, 'nuke_cache_b.exr')

nuke.root()['format'].setValue('HD_1080')
nuke.frame(2)
reader = nuke.nodes.Read(file=exr)
print('READ_CHANNELS', reader.channels(), flush=True)
shuffled = nuke.nodes.Shuffle(inputs=[reader])
shuffled['in'].setValue('interior_Combined')
source_a = shuffled
source_b = nuke.nodes.Multiply(inputs=[shuffled], value=0.35)

def make_fx(src):
    fx = nuke.createNode('DLSS Neural Rendering', inpanel=False)
    fx.setInput(0, src)
    for k, v in (('style', 2), ('intensity', 1.0), ('detail', 1.0), ('color', 1.0),
                 ('mvec_source', 0), ('depth_source', 0), ('fg_mode', 1),
                 ('fg_coalesce', True), ('hl_protect', False)):
        if k in fx.knobs():
            fx[k].setValue(v)
    return fx

fx_a = make_fx(source_a)
fx_b = make_fx(source_b)

def write_node(src, path):
    w = nuke.nodes.Write(inputs=[src], file=path, file_type='exr', channels='rgba')
    w['datatype'].setValue('32 bit float')
    nuke.execute(w, 2, 2)
    return w

write_node(fx_a, out_a)
write_node(fx_b, out_b)

ra = nuke.nodes.Read(file=out_a)
rb = nuke.nodes.Read(file=out_b)
nuke.frame(2)
diff = 0
same = 0
for y in range(100, 1000, 32):
    for x in range(100, 1800, 32):
        da = (float(ra.sample('rgba.red', x, y)), float(ra.sample('rgba.green', x, y)),
              float(ra.sample('rgba.blue', x, y)))
        db = (float(rb.sample('rgba.red', x, y)), float(rb.sample('rgba.green', x, y)),
              float(rb.sample('rgba.blue', x, y)))
        if da == db:
            same += 1
        else:
            diff += 1

print('CACHE_DIFF', diff, 'same', same, flush=True)
if diff == 0:
    print('FAIL: A and B reused the same result under identical params', flush=True)
    sys.exit(1)
print('nuke_cache_case: PASS', flush=True)
