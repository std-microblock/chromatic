#include "disable-integrity.h"
#include "blook/blook.h"

#include "Windows.h"
#include <memory>

#include "processthreadsapi.h"
bool chromatic::hooks::windows::disable_integrity() {
  static std::shared_ptr<blook::InlineHook> disable_integrity_hook;
  auto func = (blook::Pointer)(void *)GetProcAddress(
      LoadLibraryA("Kernel32.dll"), "UpdateProcThreadAttribute");

  if (!func) {
    return false;
  }

  disable_integrity_hook = func.as_function().inline_hook();
  disable_integrity_hook->install((
      void *)+[](LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList, DWORD dwFlags,
                 DWORD_PTR Attribute, PVOID lpValue, SIZE_T cbSize,
                 PVOID lpPreviousValue, PSIZE_T lpReturnSize) {
    if (Attribute == PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY &&
        cbSize >= sizeof(DWORD64)) {
      PDWORD64 old_policy = &((PDWORD64)lpValue)[0];
      *old_policy &= ~(
          DWORD64)(PROCESS_CREATION_MITIGATION_POLICY_BLOCK_NON_MICROSOFT_BINARIES_ALWAYS_ON);
      *old_policy &=
          (DWORD64)(PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON);
    }
    return disable_integrity_hook
        ->trampoline_t<int64_t(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR,
                               PVOID, SIZE_T, PVOID, PSIZE_T)>()(
            lpAttributeList, dwFlags, Attribute, lpValue, cbSize,
            lpPreviousValue, lpReturnSize);
  });
  return true;
}
