#!/usr/bin/env python3
import argparse
from collections import deque
import glob
import math
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


WIDTH = 620
HEIGHT = 460
PAD_CENTER = (260, 230)
PAD_RADIUS = 150
MAX_TRACE_POINTS = 900
BUTTON_PINS = [19, 21, 22, 23, 25, 26, 32]


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


def parse_line(line):
    parts = line.strip().split(",")
    if len(parts) != 6 or parts[0] != "JOY":
        return None
    try:
        source_h = clamp(float(parts[3]), -1.0, 1.0)
        source_v = clamp(float(parts[4]), -1.0, 1.0)
        display_h = -source_v
        display_v = source_h
        button_mask = int(parts[5])
        active_pins = [pin for i, pin in enumerate(BUTTON_PINS) if button_mask & (1 << i)]
        return {
            "raw_h": int(parts[1]),
            "raw_v": int(parts[2]),
            "source_h": source_h,
            "source_v": source_v,
            "h": display_h,
            "v": display_v,
            "button_mask": button_mask,
            "active_pins": active_pins,
            "button": 1 if active_pins else 0,
        }
    except ValueError:
        return None


def draw_text(screen, font, text, pos, color=(235, 238, 245)):
    surface = font.render(text, True, color)
    screen.blit(surface, pos)


def joystick_to_screen(h, v):
    cx, cy = PAD_CENTER
    return int(cx + h * PAD_RADIUS), int(cy - v * PAD_RADIUS)


def draw_trace(screen, trace, fade_trace):
    if len(trace) < 2:
        return

    now = time.time()
    points = [(x, y) for x, y, _, _ in trace]
    if not fade_trace:
        pygame.draw.lines(screen, (102, 218, 255), False, points, 3)
        return

    for i in range(1, len(trace)):
        x1, y1, _, t1 = trace[i - 1]
        x2, y2, button, t2 = trace[i]
        age = now - max(t1, t2)
        alpha = clamp(1.0 - age / 8.0, 0.12, 1.0)
        base = (255, 106, 122) if button else (102, 218, 255)
        color = tuple(int(channel * alpha) for channel in base)
        width = 4 if button else 3
        pygame.draw.line(screen, color, (x1, y1), (x2, y2), width)


def draw_visualizer(screen, font, small_font, data, last_seen, port_name, trace, trace_enabled, fade_trace):
    screen.fill((20, 22, 27))

    draw_text(screen, font, "Atom Lite Joystick Visualizer", (24, 22), (102, 218, 255))
    draw_text(screen, small_font, f"Port: {port_name}", (24, 52), (170, 178, 190))

    cx, cy = PAD_CENTER
    pygame.draw.circle(screen, (42, 46, 56), PAD_CENTER, PAD_RADIUS)
    pygame.draw.circle(screen, (88, 94, 110), PAD_CENTER, PAD_RADIUS, 2)
    pygame.draw.line(screen, (76, 82, 96), (cx - PAD_RADIUS, cy), (cx + PAD_RADIUS, cy), 1)
    pygame.draw.line(screen, (76, 82, 96), (cx, cy - PAD_RADIUS), (cx, cy + PAD_RADIUS), 1)
    pygame.draw.circle(screen, (62, 68, 82), PAD_CENTER, 8)
    draw_trace(screen, trace, fade_trace)

    if data:
        h = data["h"]
        v = data["v"]
        length = min(1.0, math.hypot(h, v))
        dot_x, dot_y = joystick_to_screen(h, v)
        dot_color = (94, 229, 153) if length < 0.12 else (255, 196, 87)
        if data["button"]:
            dot_color = (255, 106, 122)

        pygame.draw.line(screen, (120, 130, 150), PAD_CENTER, (dot_x, dot_y), 3)
        pygame.draw.circle(screen, dot_color, (dot_x, dot_y), 18)
        pygame.draw.circle(screen, (255, 255, 255), (dot_x, dot_y), 18, 2)

        info_x = 430
        draw_text(screen, font, "Values", (info_x, 126), (235, 238, 245))
        draw_text(screen, small_font, f"X: {h:+.3f}", (info_x, 170))
        draw_text(screen, small_font, f"Y: {v:+.3f}", (info_x, 200))
        draw_text(screen, small_font, f"raw H: {data['raw_h']}", (info_x, 240))
        draw_text(screen, small_font, f"raw V: {data['raw_v']}", (info_x, 270))
        active_text = ",".join(f"G{pin}" for pin in data["active_pins"]) if data["active_pins"] else "none"
        draw_text(screen, small_font, f"active: {active_text}", (info_x, 310))
        draw_text(screen, small_font, f"trace: {'ON' if trace_enabled else 'off'}", (info_x, 350))
        draw_text(screen, small_font, f"points: {len(trace)}", (info_x, 380))

        button_rect = pygame.Rect(426, 82, 168, 42)
        button_color = (255, 106, 122) if data["button"] else (48, 54, 66)
        button_text = active_text if data["button"] else "no button"
        pygame.draw.rect(screen, button_color, button_rect, border_radius=6)
        pygame.draw.rect(screen, (255, 255, 255), button_rect, 2, border_radius=6)
        draw_text(screen, small_font, button_text, (444, 94), (20, 22, 27) if data["button"] else (190, 198, 210))
    else:
        draw_text(screen, font, "Waiting for joystick data...", (120, 220), (255, 196, 87))

    age = time.time() - last_seen if last_seen else 999.0
    status = "LIVE" if age < 0.3 else "NO DATA"
    status_color = (94, 229, 153) if status == "LIVE" else (255, 106, 122)
    draw_text(screen, small_font, status, (24, HEIGHT - 42), status_color)
    draw_text(screen, small_font, "C: clear   T: trace   F: fade   Q: quit", (230, HEIGHT - 42), (170, 178, 190))

    pygame.display.flip()


def main():
    parser = argparse.ArgumentParser(description="Show Atom Lite joystick movement on the Mac screen")
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    if args.port is None:
        args.port = find_default_port()
        if args.port is None:
            print("No USB serial port found. Connect the Atom Lite or pass --port.", file=sys.stderr)
            return 1
        print(f"Using serial port: {args.port}")

    pygame.init()
    pygame.display.set_caption("Atom Lite Joystick Visualizer")
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    font = pygame.font.SysFont("Menlo", 22)
    small_font = pygame.font.SysFont("Menlo", 17)
    clock = pygame.time.Clock()

    data = None
    last_seen = 0.0
    trace = deque(maxlen=MAX_TRACE_POINTS)
    trace_enabled = True
    fade_trace = True
    running = True

    with serial.Serial(args.port, args.baud, timeout=0.02) as port:
        time.sleep(2.0)
        port.reset_input_buffer()

        while running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key in (pygame.K_q, pygame.K_ESCAPE):
                        running = False
                    elif event.key == pygame.K_c:
                        trace.clear()
                    elif event.key == pygame.K_t:
                        trace_enabled = not trace_enabled
                    elif event.key == pygame.K_f:
                        fade_trace = not fade_trace

            for _ in range(20):
                line = port.readline().decode(errors="replace").replace("\x00", "").strip()
                if not line:
                    break
                parsed = parse_line(line)
                if parsed:
                    data = parsed
                    last_seen = time.time()
                    if trace_enabled:
                        x, y = joystick_to_screen(data["h"], data["v"])
                        if not trace or abs(trace[-1][0] - x) > 1 or abs(trace[-1][1] - y) > 1:
                            trace.append((x, y, data["button"], last_seen))

            draw_visualizer(screen, font, small_font, data, last_seen, args.port, trace, trace_enabled, fade_trace)
            clock.tick(60)

    pygame.quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
