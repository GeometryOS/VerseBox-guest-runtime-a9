/*
 * A9's kernel uapi binder.h predates BR_TRANSACTION_SEC_CTX (Android 10).
 *
 * Two constants must match the real kernel encoding, otherwise the bridge
 * speaks a dialect the guest libbinder does not understand:
 *
 *   FLAT_BINDER_FLAG_TXN_SECURITY_CTX = 0x1000  (NOT 0x100)
 *   BR_TRANSACTION_SEC_CTX            = _IOR('r', 2, struct
 *                                       binder_transaction_data_secctx)
 *
 * 0x100 is FLAT_BINDER_FLAG_ACCEPTS_FDS on every Android release.  Using it
 * for the security-context flag made every node that accepts FDs (including
 * servicemanager and hwservicemanager) look like a secctx receiver, so the
 * broker emitted BR_TRANSACTION_SEC_CTX.  Android 9 has no such command
 * (IPCThreadState::executeCommand has no case for it), so its libhwbinder hit
 * the default branch, logged "*** BAD COMMAND" and aborted with
 * UNKNOWN_ERROR (-2147483648).
 */
#ifndef A9VM_BINDER_COMPAT_H
#define A9VM_BINDER_COMPAT_H

#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/android/binder.h>
#include <linux/ioctl.h>

/* A9's uapi lacks the secctx wrapper struct. Layout matches AOSP 10:
 * the plain transaction followed by the security-context pointer. */
struct binder_transaction_data_secctx {
    struct binder_transaction_data transaction_data;
    binder_uintptr_t secctx;
};

#ifndef BR_TRANSACTION_SEC_CTX
#define BR_TRANSACTION_SEC_CTX _IOR('r', 2, struct binder_transaction_data_secctx)
#endif

/* AOSP 10 flat_binder_object flag; A9's uapi predates it. */
#ifndef FLAT_BINDER_FLAG_TXN_SECURITY_CTX
#define FLAT_BINDER_FLAG_TXN_SECURITY_CTX 0x1000
#endif

#endif
