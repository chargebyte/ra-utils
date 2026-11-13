#!/usr/bin/env python3
# Copyright © 2026 chargebyte GmbH
# SPDX-License-Identifier: Apache-2.0
"""Exercise the real executable against a PTY safety-controller simulator."""
import os
import pty
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import termios
import unittest

EOL, CANDUMP, EOL_TEST_GPIO = sys.argv[1:4]
del sys.argv[1:4]


def crc(data):
    value = 0xff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = ((value << 1) ^ (0x1d if value & 0x80 else 0)) & 0xff
    return value ^ 0xff


def frame(com, data):
    body = bytes([com]) + data
    return b'\xa5' + body + bytes([crc(body), 3])


class Controller:
    def __init__(self, firmware=b'\x00\x05\x01\x81\x04\x00\x00\x00', extras=True, mirror=None, terminal=False, resettable=False, measurements=False):
        self.firmware = firmware
        self.extras = extras
        self.master, self.slave = pty.openpty()
        args = [EOL_TEST_GPIO if resettable else EOL, '-R', '-d', os.ttyname(self.slave)]
        if mirror:
            args += ['-M', mirror]
        self.output = tempfile.TemporaryFile()
        self.errors = tempfile.TemporaryFile()
        self.input_master = self.input_slave = None
        if terminal:
            self.input_master, self.input_slave = pty.openpty()
            self.original_termios = termios.tcgetattr(self.input_slave)
        self.process = subprocess.Popen(args, stdin=self.input_slave if terminal else subprocess.PIPE,
                                        stdout=self.output, stderr=self.errors)
        self.buffer = b''
        self.frames = []
        self.responded = False
        self.measurements = measurements
        self.last_output = 0
        self.next_cycle = time.monotonic()
        self.pp_iec = None


    def pump(self, duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            now = time.monotonic()
            if self.measurements and self.responded and self.last_output and now >= self.next_cycle:
                self.send_measurements()
                self.next_cycle = now + 0.1
            if not select.select([self.master], [], [], min(0.02, max(0, deadline - time.monotonic())))[0]:
                continue
            self.buffer += os.read(self.master, 4096)
            while len(self.buffer) >= 12:
                packet, self.buffer = self.buffer[:12], self.buffer[12:]
                if packet[0] != 0xa5 or packet[11] != 3 or crc(packet[1:10]) != packet[10]:
                    raise AssertionError(f'Invalid UART frame: {packet.hex()}')
                com, data = packet[1], packet[2:10]
                self.frames.append((time.monotonic(), com, data))
                if com == 0:
                    self.last_output = int.from_bytes(data, 'big')
                if com == 0xff:
                    if data[0] == 0x0a and self.firmware is not None:
                        # Junk and fragmentation exercise real stream synchronization.
                        reply = frame(0x0a, self.firmware)
                        os.write(self.master, b'\x00\x7e' + reply[:5])
                        time.sleep(0.005)
                        os.write(self.master, reply[5:])
                        self.responded = True
                    elif self.extras and data[0] in (0x0b, 0x14, 0x15, 0x16):
                        metadata = {0x0b: bytes.fromhex('0102030405060708'),
                                    0x14: b'R7FA2E1A', 0x15: b'90000000', 0x16: b'\x07' + bytes(7)}
                        os.write(self.master, frame(data[0], metadata[data[0]]))
                        # All cyclic EOL RX IDs; also exercise the last field in DigitalInput.
                        if not self.measurements:
                            for rx_com in range(1, 6):
                                os.write(self.master, frame(rx_com, b'\x00\x00\x40\x00\x00\x00\x00\x00'))

    def send_measurements(self):
        output = self.last_output
        # Actual EOL DigitalInput readback uses different PT enable bit positions.
        enables = [(output >> bit) & 1 for bit in (45, 44, 43, 50)]
        digital = sum(enabled << bit for enabled, bit in zip(enables, (46, 45, 44, 50)))
        iec = (output >> 55) & 1 if self.pp_iec is None else self.pp_iec
        digital |= iec << 55
        lead = struct.pack('>4H', *(1260 if enabled else 0 for enabled in enables))
        current12 = 600 if enables[0] or enables[1] else 0
        current34 = 600 if enables[2] or enables[3] else 0
        packets = [(1, digital.to_bytes(8, 'big')), (2, lead),
                   (3, struct.pack('>4H', 3574, 2680, 2660, 0)),
                   (4, struct.pack('>4H', 0, 0, 0, current12)),
                   (5, struct.pack('>4H', current34, 0, 0, 0))]
        os.write(self.master, b''.join(frame(com, data) for com, data in packets))

    def finish(self, sig=signal.SIGTERM):
        if self.process.poll() is None:
            self.process.send_signal(sig)
        self.pump(0.2)
        self.process.communicate(timeout=4)
        self.output.seek(0)
        self.errors.seek(0)
        return self.process.returncode, self.output.read().decode(), self.errors.read().decode()

    def command(self, text):
        if self.input_master is not None:
            os.write(self.input_master, text.encode())
        else:
            self.process.stdin.write(text.encode())
            self.process.stdin.flush()

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.communicate(timeout=4)
        os.close(self.master)
        os.close(self.slave)
        if self.input_master is not None:
            os.close(self.input_master)
            os.close(self.input_slave)
        self.output.close()
        self.errors.close()


class EolIntegration(unittest.TestCase):
    def controller(self, **kwargs):
        controller = Controller(**kwargs)
        self.addCleanup(controller.close)
        return controller

    def test_periodic_output_and_all_inquiries(self):
        controller = self.controller()
        controller.pump(0.9)
        code, out, err = controller.finish()
        self.assertEqual(code, 0, err)
        outputs = [(ts, data) for ts, com, data in controller.frames if com == 0]
        self.assertGreaterEqual(len(outputs), 7)
        self.assertTrue(all(data == b'\x08\x80\x28\x05\x00\x00\x00\x00' for _, data in outputs))
        for (a, _), (b, _) in zip(outputs, outputs[1:]):
            self.assertAlmostEqual(b - a, 0.1, delta=0.04)
        self.assertEqual([data[0] for _, com, data in controller.frames if com == 0xff],
                         [0x0a, 0x14, 0x15, 0x16, 0x0b])
        self.assertIn('DI_PT1_EN=1', out)
        for name in ('COM_DIGITAL_INPUT', 'COM_ANALOG_INPUT_01', 'COM_ANALOG_INPUT_02',
                     'COM_ANALOG_INPUT_03', 'COM_ANALOG_INPUT_04'):
            self.assertIn(name, out)
        for value in ('Firmware Version: 0.5.1', 'MCU Version: 7', 'R7FA2E1A90000000',
                      'Git Hash: 0102030405060708', '== Timestamps ==',
                      'COM_DIGITAL_OUTPUT', '== Available Commands =='):
            self.assertIn(value, out)
        self.assertNotIn('\x1b[', out)
        self.assertNotIn('Warning:', err)
        self.assertNotIn('Error:', err)

    def test_silent_receiver_does_not_stop_output_and_sigint_keeps_5_percent(self):
        controller = self.controller(extras=False)
        controller.pump(1.9)  # Beyond the receiver's 1500-ms timeout.
        code, _, err = controller.finish(signal.SIGINT)
        self.assertEqual(code, 0, err)
        outputs = [(ts, data) for ts, com, data in controller.frames if com == 0]
        self.assertGreaterEqual(len(outputs), 17)
        self.assertTrue(all(data[3] == 5 for _, data in outputs))
        self.assertIn('Warning: No response to COM_CHIPINFO', err)
        self.assertLess(max(b[0] - a[0] for a, b in zip(outputs, outputs[1:])), 0.18)

    def test_rejects_normal_firmware_and_ccy(self):
        for firmware in (b'\x00\x05\x01\x81\x03\x00\x00\x00',
                         b'\x00\x05\x01\x82\x04\x00\x00\x00'):
            with self.subTest(firmware=firmware):
                controller = self.controller(firmware=firmware)
                controller.pump(0.15)
                code, _, err = controller.finish()
                self.assertNotEqual(code, 0)
                self.assertIn('not running Charge SOM EOL', err)
                self.assertFalse(any(com == 0 for _, com, _ in controller.frames))

    def test_missing_firmware_response(self):
        controller = self.controller(firmware=None)
        controller.pump(1.8)
        code, _, err = controller.finish()
        self.assertNotEqual(code, 0)
        self.assertIn('No firmware version', err)
        self.assertFalse(any(com == 0 for _, com, _ in controller.frames))

    def test_signal_during_startup_does_not_set_outputs(self):
        controller = self.controller(firmware=None)
        controller.pump(0.1)
        code, _, err = controller.finish()
        self.assertEqual(code, 0, err)
        self.assertFalse(any(com == 0 for _, com, _ in controller.frames))

    def test_duty_keys_limits_enable_and_manual_send(self):
        controller = self.controller()
        controller.pump(0.6)
        controller.command('s')
        controller.pump(0.05)
        for cmd, expected in [('c', 5), ('r', 5), ('t', 10), ('z', 100),
                              ('+', 100), ('0', 0), ('-', 0), ('5', 5), ('6', 10),
                              ('9', 100), ('0', 0), ('+++++', 5), ('-', 4), ('E', 0), ('e', 4)]:
            # Auto sending is paused, so a manual c is the only output source.
            before = len([1 for _, com, _ in controller.frames if com == 0])
            controller.command(cmd + ('' if cmd == 'c' else 'c'))
            controller.pump(0.04)
            outputs = [data for _, com, data in controller.frames if com == 0]
            self.assertEqual(len(outputs), before + 1, cmd)
            self.assertEqual(outputs[-1][3], expected, cmd)
            self.assertEqual(outputs[-1][0], 0 if cmd == 'E' else 8, cmd)
            self.assertEqual(outputs[-1][1] & ~4, 0x80, cmd)
            self.assertEqual(outputs[-1][2] & ~0x38, 0, cmd)
            self.assertEqual(outputs[-1][4:], bytes(4), cmd)
        controller.command('s')
        controller.pump(0.3)
        before = len([1 for _, com, _ in controller.frames if com == 0])
        controller.command('q')
        controller.pump(0.1)
        code, _, err = controller.finish()
        self.assertEqual(code, 0, err)
        self.assertEqual(len([1 for _, com, _ in controller.frames if com == 0]), before)

    def test_partial_and_corrupt_uart_does_not_block_keyboard_or_timers(self):
        controller = self.controller(extras=False)
        controller.pump(0.6)
        packet = frame(3, bytes.fromhex('0df60df60bd00000'))
        os.write(controller.master, packet[:4])
        before = len(controller.frames)
        controller.command('t')
        controller.pump(0.3)
        outputs = [data for _, com, data in controller.frames[before:] if com == 0]
        self.assertGreaterEqual(len(outputs), 2)
        self.assertEqual(outputs[-1][3], 10)
        self.assertTrue(all(data[3] == 10 for data in outputs[1:]))
        os.write(controller.master, packet[4:])
        bad = bytearray(packet)
        bad[10] ^= 1
        os.write(controller.master, bytes(bad) + b'\xa5' * 1000 + packet)
        controller.pump(0.3)
        code, out, err = controller.finish()
        self.assertEqual(code, 0, err)
        self.assertIn('CP Interpretation: A', out)
        self.assertIn('-12.000 V', out)
        self.assertIn('12.071 V', out)

    def test_terminal_mode_restored_on_quit_and_rejected_firmware(self):
        for firmware in (b'\x00\x05\x01\x81\x04\x00\x00\x00',
                         b'\x00\x05\x01\x81\x03\x00\x00\x00'):
            controller = self.controller(terminal=True, firmware=firmware)
            controller.pump(0.6)
            if controller.process.poll() is None:
                self.assertFalse(termios.tcgetattr(controller.input_slave)[3] & termios.ICANON)
                controller.command('q')
                controller.pump(0.1)
            controller.finish()
            self.assertEqual(termios.tcgetattr(controller.input_slave), controller.original_termios)

    def test_stdin_eof_keeps_periodic_output_running(self):
        controller = self.controller()
        controller.process.stdin.close()
        controller.process.stdin = None
        controller.pump(0.8)
        code, _, err = controller.finish()
        self.assertEqual(code, 0, err)
        self.assertGreaterEqual(sum(com == 0 for _, com, _ in controller.frames), 6)

    def test_ctrl_r_clears_observations_and_reinitializes_with_saved_pwm(self):
        controller = self.controller(resettable=True)
        controller.pump(0.6)
        controller.command('t+++')
        controller.pump(0.2)
        controller.extras = False
        before = len(controller.frames)
        controller.command('\x12')
        controller.pump(0.9)
        code, out, err = controller.finish()
        self.assertEqual(code, 0, err)
        after = controller.frames[before:]
        # One already transmitted periodic frame may still be queued when Ctrl-R is entered.
        restart = next(i for i, (_, com, data) in enumerate(after) if com == 0xff and data[0] == 0x0a)
        after = after[restart:]
        self.assertTrue(any(com == 0 for _, com, _ in after))
        self.assertEqual(next(data for _, com, data in after if com == 0)[2], 0x28)
        self.assertTrue(all(data[3] == 13 for _, com, data in after if com == 0))
        last = out.split('== PWM ==')[-1]
        self.assertIn('MCU Version: not received', last)
        self.assertIn('COM_ANALOG_INPUT_02:\r\n  not received', last)
        self.assertNotIn('COM_ANALOG_INPUT_02              [', last)

    def test_pt_groups_and_external_pp_with_live_cycles(self):
        controller = self.controller(measurements=True)
        controller.pump(1.3)
        outputs = [(ts, data) for ts, com, data in controller.frames if com == 0]
        groups = [((data[1] & 4), (data[2] & 0x38)) for _, data in outputs]
        self.assertIn((0, 0x28), groups)
        self.assertIn((4, 0x10), groups)
        self.assertTrue(all(group in ((0, 0x28), (4, 0x10)) for group in groups))
        # Group selection must be held across confirmation and the settling interval.
        changes = [ts for i, (ts, _) in enumerate(outputs) if i == 0 or groups[i] != groups[i - 1]]
        self.assertTrue(all(b - a >= 0.19 for a, b in zip(changes, changes[1:])))
        controller.command('s')
        controller.pump(0.1)
        count = len([1 for _, com, _ in controller.frames if com == 0])
        group = controller.last_output & ((1 << 45) | (1 << 44) | (1 << 43) | (1 << 50))
        controller.pump(0.3)
        self.assertEqual(sum(com == 0 for _, com, _ in controller.frames), count)
        controller.command('c')
        controller.pump(0.05)
        self.assertEqual(controller.last_output & ((1 << 45) | (1 << 44) | (1 << 43) | (1 << 50)), group)
        controller.command('s')
        controller.pp_iec = 0  # An external source supplies PP with the internal pull-up off.
        controller.pump(0.5)
        code, out, err = controller.finish()
        self.assertEqual(code, 0, err)
        last = out.split('== PWM ==')[-1]
        self.assertEqual(last.count('R=1000 Ω    T=  0.0 °C'), 4)
        self.assertIn('692 Ω   [20 A]', last)
        self.assertNotIn('== CP / PP ==', out)
        self.assertIn('CP Negative Side:', last)
        self.assertIn('CP Positive Side:', last)
        self.assertIn('[settling]', out)

    def test_cli(self):
        for option in ('--help', '--version'):
            self.assertEqual(subprocess.run([EOL, option], capture_output=True).returncode, 0)
        for args in (['--reset-period', '-1'], ['--reset-period', 'abc'], ['unexpected'], ['--unknown']):
            self.assertNotEqual(subprocess.run([EOL] + args, capture_output=True).returncode, 0)

    def test_can_mirror_and_candump(self):
        try:
            interfaces = [name for _, name in socket.if_nameindex() if name.startswith('vcan')]
            if not interfaces:
                self.skipTest('No vcan interface available')
            can = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
            self.addCleanup(can.close)
            can.bind((interfaces[0],))
        except OSError as exc:
            self.skipTest(f'CAN unavailable: {exc}')
        dump_output = tempfile.TemporaryFile()
        self.addCleanup(dump_output.close)
        dump = subprocess.Popen([CANDUMP, interfaces[0]], stdout=dump_output, stderr=subprocess.PIPE)

        def close_dump():
            if dump.poll() is None:
                dump.kill()
            dump.communicate(timeout=3)

        self.addCleanup(close_dump)
        time.sleep(0.1)
        controller = self.controller(mirror=interfaces[0])
        controller.pump(0.7)
        code, _, err = controller.finish()
        self.assertEqual(code, 0, err)
        packets = []
        while select.select([can], [], [], 0)[0]:
            can_id, length, payload = struct.unpack('=IB3x8s', can.recv(16))
            self.assertTrue(can_id & 0x80000000)
            self.assertEqual(length, 8)
            packets.append((can_id & 0x1fffffff, payload))
        self.assertIn((0, b'\x08\x80\x28\x05\x00\x00\x00\x00'), packets)
        self.assertTrue(all((com, data) in packets for _, com, data in controller.frames))
        self.assertTrue(all(any(com == rx for com, _ in packets) for rx in range(1, 6)))
        dump.send_signal(signal.SIGTERM)
        _, err = dump.communicate(timeout=3)
        dump_output.seek(0)
        out = dump_output.read()
        self.assertEqual(dump.returncode, 0, err)
        self.assertIn(b'DO_CP_Dutycycle=5%', out)
        self.assertIn(b'DI_PT1_EN=1', out)


if __name__ == '__main__':
    unittest.main()
