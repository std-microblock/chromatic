import { ScriptLifecycle as NativeScriptLifecycle } from 'chromatic';

export const Script = {
  /**
   * Register a callback to be called before script dispose/reload.
   * Returns a disposable handle with a `remove()` method.
   *
   * @example
   * const handle = Script.onDispose(() => {
   *   Interceptor.detachAll();
   *   console.log('Cleaning up before reload...');
   * });
   * // Later, to unregister:
   * handle.remove();
   */
  onDispose(callback: () => void): { remove: () => void } {
    const id = NativeScriptLifecycle.onDispose(callback);
    return {
      remove() {
        NativeScriptLifecycle.removeDisposeCallback(id);
      },
    };
  },
};
