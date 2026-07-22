#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>
#include "mpu.h"
#include "gpu.h"
#include "dma.h"
#include "gpuprimitives.h"
#include "gputextures.h"
#include "linkedlists.h"

static pthread_t GPUthread;
// GPUlock guards both QueueList's pointers and the queueActive/predicate check,
// and is the mutex paired with GPUcond -- a single lock so the "is there work,
// or are we stopping" check and the cond_wait are atomic with respect to
// QueueGPUrequest()'s append+signal, which avoids the classic lost-wakeup race
// that existed here before (a separate condLock meant a signal could arrive
// between the empty-check and the wait, with no one blocked to receive it).
static pthread_mutex_t GPUlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t GPUcond = PTHREAD_COND_INITIALIZER;

// Bounds queue growth if the GPU thread ever falls behind the producer (a
// pixel-heavy program can otherwise queue requests faster than they're
// drawn, growing without limit). QueueGPUrequest() blocks the CPU thread
// once the queue reaches this depth, and ProcessGPUqueue() wakes it via
// GPUnotFullCond after every dequeue.
#define GPU_QUEUE_MAX_DEPTH 65536
static pthread_cond_t GPUnotFullCond = PTHREAD_COND_INITIALIZER;

struct _queueEntry
{
    unsigned int id;
    struct _queueEntry *nextEntry;
    unsigned char cmd;
    unsigned short i1, i2, i3, i4, i5, i6, i7;
    void *p1, *p2, *p3, *p4, *p5, *p6, *p7;
};

typedef struct _queueEntry QueueRequest;

static LinkedList QueueList = { NULL, NULL, 0 };

static short int queueActive = 1;
static short int queueProcessing = 1;

#ifdef GPU_MODE_QUEUE

static void GPUsigHandler(int signo)
{
    // write(0, "!", 1);
}

void *ProcessGPUqueue(void *ptr)
{
    int sigdummy;
    sigset_t sigmask;
    QueueRequest gpuqueue;

    sigemptyset(&sigmask);               /* to zero out all bits */
    sigaddset(&sigmask, SIGUSR1);        /* to unblock SIGUSR1 */  
    pthread_sigmask(SIG_UNBLOCK, &sigmask, NULL);

    // fprintf(stderr, "GPU queue alive\n");

    for (;;)
    {
        // Pop the head under GPUlock: the predicate check ("is there work, or
        // are we stopping") and the cond_wait share the same mutex, so a
        // request appended (and signaled) by QueueGPUrequest() between our
        // check and the wait can't be missed -- either we see it in the
        // itemCnt check, or we're already asleep in pthread_cond_wait and the
        // signal wakes us. The `while` (not `if`) also absorbs spurious
        // wakeups. Once popped, the request is unlinked from the shared list,
        // so it's safe to read/dispatch/free it without holding the lock.
        pthread_mutex_lock(&GPUlock);
        while (queueActive && QueueList.itemCnt == 0)
            pthread_cond_wait(&GPUcond, &GPUlock);

        if (!queueActive && QueueList.itemCnt == 0)
        {
            pthread_mutex_unlock(&GPUlock);
            break;
        }
        QueueRequest *request = (QueueRequest*)RemoveListHead(&QueueList);
        pthread_mutex_unlock(&GPUlock);

        // Wake a producer that's blocked in QueueGPUrequest() waiting for
        // room (see GPU_QUEUE_MAX_DEPTH).
        pthread_cond_signal(&GPUnotFullCond);

        if (request == NULL) continue;

        switch (request->cmd)
        {
            case CMD_DestroyScreen:
                DestroyScreen(request->i1);
            break;

            case CMD_SetColor:
                SetColor(request->i1, request->i2);
            break;

            case CMD_SetPixel:
                SetPixel(request->i1, request->i2, request->i3);
            break;

            case CMD_DrawLine:
                DrawLine(request->i1, request->i2, request->i3, request->i4, request->i5);
            break;

            case CMD_DestroyTexture:
                DestroyTexture(request->i1);
            break;

            case CMD_SetTextureTransparency:
                SetTextureTransparency(request->i1, request->i2, request->i3);
            break;

            case CMD_LoadTexture:
                LoadTexture(request->i1, request->i2, request->i3);
            break;

            case CMD_RenderTexture:
            {
                // Resolve screenid/textureid to Screen*/Texture* here, on the
                // GPU thread, immediately before use -- see the comment in
                // gputextures.c's RenderTexture() for why this can't be
                // resolved earlier on the CPU thread.
                Screen *screen = GetScreen(request->i1);
                Texture *texture = GetTexture(request->i2);

                if (screen != NULL && texture != NULL)
                    QRenderTexture(screen, texture, request->i3, request->i4, request->p3);
                else if (request->p3 != NULL)
                    free(request->p3);
            }
            break;

            default:
                fprintf(stderr, "GPU queue process : unhandled command %d\n", request->cmd);
            break;
        }
        free(request);
    }

    return NULL;
}

// cmd is declared int (not unsigned char) because va_start's named parameter
// must not be one that undergoes default argument promotion -- a narrower
// type there is undefined behavior per C11 7.16.1.4, regardless of the actual
// values passed. Callers passing a narrower type (e.g. mpu.c's unsigned char
// cmd) still convert safely through the ordinary prototype-based conversion.
void QueueGPUrequest(unsigned int cmd, ...)
{
    va_list       ArgumentPointer;
    QueueRequest *newGPUrequest = malloc(sizeof(QueueRequest));

    newGPUrequest->cmd = cmd;

    switch (cmd)
    {
        case CMD_DestroyScreen: // 1 short int - Screen ID
            va_start(ArgumentPointer, cmd);
            newGPUrequest->i1 = va_arg(ArgumentPointer, int);
            va_end(ArgumentPointer);
        break;

        case CMD_SetColor: // 2 short ints - Screen ID, Color
            va_start(ArgumentPointer, cmd);
            newGPUrequest->i1 = va_arg(ArgumentPointer, int);
            newGPUrequest->i2 = va_arg(ArgumentPointer, int);
            va_end(ArgumentPointer);
        break;

        case CMD_SetPixel: // 3 short ints - Screen ID, x, y
            va_start(ArgumentPointer, cmd);
            newGPUrequest->i1 = va_arg(ArgumentPointer, int);
            newGPUrequest->i2 = va_arg(ArgumentPointer, int);
            newGPUrequest->i3 = va_arg(ArgumentPointer, int);
            va_end(ArgumentPointer);
        break;

        case CMD_DrawLine: // 5 short ints - Screen ID, x1, y1, x2, y2
            va_start(ArgumentPointer, cmd);
            newGPUrequest->i1 = va_arg(ArgumentPointer, int);
            newGPUrequest->i2 = va_arg(ArgumentPointer, int);
            newGPUrequest->i3 = va_arg(ArgumentPointer, int);
            newGPUrequest->i4 = va_arg(ArgumentPointer, int);
            newGPUrequest->i5 = va_arg(ArgumentPointer, int);
            va_end(ArgumentPointer);
        break;

        case CMD_DestroyTexture: // 1 short int1 - Texture ID
            va_start(ArgumentPointer, cmd);
            newGPUrequest->i1 = va_arg(ArgumentPointer, int);
            va_end(ArgumentPointer);
        break;

        case CMD_SetTextureTransparency: // 3 short ints - Texture ID, onoff, color
            va_start(ArgumentPointer, cmd);
            newGPUrequest->i1 = va_arg(ArgumentPointer, int);
            newGPUrequest->i2 = va_arg(ArgumentPointer, int);
            newGPUrequest->i3 = va_arg(ArgumentPointer, int);
            va_end(ArgumentPointer);
        break;

        case CMD_LoadTexture: // 3 short ints - Screen ID, Texture ID, memaddr
            va_start(ArgumentPointer, cmd);
            newGPUrequest->i1 = va_arg(ArgumentPointer, int);
            newGPUrequest->i2 = va_arg(ArgumentPointer, int);
            newGPUrequest->i3 = va_arg(ArgumentPointer, int);
            va_end(ArgumentPointer);
        break;

        case CMD_RenderTexture: // screenid, textureid, screenx, screeny, Rect*
            // Ids (not resolved Screen*/Texture* pointers) are queued
            // deliberately -- see the comment in gputextures.c's
            // RenderTexture(). The lookup happens in ProcessGPUqueue, right
            // before use, so it can't race a queued destroy for the same id.
            va_start(ArgumentPointer, cmd);
            newGPUrequest->i1 = va_arg(ArgumentPointer, int);
            newGPUrequest->i2 = va_arg(ArgumentPointer, int);
            newGPUrequest->i3 = va_arg(ArgumentPointer, int);
            newGPUrequest->i4 = va_arg(ArgumentPointer, int);
            newGPUrequest->p3 = va_arg(ArgumentPointer, void*);
            va_end(ArgumentPointer);
        break;

        default:
            // No va_start was called for this cmd, so there's nothing valid to
            // queue -- free and bail out instead of leaking newGPUrequest and
            // appending garbage-filled i1..p7 fields to the queue.
            fprintf(stderr, "GPU queue add : unhandled cmd %d\n", cmd);
            free(newGPUrequest);
            return;
    }
    newGPUrequest->nextEntry = NULL;

    pthread_mutex_lock(&GPUlock);
    // Backpressure: block the CPU thread here instead of growing the queue
    // without limit if the GPU thread is behind (or stalled).
    while (QueueList.itemCnt >= GPU_QUEUE_MAX_DEPTH)
        pthread_cond_wait(&GPUnotFullCond, &GPUlock);
    AppendListItem(&QueueList, (LinkedListItem*)newGPUrequest);
    pthread_mutex_unlock(&GPUlock);

    // Only wake the GPU thread if it is asleep
    pthread_cond_signal(&GPUcond);
}

void StartGPUQueue()
{
    struct sigaction action;

    /* set up signal handlers for SIGINT & SIGUSR1 */
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = GPUsigHandler;
    sigaction(SIGUSR1, &action, NULL);

    // queueActive is reset here (not just at static-init time) because a pak
    // eject/reinsert cycle calls StopGPUqueue() then StartGPUQueue() again in
    // the same process; without this, the second thread would see
    // queueActive already 0 from the previous stop and exit immediately.
    queueActive = 1;

    int rc = pthread_create(&GPUthread, NULL, ProcessGPUqueue, NULL);

    if (rc != 0)
    {
        fprintf(stderr, "Cannot start GPU thread\n");
    }
}

void StopGPUqueue()
{
    pthread_mutex_lock(&GPUlock);
    queueActive = 0;
    pthread_mutex_unlock(&GPUlock);

    pthread_cond_signal(&GPUcond);

    // Wait for the thread to actually exit before returning. The caller
    // (mpu.c's ModuleConfig(0)) runs right before the pak is dlclose()'d
    // (see mpi.c's UnloadModule); without this join, the GPU thread could
    // still be executing code from this .so's text segment at the moment
    // it's unmapped, which crashes.
    pthread_join(GPUthread, NULL);
}
#endif

void ReportQueue()
{
    fprintf(stderr, "GPU Queue depth %d\n", QueueList.itemCnt);
}

void GetQueueLen(ushort lenref)
{
    ushort len;

    len = QueueList.itemCnt;

    WriteCoCoInt(lenref, len);
}
