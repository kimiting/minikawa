#!/usr/bin/env python3
import argparse
import re
import statistics
import time

import serial


ANALOG_RE = re.compile(r"G(\d+)=(-?\d+)")
LOW_RE = re.compile(r"G(\d+)")


def parse_snapshot(lines):
    analog = {}
    lows = set()
    for line in lines:
        line = line.replace("\x00", "").strip()
        if line.startswith("A:"):
            analog = {int(pin): int(value) for pin, value in ANALOG_RE.findall(line)}
        elif line.startswith("LOW:"):
            if "none" not in line:
                lows = {int(pin) for pin in LOW_RE.findall(line)}
    return analog, lows


def read_snapshot(port, samples=5):
    analog_samples = []
    low_samples = []

    port.reset_input_buffer()
    port.write(b"SNAP\n")
    port.flush()

    deadline = time.time() + 2.0
    while time.time() < deadline and len(analog_samples) < samples:
        lines = []
        for _ in range(2):
            line = port.readline().decode(errors="replace").strip()
            if line:
                lines.append(line)
        analog, lows = parse_snapshot(lines)
        if analog:
            analog_samples.append(analog)
            low_samples.append(lows)

    if not analog_samples:
        raise RuntimeError("スナップショットを読めませんでした。設定用スケッチが書き込まれているか確認してください。")

    pins = sorted(analog_samples[0])
    averaged = {}
    for pin in pins:
        values = [sample.get(pin, 0) for sample in analog_samples]
        averaged[pin] = int(statistics.mean(values))

    low_counts = {}
    for lows in low_samples:
        for pin in lows:
            low_counts[pin] = low_counts.get(pin, 0) + 1
    stable_lows = {pin for pin, count in low_counts.items() if count >= max(1, len(low_samples) // 2)}

    return averaged, stable_lows


def wait_step(prompt):
    input(f"\n{prompt}\n準備できたら Enter を押してください: ")


def max_changed_pin(reference, sample):
    best_pin = None
    best_delta = -1
    for pin, value in sample.items():
        delta = abs(value - reference.get(pin, value))
        if delta > best_delta:
            best_pin = pin
            best_delta = delta
    return best_pin, best_delta


def axis_value(center, sample, pin):
    return sample[pin] - center[pin]


def main():
    parser = argparse.ArgumentParser(description="Atom Lite controller input setup wizard")
    parser.add_argument("--port", default="/dev/cu.usbserial-8D52FF1D39")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    print("MiniKawa コントローラー入力設定")
    print("先に AtomLite_Controller_Setup.ino をコントローラー側へ書き込んでください。")

    with serial.Serial(args.port, args.baud, timeout=0.25) as port:
        time.sleep(2)

        wait_step("スティックを中央にして、ボタンを何も押さないでください。")
        center, center_low = read_snapshot(port)

        wait_step("スティックを「上」に倒してください。")
        up, _ = read_snapshot(port)

        wait_step("スティックを「右」に倒してください。")
        right, _ = read_snapshot(port)

        wait_step("スティックを「下」に倒してください。")
        down, _ = read_snapshot(port)

        wait_step("スティックを「左」に倒してください。")
        left, _ = read_snapshot(port)

        vertical_pin, vertical_delta = max_changed_pin(center, {
            pin: max(up[pin], down[pin], key=lambda value: abs(value - center[pin]))
            for pin in center
        })
        horizontal_pin, horizontal_delta = max_changed_pin(center, {
            pin: max(right[pin], left[pin], key=lambda value: abs(value - center[pin]))
            for pin in center
        })

        vertical_invert = axis_value(center, up, vertical_pin) < 0
        horizontal_invert = axis_value(center, right, horizontal_pin) < 0

        button_names = ["アームモード", "ボタン2", "ボタン3", "ボタン4"]
        buttons = {}
        for name in button_names:
            wait_step(f"{name} にしたいボタンを押したままにしてください。不要なら何も押さずにEnter。")
            _, lows = read_snapshot(port)
            pressed = sorted(lows - center_low)
            if pressed:
                buttons[name] = pressed[0]

        print("\n--- 判定結果 ---")
        print(f"横軸ピン: GPIO{horizontal_pin}  delta={horizontal_delta}  invert={str(horizontal_invert).lower()}")
        print(f"縦軸ピン: GPIO{vertical_pin}  delta={vertical_delta}  invert={str(vertical_invert).lower()}")
        for name, pin in buttons.items():
            print(f"{name}: GPIO{pin}")

        print("\n--- スケッチに入れる値 ---")
        print(f"constexpr int STICK_H = {horizontal_pin};")
        print(f"constexpr int STICK_V = {vertical_pin};")
        print(f"constexpr bool STICK_H_INVERT = {'true' if horizontal_invert else 'false'};")
        print(f"constexpr bool STICK_V_INVERT = {'true' if vertical_invert else 'false'};")
        if "アームモード" in buttons:
            print(f"constexpr int ARM_MODE_SW = {buttons['アームモード']};")


if __name__ == "__main__":
    main()
