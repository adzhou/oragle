/*
 * Copyright (C) 2013, 2014 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "FTLExitValue.h"

#if ENABLE(FTL_JIT)

#include "JSCInlines.h"

namespace JSC { namespace FTL {

void ExitValue::dumpInContext(PrintStream& out, DumpContext* context) const
{
    switch (kind()) {
    case InvalidExitValue:
        out.print("Invalid");
        return;
    case ExitValueDead:
        out.print("Dead");
        return;
    case ExitValueArgument:
        out.print("Argument(", exitArgument(), ")");
        return;
    case ExitValueConstant:
        out.print("Constant(", inContext(constant(), context), ")");
        return;
    case ExitValueInJSStack:
        out.print("InJSStack:r", virtualRegister());
        return;
    case ExitValueInJSStackAsInt32:
        out.print("InJSStackAsInt32:r", virtualRegister());
        return;
    case ExitValueInJSStackAsInt52:
        out.print("InJSStackAsInt52:r", virtualRegister());
        return;
    case ExitValueInJSStackAsDouble:
        out.print("InJSStackAsDouble:r", virtualRegister());
        return;
    case ExitValueRecovery:
        out.print("Recovery(", recoveryOpcode(), ", arg", leftRecoveryArgument(), ", arg", rightRecoveryArgument(), ", ", recoveryFormat(), ")");
        return;
    }
    
    RELEASE_ASSERT_NOT_REACHED();
}

void ExitValue::dump(PrintStream& out) const
{
    dumpInContext(out, 0);
}

} } // namespace JSC::FTL

#endif // ENABLE(FTL_JIT)

