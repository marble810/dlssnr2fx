// Run in AE (File > Scripts > Run Script File), or evaluate via BridgeTalk.
// Creates/removes its own fixtures; does not edit the active composition.
// Then run: python test/aex_ui_smoke.py
(function () {
  var folder = new Folder(Folder.temp.fsName + '/dlssnr-aex-ui-smoke');
  if (!folder.exists && !folder.create()) throw new Error('Cannot create smoke output directory');
  var outputs = ['source', 'mask', 'source-again', 'mask-white', 'mask-half', 'mask-zero'];
  for (var i = 0; i < outputs.length; ++i) {
    var old = new File(folder.fsName + '/' + outputs[i] + '.png');
    if (old.exists && !old.remove()) throw new Error('Cannot remove stale fixture: ' + old.fsName);
  }
  var comp = null, source = null;
  try {
    comp = app.project.items.addComp('__dlssnr_aex_ui_smoke__', 256, 256, 1, 1, 24);
    var layer = comp.layers.addSolid([0.15, 0.05, 0.10], '__dlssnr_aex_ui_smoke_source__', 256, 256, 1, 1);
    source = layer.source;
    var effect = layer.property('ADBE Effect Parade').addProperty('ADBE DLSS Neural Rendering');
    if (!effect) throw new Error('DLSS Neural Rendering is not installed');
    // Stable disk IDs: no guide inference or NGX is needed for these Views.
    effect.property('ADBE DLSS Neural Rendering-0026').setValue(1);
    effect.property('ADBE DLSS Neural Rendering-0036').setValue(1);
    var view = effect.property('ADBE DLSS Neural Rendering-0001');
    view.setValue(2);
    comp.saveFrameToPng(0, new File(folder.fsName + '/source.png'));
    view.setValue(5);
    if (view.value !== 5) throw new Error('Cannot set View=Highlight Mask');
    comp.saveFrameToPng(0, new File(folder.fsName + '/mask.png'));
    effect.property('ADBE DLSS Neural Rendering-0019').setValue(0);
    effect.property('ADBE DLSS Neural Rendering-0020').setValue(0);
    effect.property('ADBE DLSS Neural Rendering-0021').setValue(0);
    var mix = effect.property('ADBE DLSS Neural Rendering-0022');
    mix.setValue(1);
    comp.saveFrameToPng(0, new File(folder.fsName + '/mask-white.png'));
    mix.setValue(0.5);
    comp.saveFrameToPng(0, new File(folder.fsName + '/mask-half.png'));
    mix.setValue(0);
    comp.saveFrameToPng(0, new File(folder.fsName + '/mask-zero.png'));
    view.setValue(2);
    comp.saveFrameToPng(0, new File(folder.fsName + '/source-again.png'));
    return 'AE View smoke outputs: ' + folder.fsName;
  } finally {
    if (comp) comp.remove();
    if (source && source.usedIn.length === 0) source.remove();
  }
})();
