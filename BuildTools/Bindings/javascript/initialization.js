#preprocess
#if PTHREADS
// Linked with --pre-js, before workers are created or Wasm memory is allocated.
// Capture the internal manager through this module's scope: Module.PThread is
// exported only after memory allocation and may be unavailable when it fails.
Module["__bindingCleanupInitialization"] = () => {
    if (typeof PThread !== "undefined") PThread.terminateAllThreads();
};
#endif
