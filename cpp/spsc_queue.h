/*
 * Single Producer Single Consumer Queue implemented over shared-memory.
 * Copyright (C) 2022 Zero ASIC
 */

#ifndef SPSC_QUEUE_H__
#define SPSC_QUEUE_H__

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/mman.h>

#ifdef __cplusplus
#include <atomic>
using namespace std;
#else
#include <stdatomic.h>
#endif

#ifndef MAP_POPULATE
// If the implementation lacks MAP_POPULATE, define it to 0 (no-op).
#define MAP_POPULATE 0
#endif

#define SPSC_QUEUE_PACKET_SIZE 10
#define SPSC_QUEUE_CACHE_LINE_SIZE 64

typedef struct spsc_queue_shared {
    int32_t head __attribute__((__aligned__(SPSC_QUEUE_CACHE_LINE_SIZE)));
    int32_t tail __attribute__((__aligned__(SPSC_QUEUE_CACHE_LINE_SIZE)));
    uint32_t packets[1][SPSC_QUEUE_PACKET_SIZE];
} spsc_queue_shared;

typedef struct spsc_queue {
    int32_t cached_tail __attribute__((__aligned__(SPSC_QUEUE_CACHE_LINE_SIZE)));
    int32_t cached_head __attribute__((__aligned__(SPSC_QUEUE_CACHE_LINE_SIZE)));
    spsc_queue_shared *shm;
    char *name;
    size_t capacity;
} spsc_queue;

static inline size_t spsc_mapsize(size_t capacity) {
    spsc_queue *q = NULL;
    size_t mapsize;

    assert(capacity > 0);

    // Start with the size of the shared area. This includes the
    // control members + one packet.
    mapsize = sizeof(*q->shm);
    // Add additional packets.
    mapsize += sizeof(q->shm->packets[0]) * (capacity - 1);

    return mapsize;
}

static inline spsc_queue* spsc_open(const char* name, size_t capacity) {
    spsc_queue *q = NULL;
    size_t mapsize;
    void *p;
    int fd = -1;
    int r;

    // Compute the size of the SHM mapping.
    mapsize = spsc_mapsize(capacity);

    fd = open(name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        perror(name);
        goto err;
    }

    r = ftruncate(fd, mapsize);
    if (r < 0) {
        perror("ftruncate");
        goto err;
    }

    // Allocate a cache-line aligned spsc-queue.
    r = posix_memalign(&p, SPSC_QUEUE_CACHE_LINE_SIZE,
                       sizeof (spsc_queue));
    if (r) {
        fprintf(stderr, "posix_memalign: %s\n", strerror(r));
        goto err;
    }
    q = (spsc_queue *) p;
    memset(q, 0, sizeof *q);

    // Map a shared file-backed mapping for the SHM area.
    // This will always be page-aligned.
    p = mmap(NULL, mapsize,
             PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
             fd, 0);

    if (p == MAP_FAILED) {
        perror("mmap");
        goto err;
    }

    // We can now close the fd without affecting active mmaps.
    close(fd);

    q->shm = (spsc_queue_shared *) p;
    q->name = strdup(name);
    q->capacity = capacity;
    return q;

err:
    if (fd > 0) {
        close(fd);
    }
    free(q);
    return NULL;
}

static inline void spsc_remove_shmfile(const char *name) {
    remove(name);
}

static inline void spsc_close(spsc_queue *q) {
    size_t mapsize = spsc_mapsize(q->capacity);

    // We've already closed the file-descriptor. We now need to munmap the
    // mmap and remove the the shm files.
    spsc_remove_shmfile(q->name);
    free(q->name);
    munmap(q->shm, mapsize);
    free(q);
}

static inline int spsc_size(spsc_queue *q) {
    int head, tail;
    int size;

    __atomic_load(&q->shm->head, &head, __ATOMIC_ACQUIRE);
    __atomic_load(&q->shm->tail, &tail, __ATOMIC_ACQUIRE);

    size = head - tail;
    if (size < 0) {
        size += q->capacity;
    }
    return size;
}

static inline int spsc_send(spsc_queue *q, void *buf) {
    // get pointer to head
    int head;

    __atomic_load(&q->shm->head, &head, __ATOMIC_RELAXED);

    // compute the head pointer
    int next_head = head + 1;
    if (next_head == q->capacity) {
        next_head = 0;
    }

    // if the queue is full, bail out
    if (next_head == q->cached_tail) {
        __atomic_load(&q->shm->tail, &q->cached_tail, __ATOMIC_ACQUIRE);
        if (next_head == q->cached_tail) {
            return 0;
        }
    }

    // otherwise write in the packet
    memcpy(q->shm->packets[head], buf, sizeof(uint32_t)*SPSC_QUEUE_PACKET_SIZE);

    // and update the head pointer
    __atomic_store(&q->shm->head, &next_head, __ATOMIC_RELEASE);

    return 1;
}

static inline int spsc_recv_base(spsc_queue* q, void *buf, bool pop) {
    // get the read pointer
    int tail;
    __atomic_load(&q->shm->tail, &tail, __ATOMIC_RELAXED);

    // if the queue is empty, bail out
    if (tail == q->cached_head) {
        __atomic_load(&q->shm->head, &q->cached_head, __ATOMIC_ACQUIRE);
        if (tail == q->cached_head) {
            return 0;
        }
    }

    // otherwise read out the packet
    memcpy(buf, q->shm->packets[tail], sizeof(uint32_t)*SPSC_QUEUE_PACKET_SIZE);

    if (pop) {
        // and update the read pointer
        tail++;
        if (tail == q->capacity) {
            tail = 0;
        }
        __atomic_store(&q->shm->tail, &tail, __ATOMIC_RELEASE);
    }

    return 1;
}

static inline int spsc_recv(spsc_queue* q, void *buf) {
    return spsc_recv_base(q, buf, true);
}

static inline int spsc_recv_peek(spsc_queue* q, void *buf) {
    return spsc_recv_base(q, buf, false);
}
#endif // _SPSC_QUEUE
