#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import warnings
from pathlib import Path
from typing import Any

import numpy as np
warnings.filterwarnings("ignore", category=DeprecationWarning)
from sarpy.io.received.crsd import CRSDWriter1
from sarpy.io.received.crsd1_elements.CRSD import CRSDType
from sarpy.io.received.crsd1_elements.Channel import (
    ChannelParametersType,
    ChannelType,
)
from sarpy.io.received.crsd1_elements.CollectionID import CollectionIDType
from sarpy.io.received.crsd1_elements.Data import ChannelSizeType, DataType
from sarpy.io.received.crsd1_elements.Global import (
    FrcvBandType,
    FxBandType,
    GlobalType,
    TimelineType,
)
from sarpy.io.received.crsd1_elements.PVP import (
    PVPType,
    PerVectorParameterF8,
    PerVectorParameterI8,
    PerVectorParameterXYZ,
)
from sarpy.io.received.crsd1_elements.ReferenceGeometry import (
    CRPType,
    LatLonHAEType,
    RcvParametersType,
    ReferenceGeometryType,
    XYZType,
)


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _xyz(values: list[float]) -> XYZType:
    padded = list(values[:3]) + [0.0] * max(0, 3 - len(values))
    return XYZType(X=float(padded[0]), Y=float(padded[1]), Z=float(padded[2]))


def _frequency_bounds(waveform: dict[str, Any]) -> tuple[float, float]:
    axis = waveform.get("frequency_axis_hz") or []
    if axis:
        values = [float(value) for value in axis]
        return min(values), max(values)

    carrier = float(waveform.get("carrier_hz") or 0.0)
    bandwidth = float(waveform.get("bandwidth_hz") or 0.0)
    if carrier <= 0.0:
        raise ValueError("unsupported_product:missing_carrier_hz")
    if bandwidth <= 0.0:
        return carrier, carrier
    return carrier - bandwidth / 2.0, carrier + bandwidth / 2.0


def _pvp_model() -> tuple[PVPType, np.dtype]:
    offset = 0

    def f8() -> PerVectorParameterF8:
        nonlocal offset
        parameter = PerVectorParameterF8(Offset=offset)
        offset += 1
        return parameter

    def i8() -> PerVectorParameterI8:
        nonlocal offset
        parameter = PerVectorParameterI8(Offset=offset)
        offset += 1
        return parameter

    def xyz() -> PerVectorParameterXYZ:
        nonlocal offset
        parameter = PerVectorParameterXYZ(Offset=offset)
        offset += 3
        return parameter

    pvp = PVPType(
        RcvTime=f8(),
        RcvPos=xyz(),
        RcvVel=xyz(),
        RefPhi0=f8(),
        RefFreq=f8(),
        DFIC0=f8(),
        FICRate=f8(),
        FRCV1=f8(),
        FRCV2=f8(),
        DGRGC=f8(),
        SIGNAL=i8(),
    )
    return pvp, pvp.get_vector_dtype()


def _build_signal(channel: dict[str, Any]) -> np.ndarray:
    pulses = channel.get("pulses") or []
    if not pulses:
        raise ValueError("unsupported_product:empty_channel")
    sample_count = len(pulses[0].get("samples") or [])
    if sample_count == 0:
        raise ValueError("unsupported_product:empty_samples")
    signal = np.zeros((len(pulses), sample_count), dtype=np.complex64)
    for pulse_index, pulse in enumerate(pulses):
        samples = pulse.get("samples") or []
        if len(samples) != sample_count:
            raise ValueError("unsupported_product:ragged_signal")
        for sample_index, sample in enumerate(samples):
            signal[pulse_index, sample_index] = np.complex64(
                complex(float(sample.get("real", 0.0)), float(sample.get("imag", 0.0)))
            )
    return signal


def _build_pvps(
    channel: dict[str, Any],
    pvp_dtype: np.dtype,
    frequency_min_hz: float,
    frequency_max_hz: float,
    reference_frequency_hz: float,
) -> np.ndarray:
    pulses = channel.get("pulses") or []
    pvps = np.zeros(len(pulses), dtype=pvp_dtype)
    for pulse_index, pulse in enumerate(pulses):
        parameters = pulse.get("parameters") or {}
        platform = parameters.get("platform") or {}
        pvps["RcvTime"][pulse_index] = float(parameters.get("time_seconds", pulse_index))
        pvps["RcvPos"][pulse_index] = np.asarray(
            platform.get("position_m") or [0.0, 0.0, 0.0],
            dtype=np.float64,
        )
        pvps["RcvVel"][pulse_index] = np.asarray(
            platform.get("velocity_mps") or [0.0, 0.0, 0.0],
            dtype=np.float64,
        )
        pvps["RefPhi0"][pulse_index] = 0.0
        pvps["RefFreq"][pulse_index] = reference_frequency_hz
        pvps["DFIC0"][pulse_index] = 0.0
        pvps["FICRate"][pulse_index] = 0.0
        pvps["FRCV1"][pulse_index] = frequency_min_hz
        pvps["FRCV2"][pulse_index] = frequency_max_hz
        pvps["DGRGC"][pulse_index] = 0.0
        pvps["SIGNAL"][pulse_index] = 1
    return pvps


def write_crsd(
    product: dict[str, Any],
    output_crsd: Path,
    metadata_json: Path,
    pvp_json: Path,
    provenance_json: Path,
    chunk_index_json: Path,
) -> None:
    channels = product.get("channels") or []
    if len(channels) != 1:
        raise ValueError("unsupported_product:crsd_writer_supports_single_channel")

    channel = channels[0]
    waveform = channel.get("waveform") or {}
    channel_id = str(channel.get("channel_id") or "channel_0")
    signal = _build_signal(channel)
    pvp, pvp_dtype = _pvp_model()
    frequency_min_hz, frequency_max_hz = _frequency_bounds(waveform)
    reference_frequency_hz = float(waveform.get("carrier_hz") or ((frequency_min_hz + frequency_max_hz) / 2.0))
    pvps = _build_pvps(channel, pvp_dtype, frequency_min_hz, frequency_max_hz, reference_frequency_hz)

    pulses = channel.get("pulses") or []
    rcv_time_1 = float(pulses[0].get("parameters", {}).get("time_seconds", 0.0))
    rcv_time_2 = float(pulses[-1].get("parameters", {}).get("time_seconds", rcv_time_1))
    reference_platform = (
        product.get("reference_geometry", {}).get("reference_platform", {})
        or pulses[0].get("parameters", {}).get("platform", {})
    )
    reference_position = reference_platform.get("position_m") or [0.0, 0.0, 0.0]
    reference_velocity = reference_platform.get("velocity_mps") or [0.0, 0.0, 0.0]
    scene_center = product.get("reference_geometry", {}).get("scene_center_m") or [0.0, 0.0, 0.0]
    collection = product.get("collection") or {}
    product_id = str(collection.get("product_id") or collection.get("collection_id") or "graphx-gotcha-crsd")

    metadata = CRSDType(
        CollectionID=CollectionIDType(
            CollectorName=str(collection.get("collector_name") or "GOTCHA"),
            CoreName=product_id,
            CollectType="MONOSTATIC",
            Classification="UNCLASSIFIED",
            ReleaseInfo="UNRESTRICTED",
        ),
        Global=GlobalType(
            Timeline=TimelineType(
                CollectionRefTime=np.datetime64("2000-01-01T00:00:00"),
                RcvTime1=rcv_time_1,
                RcvTime2=rcv_time_2,
            ),
            FrcvBand=FrcvBandType(FrcvMin=frequency_min_hz, FrcvMax=frequency_max_hz),
            FxBand=FxBandType(FxMin=frequency_min_hz, FxMax=frequency_max_hz),
        ),
        Data=DataType(
            SignalArrayFormat="CF8",
            NumBytesPVP=pvp_dtype.itemsize,
            Channels=[
                ChannelSizeType(
                    Identifier=channel_id,
                    NumVectors=int(signal.shape[0]),
                    NumSamples=int(signal.shape[1]),
                    SignalArrayByteOffset=0,
                    PVPArrayByteOffset=0,
                )
            ],
        ),
        Channel=ChannelType(
            RefChId=channel_id,
            Parameters=[
                ChannelParametersType(
                    Identifier=channel_id,
                    RefVectorIndex=0,
                    RefFreqFixed=True,
                    FrcvFixed=False,
                    DemodFixed=True,
                    F0Ref=reference_frequency_hz,
                    Fs=float(waveform.get("sample_rate_hz") or 1.0),
                    BWInst=max(float(waveform.get("bandwidth_hz") or 0.0), frequency_max_hz - frequency_min_hz, 1.0),
                    RcvPol="H",
                    SignalNormal=True,
                )
            ],
        ),
        PVP=pvp,
        ReferenceGeometry=ReferenceGeometryType(
            CRP=CRPType(
                ECF=_xyz(scene_center),
                LLH=LatLonHAEType(Lat=0.0, Lon=0.0, HAE=0.0),
            ),
            RcvParameters=RcvParametersType(
                RcvTime=rcv_time_1,
                RcvPos=_xyz(reference_position),
                RcvVel=_xyz(reference_velocity),
                SideOfTrack="R",
                SlantRange=max(float(np.linalg.norm(np.asarray(reference_position, dtype=np.float64) - np.asarray(scene_center, dtype=np.float64))), 1.0),
                GroundRange=0.0,
                DopplerConeAngle=90.0,
                GrazeAngle=45.0,
                IncidenceAngle=45.0,
                AzimuthAngle=0.0,
            ),
        ),
    )

    if not metadata.is_valid(recursive=True, stack=False):
        raise ValueError("crsd_metadata_invalid")

    output_crsd.parent.mkdir(parents=True, exist_ok=True)
    writer = CRSDWriter1(str(output_crsd), meta=metadata, check_existence=False)
    try:
        writer.write_file({channel_id: pvps}, {channel_id: signal})
    finally:
        writer.close()

    _write_json(
        metadata_json,
        {
            "schema": "graphx.sar.crsd.metadata.v1",
            "format": "crsd",
            "label": "STANDARDS-TARGETED",
            "crsd_file": output_crsd.name,
            "sarpy_writer": "sarpy.io.received.crsd.CRSDWriter1",
            "collection": collection,
            "channel": {
                "channel_id": channel_id,
                "num_vectors": int(signal.shape[0]),
                "num_samples": int(signal.shape[1]),
                "signal_array_format": "CF8",
                "frequency_min_hz": frequency_min_hz,
                "frequency_max_hz": frequency_max_hz,
            },
        },
    )
    _write_json(
        pvp_json,
        {
            "schema": "graphx.sar.crsd.pvp.v1",
            "num_bytes_pvp": pvp_dtype.itemsize,
            "fields": list(pvp_dtype.names or []),
            "pulse_count": int(signal.shape[0]),
        },
    )
    _write_json(
        provenance_json,
        {
            "schema": "graphx.sar.crsd.provenance.v1",
            "provenance_label": collection.get("provenance_label"),
            "source_ordering": collection.get("source_ordering"),
            "source_files": collection.get("source_files") or [],
            "notes": [
                "MATLAB is not used.",
                "CRSD output is written from the GraphX normalized SAR product.",
            ],
        },
    )
    _write_json(
        chunk_index_json,
        {
            "schema": "graphx.sar.crsd.chunk_index.v1",
            "crsd_file": output_crsd.name,
            "chunks": [
                {
                    "channel_id": channel_id,
                    "pulse_start": 0,
                    "pulse_end": int(signal.shape[0] - 1),
                    "pulse_count": int(signal.shape[0]),
                    "sample_count": int(signal.shape[1]),
                }
            ],
        },
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Write a SarPy-openable CRSD from a GraphX normalized SAR product JSON")
    parser.add_argument("--input-json", required=True)
    parser.add_argument("--output-crsd", required=True)
    parser.add_argument("--metadata-json", required=True)
    parser.add_argument("--pvp-json", required=True)
    parser.add_argument("--provenance-json", required=True)
    parser.add_argument("--chunk-index-json", required=True)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    product = json.loads(Path(args.input_json).read_text(encoding="utf-8"))
    write_crsd(
        product,
        Path(args.output_crsd),
        Path(args.metadata_json),
        Path(args.pvp_json),
        Path(args.provenance_json),
        Path(args.chunk_index_json),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
