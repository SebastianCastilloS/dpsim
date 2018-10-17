import asyncio
import struct
import os
import logging
import queue
import socket

from .Event import Event

LOGGER = logging.getLogger('dpsim.EventChannel')

class EventChannelProtocol(asyncio.Protocol):

    def __init__(self, ec):
        self.transport = None
		self.ec = ec

    def connection_made(self, transport):
        self.transport = transport

    def data_received(self, data):
        self.ec.sr.feed_data(data)

        LOGGER.debug("Read from: data=%s", repr(data))

    def connection_lost(self, exc):
		pass

class EventChannel(object):
    def __init__(self, fd, loop = None):
        self._loop = loop
        if self._loop is None:
            self._loop = asyncio.get_event_loop()

        self._sock = socket.fromfd(fd, socket.AF_INET, socket.SOCK_STREAM, 0)
        self.sr = asyncio.StreamReader(loop = loop)

        self._callbacks = []
        self._queue = asyncio.Queue()

        self._last = Event.unknown
        self._lock = asyncio.Lock(loop = loop)
        self._cond = asyncio.Condition(lock = self._lock, loop = loop)

        LOGGER.debug("Created new event queue with fd=%d", fd)

        self._loop.create_task(self.__task())

		self._transport, self._protocol = await loop.create_connection(lambda: EventChannelProtocol(self), sock = self._sock)
		
    def add_callback(self, cb, *args, event = None):
        self._callbacks.append((event, cb, args))

    async def wait(self, evt):
        if evt == None:
            LOGGER.debug("Waiting for any event")
            revt = await self._queue.get()
        else:
            while True:
                LOGGER.debug("Waiting for event: %s. Current event %s" % (evt, self._last))
                revt = await self._queue.get()

                if evt == revt:
                    break

        LOGGER.debug("Received event in wait: event=%s", revt)
        return revt

    async def __task(self):
        LOGGER.debug("Entering events task")

        while True:
            data = await self.sr.readexactly(4)

            unpacked = struct.unpack('i', data)
            evt = Event(unpacked[0])

            LOGGER.debug("Received event: event=%s", evt)

            # Call user defined callbacks
            for e in self._callbacks:
                tevt = e[0]
                cb = e[1]
                args = e[2]

                if tevt == evt or tevt is None:
                    cb(evt, *args)

            await self._queue.put(evt)
