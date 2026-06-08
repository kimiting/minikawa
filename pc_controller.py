#!/usr/bin/env python3
import argparse
import glob
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


WIDTH = 420
HEIGHT = 260


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


def send_line(port, line):
    port.write((line + "\n").encode("ascii"))
    port.flush()


def drive_from_keys(keys, speed, turn):
    forward = keys[pygame.K_w]
    back = keys[pygame.K_s]
    left = keys[pygame.K_a]
    right = keys[pygame.K_d]

    if forward and not back:
        if left and not right:
            return speed // 3, speed
        if right and not left:
            return speed, speed // 3
        return speed, speed

    if back and not forward:
        if left and not right:
            return -speed // 3, -speed
        if right and not left:
            return -speed, -speed // 3
        return -speed, -speed

    if left and not right:
        return -turn, turn
    if right and not left:
        return turn, -turn

    return 0, 0


def draw_status(screen, font, left, right, arm, connected=True):
    screen.fill((18, 18, 22))
    lines = [
        "MiniKawa PC Controller",
        "",
        "W/A/S/D: move    WA: forward-left",
        "Hold J/L: arm -/+  K: arm center",
        "X: stop           Q or ESC: quit",
        "",
        f"L/R: {left:>4} / {right:<4}",
        f"Arm: {arm}",
        f"Serial: {'OK' if connected else 'NG'}",
    ]

    y = 18
    for i, line in enumerate(lines):
        color = (240, 240, 245) if i != 0 else (90, 210, 255)
        text = font.render(line, True, color)
        screen.blit(text, (18, y))
        y += 24
    pygame.display.flip()


def main():
    parser = argparse.ArgumentParser(description="Low-latency PC controller for MiniKawa robot")
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--speed", type=int, default=100)
    parser.add_argument("--turn", type=int, default=90)
    parser.add_argument("--arm", type=int, default=90)
    parser.add_argument("--arm-speed", type=float, default=25.0, help="Arm degrees per second while J/L is held")
    parser.add_argument("--send-interval", type=float, default=0.02)
    args = parser.parse_args()

    if args.port is None:
        args.port = find_default_port()
        if args.port is None:
            print("No USB serial port found. Connect the Atom Lite or pass --port.", file=sys.stderr)
            return 1
        print(f"Using serial port: {args.port}")

    arm = clamp(args.arm, 0, 180)
    left = 0
    right = 0
    last_sent = None
    last_send = 0.0
    last_arm_update = time.time()
    arm_delta_buffer = 0.0
    running = True

    pygame.init()
    pygame.display.set_caption("MiniKawa PC Controller")
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    font = pygame.font.SysFont("Menlo", 18)
    clock = pygame.time.Clock()

    with serial.Serial(args.port, args.baud, timeout=0.01) as port:
        time.sleep(2.0)
        send_line(port, "S")
        draw_status(screen, font, left, right, arm)

        while running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key in (pygame.K_q, pygame.K_ESCAPE):
                        running = False
                    elif event.key == pygame.K_x:
                        send_line(port, "S")
                    elif event.key == pygame.K_k:
                        arm = 90
                        send_line(port, f"A {arm}")
                        arm_delta_buffer = 0.0

            keys = pygame.key.get_pressed()
            left, right = drive_from_keys(keys, args.speed, args.turn)

            now = time.time()
            dt = now - last_arm_update
            last_arm_update = now

            if keys[pygame.K_j] and not keys[pygame.K_l]:
                arm_delta_buffer -= args.arm_speed * dt
            elif keys[pygame.K_l] and not keys[pygame.K_j]:
                arm_delta_buffer += args.arm_speed * dt

            arm_delta = int(arm_delta_buffer)
            if arm_delta != 0:
                send_line(port, f"R {arm_delta}")
                arm = clamp(arm + arm_delta, 0, 180)
                arm_delta_buffer -= arm_delta

            command = f"D {left} {right}"
            if command != last_sent or now - last_send >= args.send_interval:
                send_line(port, command)
                last_sent = command
                last_send = now

            while port.in_waiting:
                port.readline()

            draw_status(screen, font, left, right, int(round(arm)))
            clock.tick(60)

        send_line(port, "S")
        time.sleep(0.05)

    pygame.quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
