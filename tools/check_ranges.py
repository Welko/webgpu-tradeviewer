#!/usr/bin/env python3
"""Checks the trade count and total amount the viewer shows for three fixed
time ranges against an independent calculation, as Task 1 asks.

The viewer's numbers come from TradeStatsApp, which runs the same C++ code the
viewer displays from. This script works them out on its own: it reads the CSV
with Python's csv module and adds amounts as exact decimals. They share
nothing but the file.

Usage: tools/check_ranges.py <TradeStatsApp> <trades.csv[.gz]>
Prints a Markdown table, and exits with 1 if any range disagrees.
"""

import csv
import gzip
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from decimal import Decimal

# Half-open: from the start, up to but not including the end (UTC).
RANGES = [
    ("Initial view", "2019-12-01T12:00:00", "2019-12-01T13:00:00"),
    ("Whole day", "2019-12-01T00:00:00", "2019-12-02T00:00:00"),
    # 2 trades share the start time and count; 6 share the end time and don't.
    ("Edges on trades", "2019-12-01T12:30:00.224", "2019-12-01T12:30:51.531"),
]

EPOCH = datetime(1970, 1, 1, tzinfo=timezone.utc)


def to_us(text):
    return (datetime.fromisoformat(text).replace(tzinfo=timezone.utc) - EPOCH) // timedelta(microseconds=1)


def read_trades(path):
    """(time in µs, amount) for each trade, from a Tardis file (with a header) or Binance's (without)."""
    with (gzip.open(path, "rt", newline="") if path.endswith(".gz") else open(path, newline="")) as file:
        rows = csv.reader(file)
        first = next(rows)
        if first[0][:1].isdigit():
            # Binance: id, price, qty, quoteQty, time, isBuyerMaker, isBestMatch; time in ms until 2025.
            time_column, amount_column = 4, 2
            rows_to_read = [first]
        else:
            time_column, amount_column = first.index("timestamp"), first.index("amount")
            rows_to_read = []
        for row in [*rows_to_read, *rows]:
            if not row:
                continue
            time = int(row[time_column])
            yield (time * 1000 if time < 10**14 else time), Decimal(row[amount_column])


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    stats, path = sys.argv[1:]
    trades = list(read_trades(path))

    print("| Range (UTC, half-open) | Independent: trades | Independent: amount (BTC) | Viewer's code | Result |")
    print("|---|--:|--:|---|---|")
    failed = False
    for name, start, end in RANGES:
        start_us, end_us = to_us(start), to_us(end)
        count = sum(1 for time, _ in trades if start_us <= time < end_us)
        amount = sum((amount for time, amount in trades if start_us <= time < end_us), Decimal(0))

        output = subprocess.run([stats, path, str(start_us), str(end_us)], capture_output=True, text=True, check=True)
        viewer_count, viewer_amount = output.stdout.split()
        agree = int(viewer_count) == count and Decimal(viewer_amount) == amount
        failed |= not agree

        print(f"| {name}: {start} – {end} | {count:,} | {amount:.8f} | {int(viewer_count):,} trades, {viewer_amount} BTC "
              f"| {'agrees' if agree else 'DIFFERS'} |")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
