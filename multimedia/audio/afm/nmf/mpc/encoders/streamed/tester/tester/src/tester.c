/*****************************************************************************/
/*
 * Copyright (C) ST-Ericsson SA 2009,2010. All rights reserved.
 * This code is ST-Ericsson proprietary and confidential.
 * Any use of the code for whatever purpose is subject to
 * specific written permission of ST-Ericsson SA.
 *
 */

/**
 * \file   tester.c
 * \brief  
 * \author ST-Ericsson
 */
/*****************************************************************************/
#include "encoders/streamed/tester/tester.nmf"
#include <stdio.h>
#include <stdlib.h>
#include "dbc.h"
#include "string.h"
#include "fsm/component/include/Component.inl"
#include "common_interface.h"


#define MAX_NB_BUFFER 1

#define OUT 1
#define IN  0

////////////////////////////////////////////////////////////
//			Global Variables
////////////////////////////////////////////////////////////

static Buffer_t mInBuffer[MAX_NB_BUFFER]; 
static Buffer_t mOutBuffer[MAX_NB_BUFFER];
static void *   mFifoIn[MAX_NB_BUFFER];
static void *   mFifoOut[MAX_NB_BUFFER];
static Port     mPorts[2];
Component       mTester;

static FILE *   mFIN = NULL;
static char *   mOutputFile = NULL;
static FILE *   mFOUT = NULL;

static int      mFileSize;
static int      mNbWrapperBufIn;
static int      mNbWrapperBufOut;
static int      mNbsample;
static int      mBufferSize;
static int      mCurrentwriteBuf;
static int      mCurrentencodedBuf;
static int      mEosReached;

static t_uint16 mSampleBlockSize;
static t_uint16 mNbChannel;
static t_uint16 mSampleSize;
static t_uint16 mSampleFreq;

////////////////////////////////////////////////////////////
// Interfaces for fsm
////////////////////////////////////////////////////////////

static void 
ReadData(Buffer_t *Buf) {
    int i, bfi, frameSize;
    int nwordsread;
    unsigned char *arr;
    unsigned short *ptrdata;
			
		frameSize = 2 * mSampleBlockSize * mNbChannel; //in bytes
		arr = malloc_ext(frameSize);
		
    for(i=0; i<frameSize; i++)
    {
        arr[i] = fgetc(mFIN);
        if(feof(mFIN))
        {
        	Buf->flags |= BUFFERFLAG_EOS;
            break;
        }
    }

    if(frameSize != i)
    {
        printf("Last data\n");
        Buf->byteInLastWord = 1;
    }

    Buf->filledLen = i/2;   // in 16 bit word

    ptrdata = (unsigned short *)Buf->data;
    for(i=0; i<frameSize; i+=2) {
        *ptrdata++ = arr[i]<<8 | arr[i+1] ;
    }

    if( (frameSize%2) != 0){
        ptrdata--;
        *ptrdata = *ptrdata & 0xFF00 ;
        Buf->filledLen += 1; //in 16 bit word
    }

    free(arr);

}

static void initWrapperBuffersIn()
{
    int i;
    
    for(i=0; i<mNbWrapperBufIn ; i++)
    {
        mFifoOut[i] = &mInBuffer[i];
        if(mInBuffer[i].data == NULL)
        {    
            mInBuffer[i].data = malloc_ext(mSampleBlockSize*mNbChannel);
            ASSERT(mInBuffer[i].data != 0);
        }
        mInBuffer[i].allocLen = mSampleBlockSize*mNbChannel;
        mInBuffer[i].filledLen = 0;
        mInBuffer[i].byteInLastWord = 0;
        mInBuffer[i].flags = 0;
    }
}

static void initWrapperBuffersOut()
{
    int i;

    for(i=0; i<mNbWrapperBufOut ; i++)
    {
        mFifoIn[i] = &mOutBuffer[i];
        if(mOutBuffer[i].data == NULL)
        {    
            mOutBuffer[i].data = malloc_ext(mBufferSize);
            ASSERT(mOutBuffer[i].data != 0);
        }
        mOutBuffer[i].allocLen = mBufferSize;
        mOutBuffer[i].filledLen = 0;
        mOutBuffer[i].byteInLastWord = 0;
        mOutBuffer[i].flags = 0;
    }
}

static bool
checkWrapperInputBuffer(Buffer_p buf) {
    if (buf->data != mInBuffer[mCurrentwriteBuf].data) { 
        return false;
    }
    if (buf->filledLen != 0) {
        return false;
    }
    return true;
}

static void
fillInputBuffer(Buffer_p buf) {
    ReadData(buf);
	  mNbsample += buf->filledLen;
	  if( mNbsample >= mFileSize)
    {
		    buf->flags |= BUFFERFLAG_EOS;
        mEosReached = 1;
	  } 
}

static bool
checkWrapperOutputBuffer(Buffer_p buf) {
    int i;
    
    if (buf->data != mOutBuffer[mCurrentencodedBuf].data) {
        return false;
    }
    if (buf->allocLen != mOutBuffer[mCurrentencodedBuf].allocLen) {
        return false;
    }

    return true;
}

static void
fillOutputFile(Buffer_p buf) {
    int i;
    unsigned char CurrentChar;

	if(  buf->filledLen > 0)
	{
		fwrite16(buf->data, 1, buf->filledLen-1, mFOUT);

		if( buf->byteInLastWord == 1 ){        // write last 8 bits
			CurrentChar = (buf->data[buf->filledLen-1] >> 8) & 0x00FF;
			fputc(CurrentChar, mFOUT);
		}
		else {
			fwrite16(&buf->data[buf->filledLen-1], 1, 1, mFOUT); // write last 16 bits word
		}
	}

    buf->filledLen = 0;
    if(buf->flags & BUFFERFLAG_EOS){
        proxy.eventHandler(OMX_EventBufferFlag, IN, buf->flags);
        rewind(mFIN);
        fclose(mFOUT);
        mFOUT = 0;
    }
}

static void
tester_reset(void) {
    mNbsample = 0;

    if(mFOUT){
        rewind(mFIN);
        fclose(mFOUT);
        strcat(mOutputFile, "2");
        mFOUT = 0;
    }
    printf("Open new Output File : %s\n", mOutputFile);
    mFOUT = fopen(mOutputFile, "wb");
    ASSERT(mFOUT != NULL);
}

static void
reset(Component * this) {
    tester_reset();
}

static void
disablePortIndication(t_uint32 portIdx) {
}

static void
enablePortIndication(t_uint32 portIdx) {
}

static void
flushPortIndication(t_uint32 portIdx) {
}

static void
process(Component * this) {

    if (Port_queuedBufferCount(&this->ports[0])) {
        Buffer_p buf = Port_dequeueBuffer(&this->ports[0]);
        ASSERT(checkWrapperOutputBuffer(buf));
        fillOutputFile(buf);
        //printf("Tester sends buffer idx=%d to wrapper enc output!\n", mCurrentencodedBuf);
        Port_returnBuffer(&this->ports[0], buf);
        mCurrentencodedBuf ++;
        mCurrentencodedBuf = (mCurrentencodedBuf < mNbWrapperBufOut ) ? mCurrentencodedBuf : 0;
    }

    if (mEosReached) {
        return;
    }

    if (Port_queuedBufferCount(&this->ports[1])) {
        Buffer_p buf =  Port_dequeueBuffer(&this->ports[1]);
        ASSERT(checkWrapperInputBuffer(buf));
        fillInputBuffer(buf);
        //printf("Tester sends buffer idx=%d to wrapper enc input!\n", mCurrentwriteBuf);        
        Port_returnBuffer(&this->ports[1], buf);
        //check newFormat
        outputsettings.newFormat(mSampleFreq, mNbChannel, mSampleSize);
        mCurrentwriteBuf ++;
        mCurrentwriteBuf = (mCurrentwriteBuf < mNbWrapperBufIn ) ? mCurrentwriteBuf : 0;
    }

}


////////////////////////////////////////////////////////////
// Provided Interfaces 
////////////////////////////////////////////////////////////


void METH(start)() {
    int i;

    mCurrentwriteBuf = 0;
    mCurrentencodedBuf = 0;
    mEosReached = 0;
    
    for(i=0; i<MAX_NB_BUFFER ; i++)
    {
        mInBuffer[i].data = NULL;
        mInBuffer[i].allocLen = 0;
        mInBuffer[i].filledLen = 0;
        mInBuffer[i].byteInLastWord = 0;
        mInBuffer[i].flags = 0;
        mOutBuffer[i].data = NULL;
        mOutBuffer[i].allocLen = 0;
        mOutBuffer[i].filledLen = 0;
        mOutBuffer[i].byteInLastWord = 0;
        mOutBuffer[i].flags = 0;
    }
}

void METH(setParameter)(
        t_uint16 fileSize,
        t_uint16 buffer_size,
        char *inputFile,
        char *outputFile,
        t_uint16 mSampleFq,
        t_uint16 mNbChans)
{
    mFileSize = fileSize;
    mBufferSize = buffer_size;
    mNbWrapperBufIn = 1;
    mNbWrapperBufOut = 1;
    
    mFIN = fopen(inputFile, "rb");
    ASSERT(mFIN != NULL);

    mOutputFile = malloc_ext(strlen(outputFile)+1);
    strcpy(mOutputFile, outputFile);

		mSampleBlockSize = codec.getMaxSamples();
		mNbChannel = mNbChans;
		mSampleSize = codec.getSampleBitSize();
		mSampleFreq = mSampleFq;
		
    initWrapperBuffersIn();
    initWrapperBuffersOut();

    //Initialize Tester Output Port (binded with Wrapper Input Buffer)
    Port_init(&mPorts[1], OutputPort, true, false, 0, &mFifoOut, mNbWrapperBufIn, &outputport, 1,0,1, &mTester);

    //Initialize Tester Input Port (binded with Wrapper Output Buffer)
    Port_init(&mPorts[0], InputPort, true, false, 0, &mFifoIn, mNbWrapperBufOut, &inputport, 0,0,1, &mTester);

    mTester.reset   = reset;
    mTester.process = process;
    mTester.disablePortIndication   = disablePortIndication;
    mTester.enablePortIndication    = enablePortIndication;
    mTester.flushPortIndication     = flushPortIndication;
    Component_init(&mTester, 2, mPorts, &proxy);
}

void METH(restartDataFlow)() {
    mEosReached = 0;
    strcat(mOutputFile, "2");
    tester_reset();
    process(&mTester);
}

void METH(processEvent)() {
    Component_processEvent(&mTester);
}

void METH(emptyThisBuffer)(Buffer_p buf) {
    Component_deliverBuffer(&mTester, IN, buf);
}

void METH(fillThisBuffer)(Buffer_p buf) {
    Component_deliverBuffer(&mTester, OUT, buf);
}

void METH(sendCommand)(OMX_COMMANDTYPE cmd, t_uword param) {
    Component_sendCommand(&mTester, cmd, param);
}

void METH(displayMemPreset)(t_uint24 XMemUsed,t_uint24 YMemUsed,t_uint24 DDR24MemUsed,t_uint24 DDR16MemUsed,t_uint24 ESR24MemUsed,t_uint24 ESR16MemUsed)
{
    printf("\nMEM USAGE XMemUsed = %d\nMEM USAGE YMemUsed = %d\nMEM USAGE DDR24MemUsed= %d\nMEM USAGE DDR16MemUsed = %d\nMEM USAGE ESR24MemUsed = %d\nMEM USAGE ESR16MemUsed = %d\n\n",XMemUsed,YMemUsed,DDR24MemUsed,DDR16MemUsed,ESR24MemUsed,ESR16MemUsed);
    
}

