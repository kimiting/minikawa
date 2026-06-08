#!/usr/bin/env python3
import argparse
from collections import deque
import glob
import math
import re
import sys
import time

try:
    import pygame
except ImportError:
    print("pygame is required. Install with: python3 -m pip install pygame", file=sys.stderr)
    raise

try:
    import serial
except ImportError:
    print("pyserial is required. Install with: python3 -m pip install pyserial", file=sys.stderr)
    raise


WIDTH = 680
HEIGHT = 440
PAD_CENTER = (245, 230)
PAD_RADIUS = 150
MAX_TRACE_POINTS = 500
RX_RE = re.compile(r"RX seq=(\d+) L/R=(-?\d+)/(-?\d+) Arm=(\d+) Mode=(\w+)")


def find_default_port():
    candidates = []
    for pattern in (
        "/dev/cu.usbserial-*",
        "/dev/cu.SLAB_USBtoUART*",
        "/dev/cu.usbmodem*",
        "COM*",
    ):
        candidates.extend(glob.glob(pattern))
    return sorted(candidates)[0] if candidates else None


def clamp(value, low, high):
    return max(low, min(high, value))


def parse_rx(line):
    match = RX_RE.search(line)
    if not match:
        return None
    return {
        "seq": int(match.group(1)),
        "left": clamp(int(match.group(2)), -100, 100),
        "right": clamp(int(match.group(3)), -100, 100),
        "arm": clamp(int(match.group(4)), 0, 180),
        "mode": match.group(5),
        "line": line,
    }


def draw_text(screen, font, text, pos, color=(235, 238, 245)):
    surface = font.render(text, True, color)
    screen.blit(surface, pos)


def drive_to_joystick(left, right):
    forward = clamp((left + right) / 200.0, -1.0, 1.0)
    turn = clamp((left - right) / 180.0, -1.0, 1.0)
    length = math.hypot(turn, forward)
    if length > 1.0:
        turn /= length
        forward /= length
    return turn, forward


def joystick_to_screen(x, y):
    cx, cy = PAD_CENTER
    return int(cx + x * PAD_RADIUS), int(cy - y * PAD_RADIUS)


def draw_trace(screen, trace):
    if len(trace) < 2:
        return
    now = time.time()
    for i in range(1, len(trace)):
        x1, y1, t1 = trace[i - 1]
        x2, y2, t2 = trace[i]
        age = now - max(t1, t2)
        alpha = clamp(1.0 - age / 6.0, 0.16, 1.0)
        color = tuple(int(channel * alpha) for channel in (102, 218, 255))
        pygame.draw.line(screen, color, (x1, y1), (x2, y2), 3)


def draw_arm(screen, small_font, arm):
    arm_rect = pygame.Rect(458, 284, 160, 24)
    pygame.draw.rect(screen, (42, 46, 56), arm_rect, border_radius=6)
    pygame.draw.rect(screen, (88, 94, 110), arm_rect, 2, border_radius=6)
    arm_x = arm_rect.x + int(arm_rect.w * arm / 180)
    pygame.draw.circle(screen, (255, 106, 122), (arm_x, arm_rect.y + arm_rect.h // 2), 14)
    draw_text(screen, small_font, f"Arm: {arm:3d} deg", (458, 254))


def draw_monitor(screen, font, small_font, rx, last_seen, port_name, trace):
    screen.fill((20, 22, 27))
    draw_text(screen, font, "MiniKawa Robot Input Monitor", (24, 22), (102, 218, 255))
    draw_text(screen, small_font, f"Port: {port_name}", (24, 54), (170, 178, 190))

    now = time.time()
    age = now - last_seen if last_seen else 999.0
    live = age < 0.5
    status = "RX LIVE" if live else "NO RX"
    status_color = (94, 229, 153) if live else (255, 106, 122)
    draw_text(screen, font, status, (500, 24), status_color)

    cx, cy = PAD_CENTER
    pygame.draw.circle(screen, (42, 46, 56), PAD_CENTER, PAD_RADIUS)
    pygame.draw.circle(screen, (88, 94, 110), PAD_CENTER, PAD_RADIUS, 2)
    pygame.draw.line(screen, (76, 82, 96), (cx - PAD_RADIUS, cy), (cx + PAD_RADIUS, cy), 1)
    pygame.draw.line(screen, (76, 82, 96), (cx, cy - PAD_RADIUS), (cx, cy + PAD_RADIUS), 1)
    pygame.draw.circle(screen, (62, 68, 82), PAD_CENTER, 8)
    draw_text(screen, small_font, "received joystick", (158, 394), (170, 178, 190))
    draw_trace(screen, trace)

    if rx:
        joy_x, joy_y = drive_to_joystick(rx["left"], rx["right"])
        dot_x, dot_y = joystick_to_screen(joy_x, joy_y)
        strength = min(1.0, math.hypot(joy_x, joy_y))
        dot_color = (94, 229, 153) if strength < 0.12 else (255, 196, 87)
        if rx["mode"] == "ARM":
            dot_color = (255, 106, 122)

        pygame.draw.line(screen, (120, 130, 150), PAD_CENTER, (dot_x, dot_y), 3)
        pygame.draw.circle(screen, dot_color, (dot_x, dot_y), 18)
        pygame.draw.circle(screen, (255, 255, 255), (dot_x, dot_y), 18, 2)

        info_x = 458
        draw_text(screen, font, "Received", (info_x, 116), (235, 238, 245))
        draw_text(screen, small_font, f"X: {joy_x:+.3f}", (info_x, 158))
        draw_text(screen, small_font, f"Y: {joy_y:+.3f}", (info_x, 188))
        draw_text(screen, small_font, f"L/R: {rx['left']:+4d} / {rx['right']:+4d}", (info_x, 222))
        draw_arm(screen, small_font, rx["arm"])
        draw_text(screen, small_font, f"seq: {rx['seq']}", (info_x, 334), (190, 198, 210))
        draw_text(screen, small_font, f"mode: {rx['mode']}", (info_x, 364), (190, 198, 210))
        draw_text(screen, small_font, f"age: {age:.2f}s", (info_x, 394), (190, 198, 210))
    else:
        draw_text(screen, font, "Waiting for robot RX logs...", (92, 220), (255, 196, 87))

    draw_text(screen, small_font, "Q / ESC: quit", (500, HEIGHT - 38), (170, 178, 190))
    pygame.display.flip()


def main():
    parser = argparse.ArgumentParser(description="Show the input received by the robot-side Atom Lite")
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    if args.port is None:
        args.port = find_default_port()
        if args.port is None:
            print("No USB serial port found. Connect the robot-side Atom Lite or pass --port.", file=sys.stderr)
            return 1
        print(f"Using serial port: {args.port}")

    pygame.init()
    pygame.display.set_caption("MiniKawa Robot Input Monitor")
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    font = pygame.font.SysFont("Menlo", 24)
    small_font = pygame.font.SysFont("Menlo", 18)
    clock = pygame.time.Clock()

    rx = None
    last_seen = 0.0
    trace = deque(maxlen=MAX_TRACE_POINTS)
    running = True

    with serial.Serial(args.port, args.baud, timeout=0.02) as port:
        time.sleep(1.0)
        port.reset_input_buffer()

        while running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN and event.key in (pygame.K_q, pygame.K_ESCAPE):
                    running = False

            for _ in range(30):
                line = port.readline().decode(errors="replace").replace("\x00", "").strip()
                if not line:
                    break
                parsed = parse_rx(line)
                if parsed:
                    rx = parsed
                    last_seen = time.time()
                    joy_x, joy_y = drive_to_joystick(rx["left"], rx["right"])
                    point = joystick_to_screen(joy_x, joy_y)
                    if not trace or abs(trace[-1][0] - point[0]) > 1 or abs(trace[-1][1] - point[1]) > 1:
                        trace.append((point[0], point[1], last_seen))

            draw_monitor(screen, font, small_font, rx, last_seen, args.port, trace)
            clock.tick(60)

    pygame.quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
