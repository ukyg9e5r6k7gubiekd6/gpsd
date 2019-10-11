#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright (c) 2019 Grand Joldes (grandwork2@yahoo.com).
#
# This file is Copyright (c) 2019 by the GPSD project
#
# SPDX-License-Identifier: BSD-2-clause

# This code run compatibly under Python 3.x for x >= 6.

"""
Example of using the asyncio Python interface to GPSD. This example
demonstrates error handling by the application code when aiogps is not
configured to handle automatic re-connection.
"""

import asyncio
import logging
import gps.aiogps


async def get_gps_updates(gpsd: gps.aiogps.aiogps) -> None:
    """ Receives and prints messages from GPSD.

    The GPS status information is updated within aiogps every time a new
    message is received.
    This function also demonstrates what error messages can be expected when
    auto reconnection is not used in aiogps (reconnect = 0).
    """
    while True:
        try:
            async for msg in gpsd:
                # Print received message
                logging.info(f'Received: {msg}')
        except asyncio.CancelledError:
            return
        except asyncio.IncompleteReadError:
            logging.info('Connection closed by server')
        except asyncio.TimeoutError:
            logging.error('Timeout waiting for gpsd to respond')
        except Exception as exc:    # pylint: disable=W0703
            logging.error(f'Error: {exc}')
        # Try again in 1s
        await asyncio.sleep(1)


async def print_gps_info(gpsd: gps.aiogps.aiogps) -> None:
    """ Prints GPS status every 5s """
    while True:
        try:
            await asyncio.sleep(5)
            logging.info(f'\nGPS status:\n{gpsd}')
        except asyncio.CancelledError:
            return
        except Exception as exc:    # pylint: disable=W0703
            logging.error(f'Error: {exc}')


async def main():
    """ Main coroutine - executes 2 asyncio tasks in parralel """
    try:
        # Example of using custom connection configuration
        async with gps.aiogps.aiogps(
            connection_args={
                'host': '127.0.0.1',
                'port': 2947
            },
            connection_timeout=5,
            reconnect=0,  # do not reconnect, raise errors
            alive_opts={
                'rx_timeout': 5
            }
        ) as gpsd:
            # These tasks will be executed in parallel
            await asyncio.gather(
                get_gps_updates(gpsd),
                print_gps_info(gpsd),
                return_exceptions=True
            )
    except asyncio.CancelledError:
        return
    except Exception as exc:    # pylint: disable=W0703
        logging.error(f'Error: {exc}')


def run():
    """
    Main function.

    Because this code only compiles on Python versions >= 3.6,
    it is not run directly, but through the example_aiogps_run.py wrapper,
    which fails gracefully on unsupported Python versions.
    """
    # Set up logging program logging
    logging.basicConfig()
    logging.root.setLevel(logging.INFO)
    # Example of setting up logging level for aiogps - this setting will
    # prevent all aiogps events from being logged
    logging.getLogger('gps.aiogps').setLevel(logging.CRITICAL)
    loop = asyncio.events.new_event_loop()
    try:
        asyncio.events.set_event_loop(loop)
        loop.run_until_complete(main())
    except KeyboardInterrupt:
        print('Got keyboard interrupt.')
    finally:
        print('Exiting.')
        try:
            for task in asyncio.Task.all_tasks():
                task.cancel()
        finally:
            loop.run_until_complete(loop.shutdown_asyncgens())
            asyncio.events.set_event_loop(None)
            loop.close()
