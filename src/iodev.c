/*
 * Copyright (c) 2003-2018 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MER-
 * CHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPE-
 * CIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTH-
 * ERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "epm_util.h"
#include "epm_pool.h"

#include "epcore.h"
#include "epump_internal.h"
#include "iotimer.h"
#include "iodev.h"
#include "ioevent.h"


iodev_t * iodev_alloc ()
{
    iodev_t * pdev = NULL;

    pdev = (iodev_t *)epm_zalloc(sizeof(*pdev));
    if (pdev) {
        iodev_init(pdev);
    }
    return pdev;
}


int iodev_init (void * vdev)
{
    iodev_t * pdev = (iodev_t *)vdev;

    if (!pdev) return -1;

    InitializeCriticalSection(&pdev->fdCS);
    pdev->fd = INVALID_SOCKET;

    return 0;
}


void iodev_free (void * vpdev)
{
    iodev_t * pdev = (iodev_t *)vpdev;
    epump_t * epump = NULL;

    if (!pdev) return;

    epump = (epump_t *)pdev->epump;

    if (pdev->fd != INVALID_SOCKET) {
        if (pdev->fd <= 0 && pdev->id == 0) {
            /* invoked during unused memory pool recycling */
        } else {
            /* when calling iodev_free, the threads may be exited already.
               epumps object will not be in memory. So detaching from fd-poll is unnecessary */

            if (pdev->fdtype == FDT_CONNECTED || pdev->fdtype == FDT_ACCEPTED) {
            #ifdef UNIX
                shutdown(pdev->fd, SHUT_RDWR);
            #endif
            #ifdef _WIN32
                shutdown(pdev->fd, 0x01);//SD_SEND);
            #endif
            }
            closesocket(pdev->fd);
        }
    }
    pdev->fd = INVALID_SOCKET;
    pdev->rwflag = 0;
    pdev->fdtype = 0x00;
    pdev->iostate = 0x00;
#ifdef HAVE_EPOLL
    pdev->epev = 0;
#endif

    DeleteCriticalSection(&pdev->fdCS);

    epm_free(pdev);
}

int iodev_cmp_iodev (void * a, void * b )
{
    iodev_t * pdev = (iodev_t *)a;
    iodev_t * patt = (iodev_t *)b;

    if (!pdev || !patt) return 1;

    if (pdev->fd > patt->fd) return -1;
    else if (pdev->fd == patt->fd) return 0;
    return 1;
}

int iodev_cmp_id (void * a, void * b)
{
    iodev_t * pdev = (iodev_t *)a;
    ulong     id = *(ulong *)b;

    if (!a || !b) return -1;

    if (pdev->id == id) return 0;
    if (pdev->id > id) return 1;
    return -1;
}


int iodev_cmp_fd (void * a, void * b)
{
    iodev_t * pdev = (iodev_t *)a;
    SOCKET    fd = *(SOCKET *)b;

    if (!a || !b) return -1;

    if (pdev->fd == fd) return 0;
    if (pdev->fd > fd) return -1;
    return 1;
}

ulong iodev_hash_fd_func (void * key)
{
    int fd = *(int *)key;

    return (ulong)fd;
}

ulong iodev_hash_func (void * key)
{
    ulong  id = *(ulong *)key;

    return id;
}




void * iodev_new (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
    static ulong gdevid = 100;

    if (pcore == NULL) return NULL;

    pdev = (iodev_t *)epm_pool_fetch(pcore->device_pool);
    if (pdev == NULL) {
        pdev = iodev_alloc();
        if (pdev == NULL) return NULL;
    }

    EnterCriticalSection(&pcore->devicetableCS);
    if (gdevid < 100) gdevid = 100;
    pdev->id = gdevid++;

    epm_ht_set(pcore->device_table, &pdev->id, pdev);
    LeaveCriticalSection(&pcore->devicetableCS);

    InitializeCriticalSection(&pdev->fdCS);
    pdev->fd = INVALID_SOCKET;
    pdev->fdtype = 0;

    pdev->remote_ip[0] = '\0';
    pdev->local_ip[0] = '\0';
    pdev->remote_port = 0;
    pdev->local_port = 0;

    pdev->rwflag = 0x00;
    pdev->iostate = 0x00;
#ifdef HAVE_EPOLL
    pdev->epev = 0;
#endif

    pdev->epcore = pcore;
    pdev->iot = NULL;
    pdev->epump = NULL;

    return pdev;
}


void iodev_close (void * vdev)
{
    iodev_t  * pdev = (iodev_t *)vdev;
    epump_t  * epump = NULL;
    epcore_t * pcore = NULL;

    if (!pdev) return;

    pcore = (epcore_t *)pdev->epcore;
    if (!pcore) return;

    if (epcore_iodev_del(pcore, pdev->id) == NULL) {
        return;
    }

    epump = (epump_t *)pdev->epump;
    if (epump) 
        epump_iodev_del(epump, pdev->fd);

    EnterCriticalSection(&pdev->fdCS);

    pdev->rwflag = 0x00;
    pdev->para = NULL;
    pdev->callback = NULL;
    pdev->cbpara = NULL;
    pdev->iostate = 0x00;
    pdev->epev = 0;

    if (pdev->iot) {
        iotimer_stop(pdev->iot);
        pdev->iot = NULL;
    }

    if (pdev->fd != INVALID_SOCKET) {
        if (epump && epump->fdpollclear)
            (*epump->fdpollclear)(epump, pdev);

        if (pdev->fdtype == FDT_CONNECTED) {    
            struct linger L;
            L.l_onoff = 1; 
            L.l_linger = 0; 
            setsockopt(pdev->fd, SOL_SOCKET, SO_LINGER, (char *) &L, sizeof(L));
         
        #ifdef UNIX 
            shutdown(pdev->fd, SHUT_RDWR);
        #endif  
        #ifdef _WIN32 
            shutdown(pdev->fd, 0x01);//SD_SEND);
        #endif
        } 

        closesocket(pdev->fd);
        pdev->fd = INVALID_SOCKET;
    }
    LeaveCriticalSection(&pdev->fdCS);

    epm_pool_recycle(pcore->device_pool, pdev);
}


void iodev_linger_close (void * vdev)
{
    iodev_t  * pdev = (iodev_t *)vdev; 
    epcore_t * pcore = NULL;

    if (!pdev) return;

    if (pdev->fdtype != FDT_ACCEPTED) {
        iodev_close(pdev);
		return;
	}

    pcore = (epcore_t *)pdev->epcore;
    if (!pcore) return;

    if (epcore_iodev_find(pcore, pdev->id) == NULL) {
        return;
    }

    EnterCriticalSection(&pdev->fdCS);
    if (pdev->fd != INVALID_SOCKET) {
#ifdef UNIX
        shutdown(pdev->fd, SHUT_WR); //SHUT_RD, SHUT_RDWR
#endif
#ifdef _WIN32
        shutdown(pdev->fd, 0x01);//SD_SEND=0x01, SD_RECEIVE=0x00, SD_BOTH=0x02);
#endif
        pdev->fdtype = FDT_LINGER_CLOSE;
    }

    if (pdev->iot) {
        iotimer_stop(pdev->iot);
        pdev->iot = NULL;
    }
    pdev->iot = iotimer_start(pcore, pdev->epump, 2 *1000, IOTCMD_IDLE, pdev, NULL, NULL);
    LeaveCriticalSection(&pdev->fdCS);
}

void * iodev_new_from_fd (void * vpcore, SOCKET fd, int fdtype, void * para, IOHandler * cb, void * cbpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;

    if (!pcore) return NULL;
    if (fd == INVALID_SOCKET) return NULL;

    pdev = iodev_new(pcore);
    if (!pdev) return NULL;

    pdev->fd = fd;
    pdev->fdtype = fdtype;

    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;

    pdev->iostate = IOS_READWRITE;

    iodev_rwflag_set(pdev, RWF_READ);

    return pdev;
}


int iodev_rwflag_set(void * vpdev, uint8 rwflag)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return -1;
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        return 0;
    }
 
    EnterCriticalSection(&pdev->fdCS);
    pdev->rwflag = rwflag;
    LeaveCriticalSection(&pdev->fdCS);

    return 0;
}


int iodev_add_notify (void * vdev, uint8 rwflag)
{
    iodev_t  * pdev = (iodev_t *)vdev;
    epcore_t * pcore = NULL;
    epump_t  * epump = NULL;
    uint8      tmpflag = 0;
    int        setpoll = 0;

    if (!pdev) return -1;
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        return 0;
    }
 
    if (rwflag == 0) return 0;

    pcore = (epcore_t *)pdev->epcore;
    if (!pcore) return -2;
 
    EnterCriticalSection(&pdev->fdCS);
    tmpflag = pdev->rwflag | rwflag;
    if (pdev->rwflag != tmpflag) {
        pdev->rwflag = tmpflag;
        setpoll = 1;
    }
    LeaveCriticalSection(&pdev->fdCS);

    if (pdev->bindtype == 3) {
        epcore_thread_fdpoll(pcore, pdev);
    } else {
        epump = (epump_t *)pdev->epump;
        if (epump) (*epump->fdpoll)(epump, pdev);
    }
 
    return 0;
}

int iodev_del_notify (void * vdev, uint8 rwflag)
{
    iodev_t  * pdev = (iodev_t *)vdev;
    epcore_t * pcore = NULL;
    epump_t  * epump = NULL;
    uint8      tmpflag = 0;
    int        setpoll = 0;
 
    if (!pdev) return -1;
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        return 0;
    }
 
    if (rwflag == 0) return 0;
 
    pcore = (epcore_t *)pdev->epcore;
    if (!pcore) return -2;
 
    EnterCriticalSection(&pdev->fdCS);
    tmpflag = pdev->rwflag & ~rwflag;
    if (pdev->rwflag != tmpflag) {
        pdev->rwflag = tmpflag;
        setpoll = 1;
    }
    LeaveCriticalSection(&pdev->fdCS);

    if (!setpoll) return 0;

    if (pdev->bindtype == 3) {
        epcore_thread_fdpoll(pcore, pdev);
    } else {
        epump = (epump_t *)pdev->epump;
        if (epump) (*epump->fdpoll)(epump, pdev);
    }
 
    return 0;
}


int iodev_bind_epump (void * vdev, int bindtype, void * vepump)
{
    iodev_t  * pdev = (iodev_t *)vdev;
    epump_t  * epump = (epump_t *)vepump;
    epcore_t * pcore = NULL;

    if (!pdev) return -1;

    pcore = (epcore_t *)pdev->epcore;
    if (!pcore) return -2;

    if (bindtype == 1) { //epump will be system-decided
        epump = epcore_thread_select(pcore);
        if (!epump) return -100;

        pdev->bindtype = 1;
        pdev->epump = epump;

        epump_iodev_add(epump, pdev);
        (*epump->fdpoll)(epump, pdev);

    } else if (bindtype == 2 && epump) { //epump is the para-given
        pdev->bindtype = 2;
        pdev->epump = epump;

        epump_iodev_add(epump, pdev);
        (*epump->fdpoll)(epump, pdev);

    } else if (bindtype == 3) { //all epumps need to be bound
        pdev->bindtype = 3;
        pdev->epump = NULL;

        /* add to global list for the loading in future-starting threads */
        epcore_global_iodev_add(pcore, pdev);

        /* add to the global iodev-lists of the threads that have been started, and poll fd */
        epcore_thread_fdpoll(pcore, pdev);

    } else {
        if (epump) {
            pdev->bindtype = 2;
            pdev->epump = epump;

            epump_iodev_add(epump, pdev);
            (*epump->fdpoll)(epump, pdev);
        }
    }

    return 0;
}


void * iodev_para (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return NULL;

    return pdev->para;
}

void * iodev_epcore (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return NULL;

    return pdev->epcore;
}

void * iodev_epump (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return NULL;

    return pdev->epump;
}

SOCKET iodev_fd (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return INVALID_SOCKET;

    return pdev->fd;
}

int iodev_fdtype (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return 0;

    return pdev->fdtype;
}

int iodev_rwflag (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return 0;

    return pdev->rwflag;
}

char * iodev_rip (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return "0.0.0.0";

    return pdev->remote_ip;
}

int iodev_rport (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return 0;

    return pdev->remote_port;
}

char * iodev_lip (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return "0.0.0.0";

    return pdev->local_ip;
}

int iodev_lport (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return 0;

    return pdev->local_port;
}


int iodev_print (void * vpcore)
{
    epcore_t   * pcore = (epcore_t *) vpcore;
    iodev_t    * pdev = NULL;
    int          i, num;
    char         buf[256];

    if (!pcore) return -1;

#ifdef _DEBUG
    printf("\n-------------------------------------------------------------\n");
#endif

    EnterCriticalSection(&pcore->devicetableCS);
    num = epm_ht_num(pcore->device_table);
    for (i=0; i<num; i++) {
        pdev = epm_ht_value(pcore->device_table, i);
        if (!pdev) continue;

        buf[0] = '\0';

        sprintf(buf+strlen(buf), "%5lu %5d ", pdev->id, pdev->fd);

        switch (pdev->fdtype) {
        case FDT_LISTEN: sprintf(buf+strlen(buf), "TCP LISTEN"); break;
        case FDT_CONNECTED: sprintf(buf+strlen(buf), "TCP CONNECTED"); break;
        case FDT_ACCEPTED: sprintf(buf+strlen(buf), "TCP ACCEPTED"); break;
        case FDT_UDPSRV: sprintf(buf+strlen(buf), "UDP LISTEN"); break;
        case FDT_UDPCLI: sprintf(buf+strlen(buf), "UDP CLIENT"); break;
        case FDT_RAWSOCK: sprintf(buf+strlen(buf), "RAW SOCKET"); break;
        case FDT_TIMER: sprintf(buf+strlen(buf), "TIMER"); break;
        case FDT_USERCMD: sprintf(buf+strlen(buf), "USER CMD"); break;
        case FDT_LINGER_CLOSE: sprintf(buf+strlen(buf), "TCP LINGER"); break;
        case FDT_STDIN: sprintf(buf+strlen(buf), "STDIN"); break;
        case FDT_STDOUT: sprintf(buf+strlen(buf), "STDOUT"); break;
        case FDT_USOCK_LISTEN: sprintf(buf+strlen(buf), "USOCK LISTEN"); break;
        case FDT_USOCK_CONNECTED: sprintf(buf+strlen(buf), "USOCK CONNECTED"); break;
        case FDT_USOCK_ACCEPTED: sprintf(buf+strlen(buf), "USOCK ACCEPTED"); break;
        default: sprintf(buf+strlen(buf), "Unknown"); break;
        }

        sprintf(buf+strlen(buf), " %s:%d", pdev->local_ip, pdev->local_port);
        sprintf(buf, " %s:%d", pdev->remote_ip, pdev->remote_port);
        printf("%s\n", buf);
    }
    LeaveCriticalSection(&pcore->devicetableCS);
#ifdef _DEBUG
    printf("-------------------------------------------------------------\n\n");
#endif

    return 0;
}

