#preprocess
// Linked with --pre-js, before workers are created or Wasm memory is allocated.
Module["__bindingCleanupInitialization"] = () => {
    // A preRun exception does not call abort(). Stop initialization and its
    // dependency watcher without resolving dependencies or replacing the error.
    ABORT = true;
#if ASSERTIONS
    if (typeof runDependencyWatcher !== "undefined" && runDependencyWatcher !== null) {
        clearInterval(runDependencyWatcher);
        runDependencyWatcher = null;
    }
#endif
#if PTHREADS
    // Module.PThread is exported only after memory allocation, so an early
    // failure must use the internal manager captured through this scope.
    if (typeof PThread !== "undefined") PThread.terminateAllThreads();
#endif
};
