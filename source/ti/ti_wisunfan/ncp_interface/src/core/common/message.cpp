/*
 *  Copyright (c) 2016, The OpenThread Authors.
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
 *   This file implements the message buffer pool and message buffers.
 */

#include "message.hpp"

#include "common/code_utils.hpp"
#include "common/debug.hpp"
#include "common/instance.hpp"
#include "common/locator-getters.hpp"
#include "common/logging.hpp"
//#include "net/ip6.hpp"

namespace ot {

MessagePool::MessagePool(Instance &aInstance)
    : InstanceLocator(aInstance)
{
#if OPENTHREAD_CONFIG_PLATFORM_MESSAGE_MANAGEMENT
    // Initialize Platform buffer pool management.
    otPlatMessagePoolInit(&GetInstance(), kNumBuffers, sizeof(Buffer));
#else
    memset(mBuffers, 0, sizeof(mBuffers));

    mFreeBuffers = mBuffers;

    for (uint16_t i = 0; i < kNumBuffers - 1; i++)
    {
        mBuffers[i].SetNextBuffer(&mBuffers[i + 1]);
    }

    mBuffers[kNumBuffers - 1].SetNextBuffer(NULL);
    mNumFreeBuffers = kNumBuffers;
#endif
}

Message *MessagePool::New(uint8_t aType, uint16_t aReserveHeader, uint8_t aPriority)
{
    otError  error = OT_ERROR_NONE;
    Message *message;

    VerifyOrExit((message = static_cast<Message *>(NewBuffer(aPriority))) != NULL, OT_NOOP);

    memset(message, 0, sizeof(*message));
    message->SetMessagePool(this);
    message->SetType(aType);
    message->SetReserved(aReserveHeader);
    message->SetLinkSecurityEnabled(true);

    SuccessOrExit(error = message->SetPriority(aPriority));
    SuccessOrExit(error = message->SetLength(0));

exit:
    if (error != OT_ERROR_NONE)
    {
        Free(message);
        message = NULL;
    }

    return message;
}

Message *MessagePool::New(uint8_t aType, uint16_t aReserveHeader, const otMessageSettings *aSettings)
{
    Message *message;
    bool     linkSecurityEnabled;
    uint8_t  priority;

    if (aSettings == NULL)
    {
        linkSecurityEnabled = true;
        priority            = OT_MESSAGE_PRIORITY_NORMAL;
    }
    else
    {
        linkSecurityEnabled = aSettings->mLinkSecurityEnabled;
        priority            = aSettings->mPriority;
    }

    message = New(aType, aReserveHeader, priority);
    if (message)
    {
        message->SetLinkSecurityEnabled(linkSecurityEnabled);
    }

    return message;
}

void MessagePool::Free(Message *aMessage)
{
    OT_ASSERT(aMessage->Next() == NULL && aMessage->Prev() == NULL);

    FreeBuffers(static_cast<Buffer *>(aMessage));
}

Buffer *MessagePool::NewBuffer(uint8_t aPriority)
{
    Buffer *buffer = NULL;

    SuccessOrExit(ReclaimBuffers(1, aPriority));

#if OPENTHREAD_CONFIG_PLATFORM_MESSAGE_MANAGEMENT

    buffer = static_cast<Buffer *>(otPlatMessagePoolNew(&GetInstance()));

#else

    if (mFreeBuffers != NULL)
    {
        buffer       = mFreeBuffers;
        mFreeBuffers = mFreeBuffers->GetNextBuffer();
        buffer->SetNextBuffer(NULL);
        mNumFreeBuffers--;
    }

#endif

    if (buffer == NULL)
    {
        otLogInfoMem("No available message buffer");
    }

exit:
    return buffer;
}

void MessagePool::FreeBuffers(Buffer *aBuffer)
{
    while (aBuffer != NULL)
    {
        Buffer *tmpBuffer = aBuffer->GetNextBuffer();
#if OPENTHREAD_CONFIG_PLATFORM_MESSAGE_MANAGEMENT
        otPlatMessagePoolFree(&GetInstance(), aBuffer);
#else  // OPENTHREAD_CONFIG_PLATFORM_MESSAGE_MANAGEMENT
        aBuffer->SetNextBuffer(mFreeBuffers);
        mFreeBuffers = aBuffer;
        mNumFreeBuffers++;
#endif // OPENTHREAD_CONFIG_PLATFORM_MESSAGE_MANAGEMENT
        aBuffer = tmpBuffer;
    }
}

otError MessagePool::ReclaimBuffers(int aNumBuffers, uint8_t aPriority)
{
#if OPENTHREAD_MTD || OPENTHREAD_FTD
    while (aNumBuffers > GetFreeBufferCount())
    {
        SuccessOrExit(Get<MeshForwarder>().EvictMessage(aPriority));
    }

exit:
#else  // OPENTHREAD_MTD || OPENTHREAD_FTD
    OT_UNUSED_VARIABLE(aPriority);
#endif // OPENTHREAD_MTD || OPENTHREAD_FTD

    // First comparison is to get around issues with comparing
    // signed and unsigned numbers, if aNumBuffers is negative then
    // the second comparison wont be attempted.
    return (aNumBuffers < 0 || aNumBuffers <= GetFreeBufferCount()) ? OT_ERROR_NONE : OT_ERROR_NO_BUFS;
}

uint16_t MessagePool::GetFreeBufferCount(void) const
{
    uint16_t rval;

#if OPENTHREAD_CONFIG_PLATFORM_MESSAGE_MANAGEMENT
    rval = otPlatMessagePoolNumFreeBuffers(&GetInstance());
#else
    rval = mNumFreeBuffers;
#endif

    return rval;
}

otError Message::ResizeMessage(uint16_t aLength)
{
    otError error = OT_ERROR_NONE;

    // add buffers
    Buffer * curBuffer = this;
    Buffer * lastBuffer;
    uint16_t curLength = kHeadBufferDataSize;

    while (curLength < aLength)
    {
        if (curBuffer->GetNextBuffer() == NULL)
        {
            curBuffer->SetNextBuffer(GetMessagePool()->NewBuffer(GetPriority()));
            VerifyOrExit(curBuffer->GetNextBuffer() != NULL, error = OT_ERROR_NO_BUFS);
        }

        curBuffer = curBuffer->GetNextBuffer();
        curLength += kBufferDataSize;
    }

    // remove buffers
    lastBuffer = curBuffer;
    curBuffer  = curBuffer->GetNextBuffer();
    lastBuffer->SetNextBuffer(NULL);

    GetMessagePool()->FreeBuffers(curBuffer);

exit:
    return error;
}

void Message::Free(void)
{
    GetMessagePool()->Free(this);
}

Message *Message::GetNext(void) const
{
    Message *next;
    Message *tail;

    if (mBuffer.mHead.mInfo.mInPriorityQ)
    {
        PriorityQueue *priorityQueue = GetPriorityQueue();
        VerifyOrExit(priorityQueue != NULL, next = NULL);
        tail = priorityQueue->GetTail();
    }
    else
    {
        MessageQueue *messageQueue = GetMessageQueue();
        VerifyOrExit(messageQueue != NULL, next = NULL);
        tail = messageQueue->GetTail();
    }

    next = (this == tail) ? NULL : Next();

exit:
    return next;
}

otError Message::SetLength(uint16_t aLength)
{
    otError  error              = OT_ERROR_NONE;
    uint16_t totalLengthRequest = GetReserved() + aLength;
    uint16_t totalLengthCurrent = GetReserved() + GetLength();
    int      bufs               = 0;

    VerifyOrExit(totalLengthRequest >= GetReserved(), error = OT_ERROR_INVALID_ARGS);

    if (totalLengthRequest > kHeadBufferDataSize)
    {
        bufs = (((totalLengthRequest - kHeadBufferDataSize) - 1) / kBufferDataSize) + 1;
    }

    if (totalLengthCurrent > kHeadBufferDataSize)
    {
        bufs -= (((totalLengthCurrent - kHeadBufferDataSize) - 1) / kBufferDataSize) + 1;
    }

    SuccessOrExit(error = GetMessagePool()->ReclaimBuffers(bufs, GetPriority()));

    SuccessOrExit(error = ResizeMessage(totalLengthRequest));
    mBuffer.mHead.mInfo.mLength = aLength;

    // Correct offset in case shorter length is set.
    if (GetOffset() > aLength)
    {
        SetOffset(aLength);
    }

exit:
    return error;
}

uint8_t Message::GetBufferCount(void) const
{
    uint8_t rval = 1;

    for (const Buffer *curBuffer = GetNextBuffer(); curBuffer; curBuffer = curBuffer->GetNextBuffer())
    {
        rval++;
    }

    return rval;
}

otError Message::MoveOffset(int aDelta)
{
    otError error = OT_ERROR_NONE;

    OT_ASSERT(GetOffset() + aDelta <= GetLength());
    VerifyOrExit(GetOffset() + aDelta <= GetLength(), error = OT_ERROR_INVALID_ARGS);

    mBuffer.mHead.mInfo.mOffset += static_cast<int16_t>(aDelta);
    OT_ASSERT(mBuffer.mHead.mInfo.mOffset <= GetLength());

exit:
    return error;
}

otError Message::SetOffset(uint16_t aOffset)
{
    otError error = OT_ERROR_NONE;

    OT_ASSERT(aOffset <= GetLength());
    VerifyOrExit(aOffset <= GetLength(), error = OT_ERROR_INVALID_ARGS);

    mBuffer.mHead.mInfo.mOffset = aOffset;

exit:
    return error;
}

bool Message::IsSubTypeMle(void) const
{
    bool rval;

    switch (mBuffer.mHead.mInfo.mSubType)
    {
    case kSubTypeMleGeneral:
    case kSubTypeMleAnnounce:
    case kSubTypeMleDiscoverRequest:
    case kSubTypeMleDiscoverResponse:
    case kSubTypeMleChildUpdateRequest:
    case kSubTypeMleDataResponse:
    case kSubTypeMleChildIdRequest:
        rval = true;
        break;

    default:
        rval = false;
        break;
    }

    return rval;
}

otError Message::SetPriority(uint8_t aPriority)
{
    otError        error         = OT_ERROR_NONE;
    PriorityQueue *priorityQueue = NULL;

    VerifyOrExit(aPriority < kNumPriorities, error = OT_ERROR_INVALID_ARGS);

    VerifyOrExit(IsInAQueue(), mBuffer.mHead.mInfo.mPriority = aPriority);
    VerifyOrExit(mBuffer.mHead.mInfo.mPriority != aPriority, OT_NOOP);

    if (mBuffer.mHead.mInfo.mInPriorityQ)
    {
        priorityQueue = mBuffer.mHead.mInfo.mQueue.mPriority;
        priorityQueue->Dequeue(*this);
    }

    mBuffer.mHead.mInfo.mPriority = aPriority;

    if (priorityQueue != NULL)
    {
        priorityQueue->Enqueue(*this);
    }

exit:
    return error;
}

otError Message::Append(const void *aBuf, uint16_t aLength)
{
    otError  error     = OT_ERROR_NONE;
    uint16_t oldLength = GetLength();
    int      bytesWritten;

    SuccessOrExit(error = SetLength(GetLength() + aLength));
    bytesWritten = Write(oldLength, aLength, aBuf);

    OT_ASSERT(bytesWritten == (int)aLength);
    OT_UNUSED_VARIABLE(bytesWritten);

exit:
    return error;
}

otError Message::Prepend(const void *aBuf, uint16_t aLength)
{
    otError error     = OT_ERROR_NONE;
    Buffer *newBuffer = NULL;

    while (aLength > GetReserved())
    {
        VerifyOrExit((newBuffer = GetMessagePool()->NewBuffer(GetPriority())) != NULL, error = OT_ERROR_NO_BUFS);

        newBuffer->SetNextBuffer(GetNextBuffer());
        SetNextBuffer(newBuffer);

        if (GetReserved() < sizeof(mBuffer.mHead.mData))
        {
            // Copy payload from the first buffer.
            memcpy(newBuffer->mBuffer.mHead.mData + GetReserved(), mBuffer.mHead.mData + GetReserved(),
                   sizeof(mBuffer.mHead.mData) - GetReserved());
        }

        SetReserved(GetReserved() + kBufferDataSize);
    }

    SetReserved(GetReserved() - aLength);
    mBuffer.mHead.mInfo.mLength += aLength;
    SetOffset(GetOffset() + aLength);

    if (aBuf != NULL)
    {
        Write(0, aLength, aBuf);
    }

exit:
    return error;
}

void Message::RemoveHeader(uint16_t aLength)
{
    OT_ASSERT(aLength <= mBuffer.mHead.mInfo.mLength);

    mBuffer.mHead.mInfo.mReserved += aLength;
    mBuffer.mHead.mInfo.mLength -= aLength;

    if (mBuffer.mHead.mInfo.mOffset > aLength)
    {
        mBuffer.mHead.mInfo.mOffset -= aLength;
    }
    else
    {
        mBuffer.mHead.mInfo.mOffset = 0;
    }
}

uint16_t Message::Read(uint16_t aOffset, uint16_t aLength, void *aBuf) const
{
    Buffer * curBuffer;
    uint16_t bytesCopied = 0;
    uint16_t bytesToCopy;

    if (aOffset >= GetLength())
    {
        ExitNow();
    }

    if (aOffset + aLength >= GetLength())
    {
        aLength = GetLength() - aOffset;
    }

    aOffset += GetReserved();

    // special case first buffer
    if (aOffset < kHeadBufferDataSize)
    {
        bytesToCopy = kHeadBufferDataSize - aOffset;

        if (bytesToCopy > aLength)
        {
            bytesToCopy = aLength;
        }

        memcpy(aBuf, GetFirstData() + aOffset, bytesToCopy);

        aLength -= bytesToCopy;
        bytesCopied += bytesToCopy;
        aBuf = static_cast<uint8_t *>(aBuf) + bytesToCopy;

        aOffset = 0;
    }
    else
    {
        aOffset -= kHeadBufferDataSize;
    }

    // advance to offset
    curBuffer = GetNextBuffer();

    while (aOffset >= kBufferDataSize)
    {
        OT_ASSERT(curBuffer != NULL);

        curBuffer = curBuffer->GetNextBuffer();
        aOffset -= kBufferDataSize;
    }

    // begin copy
    while (aLength > 0)
    {
        OT_ASSERT(curBuffer != NULL);

        bytesToCopy = kBufferDataSize - aOffset;

        if (bytesToCopy > aLength)
        {
            bytesToCopy = aLength;
        }

        memcpy(aBuf, curBuffer->GetData() + aOffset, bytesToCopy);

        aLength -= bytesToCopy;
        bytesCopied += bytesToCopy;
        aBuf = static_cast<uint8_t *>(aBuf) + bytesToCopy;

        curBuffer = curBuffer->GetNextBuffer();
        aOffset   = 0;
    }

exit:
    return bytesCopied;
}

int Message::Write(uint16_t aOffset, uint16_t aLength, const void *aBuf)
{
    Buffer * curBuffer;
    uint16_t bytesCopied = 0;
    uint16_t bytesToCopy;

    OT_ASSERT(aOffset + aLength <= GetLength());

    if (aOffset + aLength >= GetLength())
    {
        aLength = GetLength() - aOffset;
    }

    aOffset += GetReserved();

    // special case first buffer
    if (aOffset < kHeadBufferDataSize)
    {
        bytesToCopy = kHeadBufferDataSize - aOffset;

        if (bytesToCopy > aLength)
        {
            bytesToCopy = aLength;
        }

        memcpy(GetFirstData() + aOffset, aBuf, bytesToCopy);

        aLength -= bytesToCopy;
        bytesCopied += bytesToCopy;
        aBuf = static_cast<const uint8_t *>(aBuf) + bytesToCopy;

        aOffset = 0;
    }
    else
    {
        aOffset -= kHeadBufferDataSize;
    }

    // advance to offset
    curBuffer = GetNextBuffer();

    while (aOffset >= kBufferDataSize)
    {
        OT_ASSERT(curBuffer != NULL);

        curBuffer = curBuffer->GetNextBuffer();
        aOffset -= kBufferDataSize;
    }

    // begin copy
    while (aLength > 0)
    {
        OT_ASSERT(curBuffer != NULL);

        bytesToCopy = kBufferDataSize - aOffset;

        if (bytesToCopy > aLength)
        {
            bytesToCopy = aLength;
        }

        memcpy(curBuffer->GetData() + aOffset, aBuf, bytesToCopy);

        aLength -= bytesToCopy;
        bytesCopied += bytesToCopy;
        aBuf = static_cast<const uint8_t *>(aBuf) + bytesToCopy;

        curBuffer = curBuffer->GetNextBuffer();
        aOffset   = 0;
    }

    return bytesCopied;
}

int Message::CopyTo(uint16_t aSourceOffset, uint16_t aDestinationOffset, uint16_t aLength, Message &aMessage) const
{
    uint16_t bytesCopied = 0;
    uint16_t bytesToCopy;
    uint8_t  buf[16];

    while (aLength > 0)
    {
        bytesToCopy = (aLength < sizeof(buf)) ? aLength : sizeof(buf);

        Read(aSourceOffset, bytesToCopy, buf);
        aMessage.Write(aDestinationOffset, bytesToCopy, buf);

        aSourceOffset += bytesToCopy;
        aDestinationOffset += bytesToCopy;
        aLength -= bytesToCopy;
        bytesCopied += bytesToCopy;
    }

    return bytesCopied;
}

Message *Message::Clone(uint16_t aLength) const
{
    otError  error = OT_ERROR_NONE;
    Message *messageCopy;
    uint16_t offset;

    VerifyOrExit((messageCopy = GetMessagePool()->New(GetType(), GetReserved(), GetPriority())) != NULL,
                 error = OT_ERROR_NO_BUFS);
    SuccessOrExit(error = messageCopy->SetLength(aLength));
    CopyTo(0, 0, aLength, *messageCopy);

    // Copy selected message information.
    offset = GetOffset() < aLength ? GetOffset() : aLength;
    messageCopy->SetOffset(offset);

    messageCopy->SetSubType(GetSubType());
    messageCopy->SetLinkSecurityEnabled(IsLinkSecurityEnabled());
#if OPENTHREAD_CONFIG_TIME_SYNC_ENABLE
    messageCopy->SetTimeSync(IsTimeSync());
#endif

exit:

    if (error != OT_ERROR_NONE && messageCopy != NULL)
    {
        messageCopy->Free();
        messageCopy = NULL;
    }

    return messageCopy;
}

bool Message::GetChildMask(uint16_t aChildIndex) const
{
    OT_ASSERT(aChildIndex < sizeof(mBuffer.mHead.mInfo.mChildMask) * 8);
    return (mBuffer.mHead.mInfo.mChildMask[aChildIndex / 8] & (0x80 >> (aChildIndex % 8))) != 0;
}

void Message::ClearChildMask(uint16_t aChildIndex)
{
    OT_ASSERT(aChildIndex < sizeof(mBuffer.mHead.mInfo.mChildMask) * 8);
    mBuffer.mHead.mInfo.mChildMask[aChildIndex / 8] &= ~(0x80 >> (aChildIndex % 8));
}

void Message::SetChildMask(uint16_t aChildIndex)
{
    OT_ASSERT(aChildIndex < sizeof(mBuffer.mHead.mInfo.mChildMask) * 8);
    mBuffer.mHead.mInfo.mChildMask[aChildIndex / 8] |= 0x80 >> (aChildIndex % 8);
}

bool Message::IsChildPending(void) const
{
    bool rval = false;

    for (size_t i = 0; i < sizeof(mBuffer.mHead.mInfo.mChildMask); i++)
    {
        if (mBuffer.mHead.mInfo.mChildMask[i] != 0)
        {
            ExitNow(rval = true);
        }
    }

exit:
    return rval;
}

uint16_t Message::UpdateChecksum(uint16_t aChecksum, uint16_t aValue)
{
    uint16_t result = aChecksum + aValue;
    return result + (result < aChecksum);
}

uint16_t Message::UpdateChecksum(uint16_t aChecksum, const void *aBuf, uint16_t aLength)
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(aBuf);

    for (int i = 0; i < aLength; i++)
    {
        aChecksum = UpdateChecksum(aChecksum, (i & 1) ? bytes[i] : static_cast<uint16_t>(bytes[i] << 8));
    }

    return aChecksum;
}

uint16_t Message::UpdateChecksum(uint16_t aChecksum, uint16_t aOffset, uint16_t aLength) const
{
    Buffer * curBuffer;
    uint16_t bytesCovered = 0;
    uint16_t bytesToCover;

    OT_ASSERT(aOffset + aLength <= GetLength());

    aOffset += GetReserved();

    // special case first buffer
    if (aOffset < kHeadBufferDataSize)
    {
        bytesToCover = kHeadBufferDataSize - aOffset;

        if (bytesToCover > aLength)
        {
            bytesToCover = aLength;
        }

        aChecksum = Message::UpdateChecksum(aChecksum, GetFirstData() + aOffset, bytesToCover);

        aLength -= bytesToCover;
        bytesCovered += bytesToCover;

        aOffset = 0;
    }
    else
    {
        aOffset -= kHeadBufferDataSize;
    }

    // advance to offset
    curBuffer = GetNextBuffer();

    while (aOffset >= kBufferDataSize)
    {
        OT_ASSERT(curBuffer != NULL);

        curBuffer = curBuffer->GetNextBuffer();
        aOffset -= kBufferDataSize;
    }

    // begin copy
    while (aLength > 0)
    {
        OT_ASSERT(curBuffer != NULL);

        bytesToCover = kBufferDataSize - aOffset;

        if (bytesToCover > aLength)
        {
            bytesToCover = aLength;
        }

        aChecksum = Message::UpdateChecksum(aChecksum, curBuffer->GetData() + aOffset, bytesToCover);

        aLength -= bytesToCover;
        bytesCovered += bytesToCover;

        curBuffer = curBuffer->GetNextBuffer();
        aOffset   = 0;
    }

    return aChecksum;
}

void Message::SetMessageQueue(MessageQueue *aMessageQueue)
{
    mBuffer.mHead.mInfo.mQueue.mMessage = aMessageQueue;
    mBuffer.mHead.mInfo.mInPriorityQ    = false;
}

void Message::SetPriorityQueue(PriorityQueue *aPriorityQueue)
{
    mBuffer.mHead.mInfo.mQueue.mPriority = aPriorityQueue;
    mBuffer.mHead.mInfo.mInPriorityQ     = true;
}

MessageQueue::MessageQueue(void)
{
    SetTail(NULL);
}

Message *MessageQueue::GetHead(void) const
{
    return (GetTail() == NULL) ? NULL : GetTail()->Next();
}

otError MessageQueue::Enqueue(Message &aMessage, QueuePosition aPosition)
{
    otError error = OT_ERROR_NONE;

    VerifyOrExit(!aMessage.IsInAQueue(), error = OT_ERROR_ALREADY);

    aMessage.SetMessageQueue(this);

    OT_ASSERT((aMessage.Next() == NULL) && (aMessage.Prev() == NULL));

    if (GetTail() == NULL)
    {
        aMessage.Next() = &aMessage;
        aMessage.Prev() = &aMessage;

        SetTail(&aMessage);
    }
    else
    {
        Message *head = GetTail()->Next();

        aMessage.Next() = head;
        aMessage.Prev() = GetTail();

        head->Prev()      = &aMessage;
        GetTail()->Next() = &aMessage;

        if (aPosition == kQueuePositionTail)
        {
            SetTail(&aMessage);
        }
    }

exit:
    return error;
}

otError MessageQueue::Dequeue(Message &aMessage)
{
    otError error = OT_ERROR_NONE;

    VerifyOrExit(aMessage.GetMessageQueue() == this, error = OT_ERROR_NOT_FOUND);

    OT_ASSERT((aMessage.Next() != NULL) && (aMessage.Prev() != NULL));

    if (&aMessage == GetTail())
    {
        SetTail(GetTail()->Prev());

        if (&aMessage == GetTail())
        {
            SetTail(NULL);
        }
    }

    aMessage.Prev()->Next() = aMessage.Next();
    aMessage.Next()->Prev() = aMessage.Prev();

    aMessage.Prev() = NULL;
    aMessage.Next() = NULL;

    aMessage.SetMessageQueue(NULL);

exit:
    return error;
}

void MessageQueue::GetInfo(uint16_t &aMessageCount, uint16_t &aBufferCount) const
{
    aMessageCount = 0;
    aBufferCount  = 0;

    for (const Message *message = GetHead(); message != NULL; message = message->GetNext())
    {
        aMessageCount++;
        aBufferCount += message->GetBufferCount();
    }
}

PriorityQueue::PriorityQueue(void)
{
    for (int priority = 0; priority < Message::kNumPriorities; priority++)
    {
        mTails[priority] = NULL;
    }
}

Message *PriorityQueue::FindFirstNonNullTail(uint8_t aStartPriorityLevel) const
{
    Message *tail = NULL;
    uint8_t  priority;

    priority = aStartPriorityLevel;

    do
    {
        if (mTails[priority] != NULL)
        {
            tail = mTails[priority];
            break;
        }

        priority = PrevPriority(priority);
    } while (priority != aStartPriorityLevel);

    return tail;
}

Message *PriorityQueue::GetHead(void) const
{
    Message *tail;

    tail = FindFirstNonNullTail(0);

    return (tail == NULL) ? NULL : tail->Next();
}

Message *PriorityQueue::GetHeadForPriority(uint8_t aPriority) const
{
    Message *head;
    Message *previousTail;

    if (mTails[aPriority] != NULL)
    {
        previousTail = FindFirstNonNullTail(PrevPriority(aPriority));

        OT_ASSERT(previousTail != NULL);

        head = previousTail->Next();
    }
    else
    {
        head = NULL;
    }

    return head;
}

Message *PriorityQueue::GetTail(void) const
{
    return FindFirstNonNullTail(0);
}

otError PriorityQueue::Enqueue(Message &aMessage)
{
    otError  error = OT_ERROR_NONE;
    uint8_t  priority;
    Message *tail;
    Message *next;

    VerifyOrExit(!aMessage.IsInAQueue(), error = OT_ERROR_ALREADY);

    aMessage.SetPriorityQueue(this);

    priority = aMessage.GetPriority();

    tail = FindFirstNonNullTail(priority);

    if (tail != NULL)
    {
        next = tail->Next();

        aMessage.Next() = next;
        aMessage.Prev() = tail;
        next->Prev()    = &aMessage;
        tail->Next()    = &aMessage;
    }
    else
    {
        aMessage.Next() = &aMessage;
        aMessage.Prev() = &aMessage;
    }

    mTails[priority] = &aMessage;

exit:
    return error;
}

otError PriorityQueue::Dequeue(Message &aMessage)
{
    otError  error = OT_ERROR_NONE;
    uint8_t  priority;
    Message *tail;

    VerifyOrExit(aMessage.GetPriorityQueue() == this, error = OT_ERROR_NOT_FOUND);

    priority = aMessage.GetPriority();

    tail = mTails[priority];

    if (&aMessage == tail)
    {
        tail = tail->Prev();

        if ((&aMessage == tail) || (tail->GetPriority() != priority))
        {
            tail = NULL;
        }

        mTails[priority] = tail;
    }

    aMessage.Next()->Prev() = aMessage.Prev();
    aMessage.Prev()->Next() = aMessage.Next();
    aMessage.Next()         = NULL;
    aMessage.Prev()         = NULL;

    aMessage.SetMessageQueue(NULL);

exit:
    return error;
}

void PriorityQueue::GetInfo(uint16_t &aMessageCount, uint16_t &aBufferCount) const
{
    aMessageCount = 0;
    aBufferCount  = 0;

    for (const Message *message = GetHead(); message != NULL; message = message->GetNext())
    {
        aMessageCount++;
        aBufferCount += message->GetBufferCount();
    }
}

} // namespace ot
