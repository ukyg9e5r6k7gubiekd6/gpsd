#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Simple example of using the asyncio Python interface to GPSD.

"""

# This code is compatible with Python 3.x for x >= 6.

__author__ = "Grand Joldes"
__copyright__ = "Copyright 2019, The GPSD Project"
__license__ = "BSD"
__version__ = "0.1"
__email__ = "grandwork2@yahoo.com"

import asyncio
import logging
import gps

async def main():
    """ Main coroutine. """
    while True:
        try:
            # Example of using custom connection configuration
            async with gps.aiogps(
                    connection_args={
                        'host': '127.0.0.1',
                        'port': 2947
                    },
                    connection_timeout=5,
                    reconnect=0,  # do not try to reconnect, raise exceptions
                    alive_opts={
                        'rx_timeout': 5
                    }
                ) as gpsd:
                async for msg in gpsd:
                    # Print last message
                    print(msg)
                    # Print updated gps info
                    print(gpsd)
        except asyncio.CancelledError:
            return
        except asyncio.IncompleteReadError:
            print('Connection closed by server')
        except asyncio.TimeoutError:
            print('Timeout waiting for gpsd to respond')
        except Exception as exc:    # pylint: disable=W0703
            print(f'Error: {exc}')

if __name__ == "__main__":
    logging.root.setLevel(logging.INFO)
    loop = asyncio.events.new_event_loop()
    main_task = None
    try:
        asyncio.events.set_event_loop(loop)
        main_task = loop.create_task(main())
        loop.run_until_complete(main_task)
    except KeyboardInterrupt:
        print('Got keyboard interrupt.')
    finally:
        print('Exiting.')
        try:
            main_task.cancel()
            loop.run_until_complete(loop.shutdown_asyncgens())
        finally:
            asyncio.events.set_event_loop(None)
            loop.close()
