#!/usr/bin/env python3
"""中文：用串口替身验证采集器断线停止及显式重连，不访问设备。
English: Verify stop-on-disconnect and explicit reconnection using a serial double, without device access.
"""
import contextlib
import io
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

import capture_reader_perf


class SerialError(Exception):
    pass


class Clock:
    def __init__(self):
        self.now = 0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class Port:
    def __init__(self, clock, outcomes):
        self.clock, self.outcomes = clock, iter(outcomes)
        self.dtr = self.rts = None
        self.port = None
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.closed = True

    def open(self):
        assert self.port == "test-console"
        assert self.dtr is False and self.rts is False

    def read(self, size):
        assert size == 8192
        self.clock.now += .25
        value = next(self.outcomes, b"")
        if isinstance(value, BaseException):
            raise value
        return value


class CaptureTest(unittest.TestCase):
    def run_capture(self, batches, *, reconnect=False):
        clock = Clock()
        ports = []

        def factory(**kwargs):
            self.assertEqual(kwargs, dict(port=None, baudrate=115200, timeout=.25, exclusive=True))
            port = Port(clock, batches[min(len(ports), len(batches) - 1)])
            ports.append(port)
            return port

        serial = types.SimpleNamespace(Serial=factory, SerialException=SerialError)
        with tempfile.TemporaryDirectory(prefix="capture-test-") as directory:
            output = Path(directory) / "raw.log"
            args = ["capture_reader_perf.py", "--port", "test-console", "--seconds", "2", "--output", str(output)]
            if reconnect:
                args.append("--reconnect")
            with patch.dict("sys.modules", {"serial": serial}), patch("sys.argv", args), \
                    patch.object(capture_reader_perf.time, "monotonic", clock.monotonic), \
                    patch.object(capture_reader_perf.time, "sleep", clock.sleep), \
                    contextlib.redirect_stdout(io.StringIO()) as log:
                result = capture_reader_perf.main()
            self.assertTrue(all(port.closed for port in ports))
            return result, ports, output.read_bytes(), log.getvalue()

    def test_disconnect_stops_without_reopening(self):
        for error in (SerialError("device removed"), OSError("device unavailable")):
            with self.subTest(error=type(error).__name__):
                result, ports, data, log = self.run_capture([[b"READER_PERF first\n", error], [b"unexpected reopen\n"]])
                self.assertEqual(len(ports), 1)
                self.assertEqual(result, 1)
                self.assertEqual(data, b"READER_PERF first\n")
                self.assertIn("automatic reconnect disabled", log)

    def test_reconnect_requires_explicit_flag(self):
        result, ports, data, log = self.run_capture([[b"before\n", SerialError("device removed")], [b"after\n"]], reconnect=True)
        self.assertEqual(result, 0)
        self.assertEqual(len(ports), 2)
        self.assertEqual(data, b"before\nafter\n")
        self.assertIn("Waiting for", log)

    def test_timeout_closes_and_preserves_raw_bytes(self):
        result, ports, data, log = self.run_capture([[b"READER_PERF one\n\x00\xff"]])
        self.assertEqual(result, 0)
        self.assertEqual(len(ports), 1)
        self.assertEqual(data, b"READER_PERF one\n\x00\xff")
        self.assertIn("1 records", log)

    def test_interrupt_closes_without_reopening(self):
        result, ports, data, _ = self.run_capture([[b"saved\n", KeyboardInterrupt()]])
        self.assertEqual(result, 0)
        self.assertEqual(len(ports), 1)
        self.assertEqual(data, b"saved\n")


if __name__ == "__main__":
    unittest.main()
