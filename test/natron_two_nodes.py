# Two OFX instances in one NatronRenderer process.
import os

infile = os.environ.get("DLSSNR_IN")
out1 = os.environ.get("DLSSNR_OUT1")
out2 = os.environ.get("DLSSNR_OUT2")
if not infile or not out1 or not out2:
    raise RuntimeError("set DLSSNR_IN, DLSSNR_OUT1, DLSSNR_OUT2")

app.addFormat("128x128 1.0 dlssnr_test")
opts = app.getProjectParam("outputFormat").getOptions()
idx = opts.index("128x128") if "128x128" in opts else (len(opts) - 1)
app.getProjectParam("outputFormat").setValue(idx)
try:
    app.frameRange.set(1, 1)
except Exception:
    app.frameRange.setValue(1, 1)

reader = app.createReader(infile)
fx1 = app.createNode("com.dlss5ofx.DLSSNeural")
fx2 = app.createNode("com.dlss5ofx.DLSSNeural")
if fx1 is None or fx2 is None:
    raise RuntimeError("OFX plugin not found")
fx1.connectInput(0, reader)
fx2.connectInput(0, reader)
for fx, intensity, style in ((fx1, 1.0, 0), (fx2, 0.2, 2)):
    fx.getParam("intensity").setValue(intensity)
    fx.getParam("style").setValue(style)
    hl = fx.getParam("hl_protect")
    if hl is not None:
        try:
            hl.setValue(0)
        except Exception:
            hl.setValue(False)

def write_node(src, path):
    writer = app.createNode("fr.inria.openfx.WritePNG")
    p = writer.getParam("filename") or writer.getParam("file")
    p.setValue(path)
    writer.connectInput(0, src)
    app.render(writer, 1, 1)
    print("wrote", path, os.path.exists(path))

write_node(fx1, out1)
write_node(fx2, out2)
