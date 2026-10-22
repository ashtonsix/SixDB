#!/usr/bin/env python3
"""Bounded, row-aligned Jena windows from the pinned original decimal CSV.

Uses the existing dataset cache; never expands the 43 MB CSV to disk. Defaults
to three 65,536-row windows, all 14 weather fields, original row positions and
date text. Weather values are exact scale-100 signed integers, not IEEE floats.
The source's -9999 wind sentinel stays a value. Larger samples belong on workers.
"""

import argparse
import csv
from datetime import datetime
from decimal import Decimal
import hashlib
import io
from pathlib import Path
import sys
import zipfile

import numpy as np
import polars as pl

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import datasets

SOURCE = {
    "url": "https://storage.googleapis.com/tensorflow/tf-keras-datasets/"
           "jena_climate_2009_2016.csv.zip",
    "bytes": 13568290,
    "sha256": "63d757501e92284a7de7cdbef0337f03b24e13ead7ac2b5b8c86a18d8e38ba5b",
}
CSV_NAME = "jena_climate_2009_2016.csv"
CSV_BYTES = 43164220
CSV_SHA256 = "9626d3964f7afb3fa94b6f3d3a0e92fe96e4a4477c31ed9875cc64d6717c129f"
SOURCE_ROWS = 420551
FIELDS = ["p (mbar)", "T (degC)", "Tpot (K)", "Tdew (degC)", "rh (%)",
          "VPmax (mbar)", "VPact (mbar)", "VPdef (mbar)", "sh (g/kg)",
          "H2OC (mmol/mol)", "rho (g/m**3)", "wv (m/s)",
          "max. wv (m/s)", "wd (deg)"]


def fixed100(text):
    scaled = Decimal(text) * 100
    if (not scaled.is_finite() or scaled != scaled.to_integral_value() or
            not -(1 << 63) <= scaled < (1 << 63)):
        raise ValueError(f"not an exact scale-100 int64: {text!r}")
    return int(scaled)


def prepare(starts, rows):
    if (not starts or len(set(starts)) != len(starts) or not 1 <= rows <= 65536 or
            any(start < 0 or start + rows > SOURCE_ROWS for start in starts)):
        raise ValueError("use distinct in-bounds windows of 1..65536 rows")
    if len(starts) * rows > 3 * 65536:
        raise ValueError("local preparation is capped at 196608 selected rows")
    archive = datasets.source(SOURCE)

    def build(output):
        with zipfile.ZipFile(archive) as compressed:
            if compressed.getinfo(CSV_NAME).file_size != CSV_BYTES:
                raise ValueError("unexpected uncompressed CSV size")
            checksum = hashlib.sha256()
            with compressed.open(CSV_NAME) as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    checksum.update(chunk)
            if checksum.hexdigest() != CSV_SHA256:
                raise ValueError("CSV differs from the retained source identity")
            columns = np.empty((len(starts), len(FIELDS), rows), dtype=np.int64)
            dates = [[] for _ in starts]
            civil = np.empty((len(starts), rows), dtype=np.int64)
            counts = [0] * len(starts)
            epoch = datetime(1970, 1, 1)
            seen = 0
            with compressed.open(CSV_NAME) as stream:
                records = csv.reader(io.TextIOWrapper(stream, encoding="utf-8", newline=""))
                if next(records) != ["Date Time", *FIELDS]:
                    raise ValueError("unexpected source columns")
                for position, record in enumerate(records):
                    seen += 1
                    for part, start in enumerate(starts):
                        if not start <= position < start + rows:
                            continue
                        if len(record) != len(FIELDS) + 1:
                            raise ValueError("malformed source row")
                        local = position - start
                        dates[part].append(record[0])
                        # A civil coordinate, not an asserted timezone/UTC instant.
                        stamp = datetime.strptime(record[0], "%d.%m.%Y %H:%M:%S")
                        civil[part, local] = (stamp - epoch).days * 86400 + (stamp - epoch).seconds
                        for field, text in enumerate(record[1:]):
                            columns[part, field, local] = fixed100(text)
                        counts[part] += 1
            if seen != SOURCE_ROWS or any(count != rows for count in counts):
                raise ValueError("source population/window count changed")

        windows = []
        for part, start in enumerate(starts):
            frame = pl.DataFrame({name: columns[part, i] for i, name in enumerate(FIELDS)})
            frame = frame.with_columns(
                pl.Series("_source_row", np.arange(start, start + rows, dtype=np.uint32)),
                pl.Series("_date_text", dates[part]),
                pl.Series("_time_civil_s", civil[part]))
            name = f"jena-{start}-{rows}.parquet"
            frame.write_parquet(output / name)
            windows.append({"file": name, "row_start": start, "rows": rows,
                            "wind_faults": {field: int((frame[field] == -999900).sum())
                                            for field in ("wv (m/s)", "max. wv (m/s)")}})
        return {"source": SOURCE, "source_csv_sha256": CSV_SHA256,
                "source_rows": SOURCE_ROWS, "windows": windows,
                "weather_scale": 100, "weather_type": "signed-int64",
                "order": "original CSV order, one shared window across all fields",
                "date_coordinate": "naive Gregorian civil seconds since 1970-01-01; no timezone inferred",
                "changes": ["decimal text mapped exactly to scale-100 integers",
                            "original date text retained; civil coordinate added",
                            "original row positions added; no sentinel cleanup or null injection"],
                "scope": "local encoding input; windows are not representative population samples"}

    return datasets.cached("frame-jena-windows", Path(__file__), [SOURCE],
                           {"starts": starts, "rows": rows}, build)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--starts", nargs="+", type=int, default=[0, 196608, 355015])
    parser.add_argument("--rows", type=int, default=65536)
    args = parser.parse_args()
    print(prepare(args.starts, args.rows))
