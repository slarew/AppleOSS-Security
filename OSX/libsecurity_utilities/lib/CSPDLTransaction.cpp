/*
 * Copyright (c) 2015 Apple Inc. All Rights Reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include "CSPDLTransaction.h"
#include <Security/SecBasePriv.h>
#include <syslog.h>

DLTransaction::DLTransaction(CSSM_DL_DB_HANDLE dldbh)
    : mDldbh(dldbh), mSuccess(false), mFinalized(false), mAutoCommit(CSSM_TRUE) {
    initialize();
}

DLTransaction::DLTransaction()
    : mSuccess(false), mFinalized(false), mAutoCommit(CSSM_TRUE) {
}

void DLTransaction::initialize() {
    // Turn off autocommit on the underlying DL and remember the old state.
    Security::CssmClient::ObjectImpl::check(CSSM_DL_PassThrough(mDldbh,
                CSSM_APPLEFILEDL_TOGGLE_AUTOCOMMIT,
                NULL, reinterpret_cast<void **>(&mAutoCommit)));
}

DLTransaction::~DLTransaction() {
    finalize();
}

void DLTransaction::success() {
    mSuccess = true;
}

void DLTransaction::finalize() {
    if(mFinalized) {
        return;
    }

    // If autocommit wasn't on on the database when we started, don't
    // actually commit. There might be something else going on...
    if(mAutoCommit) {
        // if this transaction was a success, commit. Otherwise, roll back.
        if(mSuccess) {
            Security::CssmClient::ObjectImpl::check(CSSM_DL_PassThrough(mDldbh,
                        CSSM_APPLEFILEDL_COMMIT, NULL, NULL));
            CSSM_DL_PassThrough(mDldbh, CSSM_APPLEFILEDL_TOGGLE_AUTOCOMMIT,
                    reinterpret_cast<const void *>(mAutoCommit), NULL);
        } else {
            // This is a failed transaction. Roll back, and turn autoCommit back on.
            //
            // Note that we're likely (but not necessarily) unwinding the stack for an exception right now.
            // (If this transaction succeeded, we wouldn't be here. So, it failed, and this code likes to fail with exceptions.)
            // If this throws an exception, we might crash the whole process.
            // Swallow exceptions whole, but log them aggressively.
            try {
                CSSM_DL_PassThrough(mDldbh, CSSM_APPLEFILEDL_ROLLBACK, NULL, NULL);
                CSSM_DL_PassThrough(mDldbh, CSSM_APPLEFILEDL_TOGGLE_AUTOCOMMIT,
                        reinterpret_cast<const void *>(mAutoCommit), NULL);
            } catch(CssmError cssme) {
                const char* errStr = cssmErrorString(cssme.error);
                secdebugfunc("integrity", "caught CssmError during transaction rollback: %d %s", (int) cssme.error, errStr);
                syslog(LOG_ERR, "ERROR: failed to rollback keychain transaction: %d %s", (int) cssme.error, errStr);
            }
        }
    }
    mFinalized = true;
}


CSPDLTransaction::CSPDLTransaction(Security::CssmClient::Db& db)
    : DLTransaction(), mDb(db) {
    // Get the handle of the DL underlying this CSPDL.
    mDb->passThrough(CSSM_APPLECSPDL_DB_GET_HANDLE, NULL,
            reinterpret_cast<void **>(&mDldbh));

    initialize();
}

CSPDLTransaction::~CSPDLTransaction() {
    finalize();
}

