# NatronRenderer: Read -> com.dlss5ofx.DLSSNeural -> WritePNG at source resolution.
import os

infile = os.environ.get("DLSSNR_IN")
outfile = os.environ.get("DLSSNR_OUT")
if not infile or not outfile:
    raise RuntimeError("set DLSSNR_IN and DLSSNR_OUT")

app.addFormat("128x128 1.0 dlssnr_test")
opts = app.getProjectParam("outputFormat").getOptions()
idx = opts.index("128x128") if "128x128" in opts else (len(opts) - 1)
app.getProjectParam("outputFormat").setValue(idx)
try:
    app.frameRange.set(1, 1)
except Exception:
    app.frameRange.setValue(1, 1)

reader = app.createReader(infile)
print("reader", reader.getPluginID() if reader else None, "format idx", idx)

fx = app.createNode("com.dlss5ofx.DLSSNeural")
if fx is None:
    raise RuntimeError("OFX plugin com.dlss5ofx.DLSSNeural not found")
print("fx", fx.getPluginID())
fx.connectInput(0, reader)

intensity = os.environ.get("DLSSNR_INTENSITY")
style = os.environ.get("DLSSNR_STYLE")
if intensity:
    fx.getParam("intensity").setValue(float(intensity))
if style:
    fx.getParam("style").setValue(int(style))
hl = fx.getParam("hl_protect")
if hl is not None:
    try:
        hl.setValue(False)
    except Exception:
        try:
            hl.setValue(0)
        except Exception:
            pass

writer = app.createNode("fr.inria.openfx.WritePNG")
p = writer.getParam("filename") or writer.getParam("file")
p.setValue(outfile)
for name, val in (("frameRange", 1), ("firstFrame", 1), ("lastFrame", 1)):
    wp = writer.getParam(name)
    if wp is not None:
        try:
            wp.setValue(val)
        except Exception:
            pass
writer.connectInput(0, fx)
app.render(writer, 1, 1)
print("wrote", outfile, "exists", os.path.exists(outfile))
