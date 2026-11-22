# TesmioLoader plugin headers (vendored)

- Source: https://github.com/MaxLegend/TesmioLoader, `src/tesmio_api.h` and
  `src/tesmio_plugin.h`, commit `775b9d90a70ebb05b837e4d038389a3b40097941`
  (2026-11-15), unmodified. Plugin API version 4, accepted range 3..4.
- Licence: **GPL-3.0** (TesmioLoader). Compiling `tesmio_plugin.h` into the DLL
  makes the DLL a GPL-3.0 work: anyone you give the DLL to is entitled to its
  source. Every public WRSR plugin built on these headers is GPL-3.0 for this
  reason.
- Republic Mod Loader (RML) hosts plugins built against this API through its
  Tesmio compatibility layer, so one DLL serves both loaders.

To update: copy both files from a newer TesmioLoader checkout, bump the commit
above, and check `TSM_API_VERSION` / `TSM_API_VERSION_MIN` in `tesmio_api.h`.
