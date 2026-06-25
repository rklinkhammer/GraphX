#!/usr/bin/env python3
"""Create, extend, validate, and summarize GraphX FHSS message schedules."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

FREQUENCY_COUNT = 64
FIRST_SELECTABLE = 1
LAST_SELECTABLE = 62
PREAMBLE_PULSE_COUNT = 16
MAX_PULSE_COUNT = 256
PULSE_PERIOD_SAMPLES = 6500


def parse_uint32(raw: str) -> int:
    value = int(raw, 0)
    if not 0 <= value <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"{raw!r} is not a uint32 value")
    return value


def parse_frequency_list(raw: str) -> list[int]:
    try:
        values = [int(item, 0) for item in raw.split(",")]
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    if len(values) != 4 or len(set(values)) != 4:
        raise argparse.ArgumentTypeError(
            "active frequencies must contain exactly four distinct indices"
        )
    if any(not FIRST_SELECTABLE <= value <= LAST_SELECTABLE for value in values):
        raise argparse.ArgumentTypeError("active frequencies must be in [1,62]")
    return values


def parse_word_list(raw: str) -> list[int]:
    try:
        values = [parse_uint32(item) for item in raw.split(",")]
    except argparse.ArgumentTypeError:
        raise
    if len(values) != 4:
        raise argparse.ArgumentTypeError(
            "preamble words must contain exactly four uint32 values"
        )
    return values


def parse_body_pulse(raw: str) -> dict[str, Any]:
    try:
        frequency_raw, value_raw = raw.split(":", 1)
        frequency_index = int(frequency_raw, 0)
        value = parse_uint32(value_raw)
    except (ValueError, argparse.ArgumentTypeError) as exc:
        raise argparse.ArgumentTypeError(
            "body pulse must be FREQUENCY_INDEX:VALUE"
        ) from exc
    return {
        "frequency_index": frequency_index,
        "value": value,
        "role": "body",
    }


def build_message(args: argparse.Namespace) -> dict[str, Any]:
    preamble = []
    for pulse_index in range(PREAMBLE_PULSE_COUNT):
        selection = pulse_index % len(args.active_frequencies)
        preamble.append(
            {
                "frequency_index": args.active_frequencies[selection],
                "value": args.preamble_words[selection],
                "role": "preamble",
            }
        )
    pulses = preamble + list(args.body)
    if len(pulses) > MAX_PULSE_COUNT:
        raise ValueError(f"message has {len(pulses)} pulses; maximum is 256")
    return {
        "message_id": args.message_id,
        "transmit_start_sample": args.transmit_start_sample,
        "pulses": pulses,
    }


def new_schedule(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "description": args.description,
        "active_frequency_indices": args.active_frequencies,
        "iq_center_frequency_hz": args.iq_center_frequency_hz,
        "messages": [build_message(args)],
        "idle_mode": "zero",
        "idle_duration_samples": args.idle_duration_samples,
        "occupied_bandwidth_hz": args.occupied_bandwidth_hz,
        "max_abs_cfo_hz": args.max_abs_cfo_hz,
        "enable_noise": False,
        "enable_doppler": False,
        "enable_multipath": False,
        "allow_overlap": False,
    }


def message_end_sample(message: dict[str, Any]) -> int:
    return int(message["transmit_start_sample"]) + len(message["pulses"]) * PULSE_PERIOD_SAMPLES


def validate_schedule(schedule: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    active = schedule.get("active_frequency_indices")
    if not isinstance(active, list) or len(active) != 4 or len(set(active)) != 4:
        errors.append("active_frequency_indices must contain four distinct indices")
        active = []
    for index in active:
        if not isinstance(index, int) or not FIRST_SELECTABLE <= index <= LAST_SELECTABLE:
            errors.append(f"active frequency index {index!r} is outside [1,62]")

    messages = schedule.get("messages")
    if not isinstance(messages, list):
        return errors + ["messages must be an array"]

    message_ids: set[int] = set()
    ordered_messages = sorted(
        messages, key=lambda item: int(item.get("transmit_start_sample", 0))
    )
    previous_end = 0
    for message_index, message in enumerate(ordered_messages):
        message_id = message.get("message_id")
        if not isinstance(message_id, int) or message_id in message_ids:
            errors.append(f"message {message_index} has an invalid or duplicate message_id")
        else:
            message_ids.add(message_id)
        start = message.get("transmit_start_sample")
        if not isinstance(start, int) or start < 0:
            errors.append(f"message {message_index} has an invalid transmit_start_sample")
            start = 0
        if start < previous_end:
            errors.append(f"message {message_index} overlaps the previous message")

        pulses = message.get("pulses")
        if not isinstance(pulses, list):
            errors.append(f"message {message_index} pulses must be an array")
            continue
        if not PREAMBLE_PULSE_COUNT <= len(pulses) <= MAX_PULSE_COUNT:
            errors.append(f"message {message_index} must contain 16..256 pulses")
        preamble_values: dict[int, int] = {}
        for pulse_index, pulse in enumerate(pulses):
            frequency_index = pulse.get("frequency_index")
            role = pulse.get("role")
            value = pulse.get("value")
            if frequency_index not in active:
                errors.append(
                    f"message {message_index} pulse {pulse_index} frequency is not active"
                )
            if not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
                errors.append(
                    f"message {message_index} pulse {pulse_index} value is not uint32"
                )
            expected_role = "preamble" if pulse_index < PREAMBLE_PULSE_COUNT else "body"
            if role != expected_role:
                errors.append(
                    f"message {message_index} pulse {pulse_index} must be {expected_role}"
                )
            if pulse_index < PREAMBLE_PULSE_COUNT and isinstance(value, int):
                previous = preamble_values.setdefault(frequency_index, value)
                if previous != value:
                    errors.append(
                        f"message {message_index} preamble frequency {frequency_index} "
                        "uses inconsistent word values"
                    )
        previous_end = max(previous_end, message_end_sample(message))
    return errors


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise ValueError("FHSS message schedule root must be an object")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as output:
        json.dump(value, output, indent=2)
        output.write("\n")


def add_message_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--message-id", type=int, required=True)
    parser.add_argument("--transmit-start-sample", type=int, default=0)
    parser.add_argument(
        "--active-frequencies", type=parse_frequency_list, required=True
    )
    parser.add_argument("--preamble-words", type=parse_word_list, required=True)
    parser.add_argument(
        "--body",
        type=parse_body_pulse,
        action="append",
        default=[],
        metavar="INDEX:VALUE",
    )


def command_create(args: argparse.Namespace) -> int:
    schedule = new_schedule(args)
    errors = validate_schedule(schedule)
    if errors:
        raise ValueError("; ".join(errors))
    write_json(args.output, schedule)
    print(f"Wrote FHSS message schedule: {args.output}")
    return 0


def command_add(args: argparse.Namespace) -> int:
    schedule = load_json(args.path)
    if schedule.get("active_frequency_indices") != args.active_frequencies:
        raise ValueError("new message active frequencies must match the schedule")
    schedule.setdefault("messages", []).append(build_message(args))
    errors = validate_schedule(schedule)
    if errors:
        raise ValueError("; ".join(errors))
    write_json(args.path, schedule)
    print(f"Added message {args.message_id} to {args.path}")
    return 0


def command_validate(args: argparse.Namespace) -> int:
    schedule = load_json(args.path)
    errors = validate_schedule(schedule)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print(
        f"Valid FHSS schedule: {len(schedule['messages'])} message(s), "
        f"{sum(len(message['pulses']) for message in schedule['messages'])} pulse(s)"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="create a new message schedule")
    create.add_argument("output", type=Path)
    add_message_arguments(create)
    create.add_argument("--description", default="GraphX FHSS development schedule")
    create.add_argument("--iq-center-frequency-hz", type=float, default=1_240_000_000.0)
    create.add_argument("--idle-duration-samples", type=int, default=0)
    create.add_argument("--occupied-bandwidth-hz", type=float, default=5_000_000.0)
    create.add_argument("--max-abs-cfo-hz", type=float, default=1_000.0)
    create.set_defaults(handler=command_create)

    add = subparsers.add_parser("add-message", help="append a scheduled message")
    add.add_argument("path", type=Path)
    add_message_arguments(add)
    add.set_defaults(handler=command_add)

    validate = subparsers.add_parser("validate", help="validate a message schedule")
    validate.add_argument("path", type=Path)
    validate.set_defaults(handler=command_validate)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return args.handler(args)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.error(str(exc))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
