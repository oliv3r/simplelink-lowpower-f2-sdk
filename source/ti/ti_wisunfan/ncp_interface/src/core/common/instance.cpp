/*
 *  Copyright (c) 2016-2017, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   This file implements the OpenThread Instance class.
 */

#include "instance.hpp"

#include <openthread/platform/misc.h>

#include "common/logging.hpp"
#include "common/new.hpp"

namespace ot {

// Define the raw storage used for NCP instance (in single-instance case).
OT_DEFINE_ALIGNED_VAR(gInstanceRaw, sizeof(Instance), uint64_t);

Instance::Instance(void)
    : mTaskletScheduler()
    , mTimerMilliScheduler(*this)
    , mIsInitialized(false)
    , mMessagePool(*this)
{
}

Instance &Instance::InitSingle(void)
{
    Instance *instance = &Get();

    VerifyOrExit(!instance->mIsInitialized, OT_NOOP);

    instance = new (&gInstanceRaw) Instance();

    instance->AfterInit();

exit:
    return *instance;
}

Instance &Instance::Get(void)
{
    void *instance = &gInstanceRaw;

    return *static_cast<Instance *>(instance);
}


void Instance::Reset(void)
{
    otPlatReset(this);
}

void Instance::AfterInit(void)
{
    mIsInitialized = true;
}

void Instance::Finalize(void)
{
    VerifyOrExit(mIsInitialized, OT_NOOP);

    mIsInitialized = false;

    /**
     * Object was created on buffer, so instead of deleting
     * the object we call destructor explicitly.
     */
    this->~Instance();

exit:
    return;
}

} // namespace ot
