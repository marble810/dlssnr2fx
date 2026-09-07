# Isolated-process HDR false-colour probe.
# Usage:
#   set DLSSNR_EXR=F:/Design_Sync/dlssnr2fx/classroom/Export/export0002.exr
#   set DLSSNR_OUT=%CD%\nr_out\nuke_hdr.exr
#   Nuke15.1.exe --tg test\nuke_hdr_case.py
#
# Do not reuse this process to switch inputs; that is a cache test, not an HDR test.
import os
import sys

import nuke

nuke.ofxMenu('')

exr = os.environ.get('DLSSNR_EXR', r'F:/Design_Sync/dlssnr2fx/classroom/Export/export0002.exr')
out = os.environ.get('DLSSNR_OUT', os.path.abspath(os.path.join('nr_out', 'nuke_hdr.exr')))
os.makedirs(os.path.dirname(out) or '.', exist_ok=True)

nuke.root()['format'].setValue('HD_1080')
nuke.frame(2)
reader = nuke.nodes.Read(file=exr)
print('READ_CHANNELS', reader.channels(), flush=True)
source = nuke.nodes.Shuffle(inputs=[reader])
source['in'].setValue('interior_Combined')

fx = nuke.createNode('DLSS Neural Rendering', inpanel=False)
fx.setInput(0, source)
for k, v in (('style', 2), ('mvec_source', 0), ('depth_source', 0), ('fg_mode', 1),
             ('fg_coalesce', False), ('hl_protect', False)):
    if k in fx.knobs():
        fx[k].setValue(v)

writer = nuke.nodes.Write(inputs=[fx], file=out, file_type='exr', channels='rgba')
writer['datatype'].setValue('32 bit float')
nuke.execute(writer, 2, 2)

probe = nuke.nodes.Read(file=out)
nuke.frame(2)
max_rgb = 0.0
neg = 0
green = 0
samples = 0
for y in range(600, 1080, 8):
    for x in range(500, 1300, 8):
        r = float(probe.sample('rgba.red', x, y))
        g = float(probe.sample('rgba.green', x, y))
        b = float(probe.sample('rgba.blue', x, y))
        samples += 1
        max_rgb = max(max_rgb, r, g, b)
        if r < 0 or g < 0 or b < 0:
            neg += 1
        if g > r * 1.15 and g > b * 1.5:
            green += 1

print('HDR_OUT', out, 'max', max_rgb, 'neg', neg, 'green_roi', green, 'samples', samples, flush=True)
if max_rgb <= 1.0:
    print('FAIL: HDR output lost values above 1', flush=True)
    sys.exit(1)
if green >= 100:
    print('FAIL: desk/chair ROI has green false-colour blocks', flush=True)
    sys.exit(1)
print('nuke_hdr_case: PASS', flush=True)
