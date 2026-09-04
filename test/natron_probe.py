print("addFormat", app.addFormat)
app.addFormat("128x128 1.0 dlssnr_test")
fmt = app.getProjectParam("outputFormat")
print("type", type(fmt), "dir", [n for n in dir(fmt) if not n.startswith("_")])
for meth in ("getNumOptions", "getNumEntries", "getOptions", "getOption", "getValue"):
    if hasattr(fmt, meth):
        try:
            print(meth, getattr(fmt, meth)())
        except TypeError:
            try:
                print(meth, "0", getattr(fmt, meth)(0))
            except Exception as e:
                print(meth, e)
print("outputFormat attr", app.outputFormat)
print("frameRange", app.frameRange, app.frameRange.get() if hasattr(app.frameRange, "get") else app.frameRange)
