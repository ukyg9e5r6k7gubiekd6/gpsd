#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""aiogps.py -- Asyncio Python interface to GPSD.

This module adds asyncio functionality to the Python gps interface. It can also
manage connections over unreliable networks through timeouts, keepalive and
automatic reconnection.

Examples:
    // using default parameters
    async with gps.aiogps() as gpsd:
        async for msg in gpsd:
            # Print last message
            print(msg)
            # Print updated GPS info
            print(gpsd)

    // using custom parameters
    try:
        async with gps.aiogps(
                connection_args = {
                    'host': '192.168.10.116',
                    'port': 2947
                },
                connection_timeout = 5,
                reconnect = 0,   # do not try to reconnect, raise exceptions
                alive_opts = {
                    'rx_timeout': 5
                }
            ) as gpsd:
            async for msg in gpsd:
                print(msg)
    except asyncio.CancelledError:
        return
    except asyncio.IncompleteReadError:
        print('Connection closed by server')
    except asyncio.TimeoutError:
        print('Timeout waiting for gpsd to respond')
    except Exception as exc:
        print(f'Error: {exc}')

"""

# This code is compatible with Python 3.x for x >= 6.

__author__ = "Grand Joldes"
__copyright__ = "Copyright 2019, The GPSD Project"
__license__ = "BSD"
__version__ = "0.1"
__email__ = "grandwork2@yahoo.com"

import logging
import asyncio
import socket
from typing import Optional, Union, AnyStr

from .client import gpsjson, dictwrapper
from .gps import gps, gpsdata, WATCH_ENABLE, PACKET_SET
from .misc import polystr, polybytes


class aiogps(gps):  # pylint: disable=R0902
    """An asyncio gps client.

    Reimplements all gps IO methods using asyncio coros.
    Adds connection management, an asyncio context manager and an asyncio iterator.
    """

    def __init__(self, connection_args: Optional[dict] = None,  # pylint: disable=W0231
                 connection_timeout: Optional[float] = None,
                 reconnect: Optional[float] = 2,
                 alive_opts: Optional[dict] = None) -> None:
        """
        Arguments:
            connection_args: dictionary with arguments needed for opening a connection.
                These will be passed directly to asyncio.open_connection. If set to None,
                a connection to the default gps host and port will be attempded.
            connection_timeout: time to wait for a connection to complete (seconds).
                Set to None to disable.
            reconnect: configures automatic reconnections:
                - 0: reconnection is not attempted in case of an error and the error
                is raised to the user;
                - number > 0: delay until next reconnection attempt (seconds).
            alive_opts: dictionary with options related to detection of disconnections.
                Two mecanisms are supported: TCP keepalive (default, may not be available
                on all platforms) and Rx timeout, through the following options:
                - rx_timeout: Rx timeout (seconds). Set to None to disable.
                - SO_KEEPALIVE: socket keepalive and related parameters:
                - TCP_KEEPIDLE
                - TCP_KEEPINTVL
                - TCP_KEEPCNT
        """
        # If connection_args are not specified use defaults
        self.connection_args = connection_args or {
                'host': self.host,
                'port': self.port
            }
        self.connection_timeout = connection_timeout
        assert reconnect >= 0
        self.reconnect = reconnect
        # If alive_opts are not specified use defaults
        self.alive_opts = alive_opts or {
                'rx_timeout': None,
                'SO_KEEPALIVE': 1,
                'TCP_KEEPIDLE': 2,
                'TCP_KEEPINTVL': 2,
                'TCP_KEEPCNT': 3
            }
        # Connection access streams
        self.reader = None
        self.writer = None
        # Init gps parents
        gpsdata.__init__(self) # pylint: disable=W0233
        gpsjson.__init__(self) # pylint: disable=W0233
        # Provide the response in both 'str' and 'bytes' form
        self.bresponse = b''
        self.response = polystr(self.bresponse)
        # Default stream command
        self.stream_command = self.generate_stream_command(WATCH_ENABLE)
        self.loop = self.connection_args.get('loop', asyncio.get_event_loop())

    def __del__(self) -> None:
        """ Destructor """
        self.close()

    async def _open_connection(self) -> None:
        """ Opens a connection to the GPSD server and configures the TCP socket """
        logging.info(f"Connecting to gpsd at {self.connection_args['host']}" +
                     (f":{self.connection_args['port']}" if self.connection_args['port'] else ''))
        conn = asyncio.open_connection(**self.connection_args)
        self.reader, self.writer = await asyncio.wait_for(conn,
                                                          self.connection_timeout,
                                                          loop=self.loop)
        # Set socket options
        sock = self.writer.get_extra_info('socket')
        if sock is not None:
            if 'SO_KEEPALIVE' in self.alive_opts:
                sock.setsockopt(socket.SOL_SOCKET,
                                socket.SO_KEEPALIVE,
                                self.alive_opts['SO_KEEPALIVE'])
            if hasattr(sock, 'TCP_KEEPIDLE') and 'TCP_KEEPIDLE' in self.alive_opts:
                sock.setsockopt(socket.IPPROTO_TCP,
                                socket.TCP_KEEPIDLE,    # pylint: disable=E1101
                                self.alive_opts['TCP_KEEPIDLE'])
            if hasattr(sock, 'TCP_KEEPINTVL') and 'TCP_KEEPINTVL' in self.alive_opts:
                sock.setsockopt(socket.IPPROTO_TCP,
                                socket.TCP_KEEPINTVL,   # pylint: disable=E1101
                                self.alive_opts['TCP_KEEPINTVL'])
            if hasattr(sock, 'TCP_KEEPCNT') and 'TCP_KEEPCNT' in self.alive_opts:
                sock.setsockopt(socket.IPPROTO_TCP,
                                socket.TCP_KEEPCNT,
                                self.alive_opts['TCP_KEEPCNT'])

    def close(self) -> None:
        """ Closes connection to GPSD server """
        if self.writer:
            try:
                self.writer.close()
            except Exception:   # pylint: disable=W0703
                pass
            self.writer = None

    async def read(self) -> Union[dictwrapper, str]:
        """ Reads data from GPSD server """
        while True:
            await self.connect()
            try:
                rx_timeout = self.alive_opts.get('rx_timeout', None)
                reader = self.reader.readuntil(separator=b'\n')
                self.bresponse = await asyncio.wait_for(reader,
                                                        rx_timeout,
                                                        loop=self.loop)
                self.response = polystr(self.bresponse)
                if self.response.startswith("{") and self.response.endswith("}\r\n"):
                    self.unpack(self.response)
                    self._oldstyle_shim()
                    self.valid |= PACKET_SET
                    return self.data
                return self.response
            except asyncio.CancelledError:
                self.close()
                raise
            except Exception as exc:    # pylint: disable=W0703
                error = 'timeout' if isinstance(exc, asyncio.TimeoutError) else exc
                logging.warning(f'Failed to get message from GPSD: {error}')
                self.close()
                if self.reconnect:
                    # Try again later
                    await asyncio.sleep(self.reconnect)
                else:
                    raise

    async def connect(self) -> None:    # pylint: disable=W0221
        """ Connects to GPSD server and starts streaming data """
        while not self.writer:
            try:
                await self._open_connection()
                await self.stream()
                logging.info('Connected to gpsd')
            except asyncio.CancelledError:
                self.close()
                raise
            except Exception as exc:    # pylint: disable=W0703
                error = 'timeout' if isinstance(exc, asyncio.TimeoutError) else exc
                logging.error(f'Failed to connect to GPSD: {error}')
                self.close()
                if self.reconnect:
                    # Try again later
                    await asyncio.sleep(self.reconnect)
                else:
                    raise

    async def send(self, commands: AnyStr) -> None:
        """ Sends commands """
        lineend = "\n"
        if isinstance(commands, bytes):
            lineend = polybytes("\n")
        if not commands.endswith(lineend):
            commands += lineend

        if self.writer:
            self.writer.write(polybytes(commands))
            await self.writer.drain()

    async def stream(self, flags: Optional[int] = 0, devpath: Optional[str] = None) -> None:
        """ Creates and sends the stream command """
        if flags > 0:
            # Update the stream command
            self.stream_command = self.generate_stream_command(flags, devpath)

        if self.stream_command:
            logging.info(f'Send: stream as: {self.stream_command}')
            await self.send(self.stream_command)
        else:
            raise TypeError(f'Invalid streaming command: {flags}')

    async def __aenter__(self) -> 'aiogps':
        """ Context manager entry: open connection """
        await self.connect()
        return self

    async def __aexit__(self, exc_type, exc, traceback) -> None:
        """ Context manager exit: close connection """
        self.close()

    def __aiter__(self) -> 'aiogps':
        """ Async iterator interface """
        return self

    async def __anext__(self) -> None:
        """ Returns next message from GPSD """
        data = await self.read()
        return data
