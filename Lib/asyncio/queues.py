__all__ = ('Queue', 'PriorityQueue', 'LifoQueue', 'QueueFull', 'QueueEmpty')

import collections
import heapq
from types import GenericAlias

from . import locks
from . import mixins


class QueueEmpty(Exception):
    """Raised when Queue.get_nowait() is called on an empty Queue."""
    pass

class PendingGetTasks(QueueEmpty):
    """Raised when the Queue.get_nowait() method is called on not empty Queue._getters"""
    pass

class QueueFull(Exception):
    """Raised when the Queue.put_nowait() method is called on a full Queue."""
    pass

class PendingPutTasks(QueueFull):
    """Raised when the Queue.put_nowait() method is called on not empty Queue._putters"""
    pass

class Queue(mixins._LoopBoundMixin):
    """A queue, useful for coordinating producer and consumer coroutines.

    If maxsize is less than or equal to zero, the queue size is infinite. If it
    is an integer greater than 0, then "await put()" will block when the
    queue reaches maxsize, until an item is removed by get().

    Unlike the standard library Queue, you can reliably know this Queue's size
    with qsize(), since your single-threaded asyncio application won't be
    interrupted between calling qsize() and doing an operation on the Queue.
    """
    def __init__(self, maxsize=0):
        self._maxsize = maxsize

        if maxsize > 0:
            self._sem_putters = locks.Semaphore(maxsize)
        self._sem_getters = locks.Semaphore(0)
        self._unfinished_tasks = 0
        self._finished = locks.Event()
        self._finished.set()
        self._init(maxsize)

    @property
    def _putters(self):
        if self.maxsize > 0:
            return self._sem_putters._waiters if self._sem_putters._waiters is not None else []
        return []

    @property
    def _getters(self):
        return self._sem_getters._waiters if self._sem_getters._waiters is not None else []

    # These three are overridable in subclasses.
    def _init(self, maxsize):
        self._queue = collections.deque()

    def _get(self):
        return self._queue.popleft()

    def _put(self, item):
        self._queue.append(item)

    # End of the overridable methods.

    def _wakeup_next(self, waiters):
        # Wake up the next waiter (if any) that isn't cancelled.
        if waiters is self._sem_getters:
            self._sem_getters.release()
            return

        if self._maxsize > 0:
            self._sem_putters.release()

    def __repr__(self):
        return f'<{type(self).__name__} at {id(self):#x} {self._format()}>'

    def __str__(self):
        return f'<{type(self).__name__} {self._format()}>'

    __class_getitem__ = classmethod(GenericAlias)

    def _format(self):
        result = f'maxsize={self._maxsize!r}'
        if getattr(self, '_queue', None):
            result += f' _queue={list(self._queue)!r}'
        if self._getters:
            result += f' _getters[{len(self._getters)}]'
        if self._putters:
            result += f' _putters[{len(self._putters)}]'
        if self._unfinished_tasks:
            result += f' tasks={self._unfinished_tasks}'
        return result

    def qsize(self):
        """Number of items in the queue."""
        return len(self._queue)

    @property
    def maxsize(self):
        """Number of items allowed in the queue."""
        return self._maxsize

    def empty(self):
        """Return True if the queue is empty, False otherwise."""
        return not self._queue

    def full(self):
        """Return True if there are maxsize items in the queue.

        Note: if the Queue was initialized with maxsize=0 (the default),
        then full() is never True.
        """
        if self._maxsize <= 0:
            return False
        else:
            return self.qsize() >= self._maxsize

    async def put(self, item):
        """Put an item into the queue.

        Put an item into the queue. If the queue is full, wait until a free
        slot is available before adding item.
        If queue is not full and there are still items waiting for free slots,
        new item has to wait for its time.
        """
        if self._maxsize > 0:
            await self._sem_putters.acquire()
        return self._put_and_wakeup_next(item)

    def put_nowait(self, item):
        """Put an item into the queue without blocking.

        If no free slot is immediately available, raise QueueFull.
        If tasks still pending for a free slot, raise PendingOrMovingItemsError
        """
        if self.full():
            raise QueueFull
        if self._maxsize > 0 and self._sem_putters.locked():
            raise QueueFull # PendingPutTasks
        self._put_and_wakeup_next(item)

    def _put_and_wakeup_next(self, item):
        """Put an item into the queue.

        Wakes up next tasks waiting for slots.
        """
        self._put(item)
        self._unfinished_tasks += 1
        self._finished.clear()
        self._wakeup_next(self._sem_getters)

    async def get(self):
        """Remove and return an item from the queue.

        If queue is empty, wait until an item is available.
        """
        await self._sem_getters.acquire()
        return self._get_and_wakeup_next()

    def get_nowait(self):
        """Remove and return an item from the queue.

        Return an item if one is immediately available, else raise QueueEmpty
        """
        if self.empty():
            raise QueueEmpty
        if self._sem_getters.locked():
            raise QueueEmpty # PendingGetTasks
        return self._get_and_wakeup_next()

    def _get_and_wakeup_next(self):
        """Remove and return an item from the queue.
        """
        item = self._get()
        if self.maxsize > 0:
            self._wakeup_next(self._sem_putters)
        return item

    def task_done(self):
        """Indicate that a formerly enqueued task is complete.

        Used by queue consumers. For each get() used to fetch a task,
        a subsequent call to task_done() tells the queue that the processing
        on the task is complete.

        If a join() is currently blocking, it will resume when all items have
        been processed (meaning that a task_done() call was received for every
        item that had been put() into the queue).

        Raises ValueError if called more times than there were items placed in
        the queue.
        """
        if self._unfinished_tasks <= 0:
            raise ValueError('task_done() called too many times')
        self._unfinished_tasks -= 1
        if self._unfinished_tasks == 0:
            self._finished.set()

    async def join(self):
        """Block until all items in the queue have been gotten and processed.

        The count of unfinished tasks goes up whenever an item is added to the
        queue. The count goes down whenever a consumer calls task_done() to
        indicate that the item was retrieved and all work on it is complete.
        When the count of unfinished tasks drops to zero, join() unblocks.
        """
        if self._unfinished_tasks > 0:
            await self._finished.wait()


class PriorityQueue(Queue):
    """A subclass of Queue; retrieves entries in priority order (lowest first).

    Entries are typically tuples of the form: (priority number, data).
    """

    def _init(self, maxsize):
        self._queue = []

    def _put(self, item, heappush=heapq.heappush):
        heappush(self._queue, item)

    def _get(self, heappop=heapq.heappop):
        return heappop(self._queue)


class LifoQueue(Queue):
    """A subclass of Queue that retrieves most recently added entries first."""

    def _init(self, maxsize):
        self._queue = []

    def _put(self, item):
        self._queue.append(item)

    def _get(self):
        return self._queue.pop()
