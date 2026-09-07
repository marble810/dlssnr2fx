# Same-process Natron isolation: two nodes, identical params, different inputs.
# Existing test/natron_two_nodes.py uses different params and cannot catch this bug.
#
# Usage:
#   set DLSSNR_IN=%CD%\test\test_frame.png
#   set DLSSNR_OUT1=%CD%\nr_out\natron_a.png
#   set DLSSNR_OUT2=%CD%\nr_out\natron_b.png
#   NatronRenderer -p test\natron_cache_same_params.py
#
# Natron has previously crashed after a successful write. File existence is not
# the same as a clean process exit.
import os

infile = os.environ.get('DLSSNR_IN')
out1 = os.environ.get('DLSSNR_OUT1')
out2 = os.environ.get('DLSSNR_OUT2')
if not infile or not out1 or not out2:
    raise RuntimeError('set DLSSNR_IN, DLSSNR_OUT1, DLSSNR_OUT2')

app.addFormat('128x128 1.0 dlssnr_test')
opts = app.getProjectParam('outputFormat').getOptions()
idx = opts.index('128x128') if '128x128' in opts else (len(opts) - 1)
app.getProjectParam('outputFormat').setValue(idx)
try:
    app.frameRange.set(1, 1)
except Exception:
    app.frameRange.setValue(1, 1)

reader = app.createReader(infile)
grade = app.createNode('net.sf.openfx.Grade')
if grade is None:
    grade = app.createNode('net.sf.openfx.ColorCorrectPlugin')
if grade is None:
    raise RuntimeError('need a second, different image path; Grade node not found')
grade.connectInput(0, reader)
gain = grade.getParam('gain') or grade.getParam('white') or grade.getParam('multiply')
if gain is not None:
    try:
        gain.setValue(0.25, 0)
        gain.setValue(0.25, 1)
        gain.setValue(0.25, 2)
    except Exception:
        try:
            gain.setValue(0.25)
        except Exception:
            pass

fx1 = app.createNode('com.dlss5ofx.DLSSNeural')
fx2 = app.createNode('com.dlss5ofx.DLSSNeural')
if fx1 is None or fx2 is None:
    raise RuntimeError('OFX plugin not found')
fx1.connectInput(0, reader)
fx2.connectInput(0, grade)

def apply_look(fx):
    fx.getParam('intensity').setValue(1.0)
    fx.getParam('style').setValue(2)
    fx.getParam('detail').setValue(1.0)
    coalesce = fx.getParam('fg_coalesce')
    if coalesce is not None:
        try:
            coalesce.setValue(True)
        except Exception:
            coalesce.setValue(1)
    hl = fx.getParam('hl_protect')
    if hl is not None:
        try:
            hl.setValue(0)
        except Exception:
            hl.setValue(False)

apply_look(fx1)
apply_look(fx2)

def write_node(src, path):
    writer = app.createNode('fr.inria.openfx.WritePNG')
    p = writer.getParam('filename') or writer.getParam('file')
    p.setValue(path)
    writer.connectInput(0, src)
    app.render(writer, 1, 1)
    print('wrote', path, os.path.exists(path))

write_node(fx1, out1)
write_node(fx2, out2)
if not (os.path.exists(out1) and os.path.exists(out2)):
    raise RuntimeError('missing Natron outputs')
if os.path.getsize(out1) == 0 or os.path.getsize(out2) == 0:
    raise RuntimeError('empty Natron outputs')
if open(out1, 'rb').read() == open(out2, 'rb').read():
    raise RuntimeError('same-param nodes reused one result')
print('natron_cache_same_params: files differ')
