#!/usr/bin/env python3
"""Bounded STATIC admission of ComputeWind for normal Nadine rural arms.

Reads pinned PAK and retail PE file bytes only. It does not map executable code,
call retail functions, launch a game, attach to a process, or evaluate a shader.
The conclusion is about the reviewed FgInstance initialization/command path;
it is NOT a proof that an arbitrary live instance has no position writers.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import native_anim_pose_audit as native
from pak_skin_influence_audit import PagedPak
from p99_hand_surface_delta_audit import BODY_SHA256, ROOT, body_ownership
from pe_disassemble_va import Image


DEFAULT_EXE = native.DEFAULT_GAME / "tll.exe"
DEFAULT_BODY = ROOT / "reports/actor_packages/nadine-rural-body.pak"

# These digests pin the complete reviewed control flow, not merely a convenient
# instruction snippet. They guard static evidence against silent build drift;
# the dataflow interpretation was established with IDA callers/callees/RTTI.
FUNCTIONS = (
    (0x14192E590, 0xB2F, "FgInstance_Init", "0cd7c435d94d651d96d1a28eb57f5a971d3e36e64543746e402153ce51ec5d6f"),
    (0x141881C00, 0xE73, "MeshInstance_InitializeSubMeshes", "0b4751c2613cb76e5a94ee7ff24a4759346d2b92fdc96242365a1eb6bf08af58"),
    (0x14187FE60, 0x1FE, "MeshInstance_AllocateSubMeshes", "2ccfad598be7cd1cdd64fec8b7250dc20e0ee17e3aba34d82ca879b7127bc641"),
    (0x14187F1B0, 0x856, "MeshInstance_InitializePrototypeState", "8fe66c0b8b692a8fbe3836766a9ab24b60a00ba931683600338d4bf3376fce7d"),
    (0x141879C90, 0x11E, "SubMeshInstance_Construct", "3bed046f4580f5a86878a732b41b721f50df24f182ac3c94ebe3920951ae66c0"),
    (0x14187A3B0, 0xD11, "SubMeshInstance_InitializeDeformerResources", "19cdc52b2a2464beab31abd6d3a05273b0492db8914dce104cd90ecd6199ab7b"),
    (0x141883580, 0x50C, "MeshInstance_BuildDeformerCommandChain", "b6fd4eedcf79a517f787243004560690ca704abf79098dd875db5f02673e235a"),
    (0x141875F90, 0x10, "ComputeWind_Execute", "096264a97a9b1d315b3e9b44cba67a1cd27fb24bf81aa126ae460b52792bf121"),
    (0x1417483F0, 0x20F, "WindDeformer_Create", "dbfbeda3f61806b69465f83b612e35b4a4eae7cd32618fd8b303472ad1dc4f12"),
    (0x141748650, 0x808, "WindDeformer_Deform", "14ef352ea544b3b912bfdb40975f43c7851368622e69fc84667b09760dcd42bf"),
)

EXACT_FIELDS = (
    (0x14192EC79, bytes.fromhex("c6 44 24 68 00"), "FgInstance a14=false"),
    (0x142B08B38, struct.pack("<Q", 0x142CDDE98), "ComputeWind RTTI locator"),
    (0x142B08B40, struct.pack("<Q", 0x141875F90), "ComputeWind execution slot0"),
    (0x142CDDEA4, struct.pack("<I", 0x3770DC8), "ComputeWind RTTI type RVA"),
    (0x143770DD8, b".?AUComputeWind@@\0", "ComputeWind RTTI name"),
)


def validate_native_contract(read):
    """Require exact reviewed native file bytes, accepting a pure VA reader."""
    for address, size, name, expected in FUNCTIONS:
        value = read(address, size)
        if not isinstance(value, bytes) or len(value) != size:
            raise ValueError(f"truncated native contract: {name}")
        if hashlib.sha256(value).hexdigest() != expected:
            raise ValueError(f"changed native contract: {name}")
    for address, expected, label in EXACT_FIELDS:
        if read(address, len(expected)) != expected:
            raise ValueError(f"changed native contract field: {label}")
    return [{"address": hex(a), "bytes": n, "name": name, "sha256": digest}
            for a, n, name, digest in FUNCTIONS]


def validate_body_scope(pak):
    if hashlib.sha256(pak.data).hexdigest() != BODY_SHA256:
        raise ValueError("changed Nadine rural body asset")
    return body_ownership(pak)


def audit(body_path=DEFAULT_BODY, exe_path=DEFAULT_EXE):
    ownership = validate_body_scope(PagedPak(Path(body_path)))
    image = Image(str(exe_path))  # PE file parser; no executable allocation.
    try:
        if (image.base != native.PREFERRED_BASE or
                image.pe.FILE_HEADER.Machine != 0x8664 or
                hashlib.sha256(image.data).hexdigest() != native.EXPECTED_EXE_SHA256):
            raise ValueError("unsupported retail PE build")
        functions = validate_native_contract(image.read)
    finally:
        image.pe.close()
    return {
        "classification": "STATIC_NORMAL_FGINSTANCE_COMPUTE_WIND_ABSENT",
        "retail_or_game_execution": False,
        "runtime_writes": False,
        "shader_execution": False,
        "body_sha256": BODY_SHA256,
        "exe_sha256": native.EXPECTED_EXE_SHA256,
        "ownership": ownership,
        "native_contract": functions,
        "fg_init_call": "14192ECBD",
        "wind_descriptor_scan_enabled": False,
        "wind_descriptor_type": 23,
        "mesh_wind_enable_bit": "MeshInstance+104 bit400",
        "submesh_wind_pointer": "SubMesh+80",
        "normal_initializer_constructs_wind_deformer": False,
        "normal_command_path_constructs_compute_wind": False,
        "global_or_live_position_writer_absence_proven": False,
        "proof": [
            "14192EC79 writes argument14=false at [rsp+68]; call14192ECBD forwards it through141881CA9 and14187FEC0",
            "14187F400 clears bit400;14187F418 clears override+150; false a10 skips retained descriptor+148 and type23 scan14187F42D..463",
            "14187AC8D nulls SubMesh+80; common bit400 guard14187AC9F excludes both override+1F0 and descriptor type23 WindDeformer_Create calls",
            "141882082..099 derives has-wind bit20 from SubMesh+80;141883916..92E requires bit20 and that pointer before ComputeWind construction",
            "ComputeWind vtable142B08B40 slot0=141875F90; RTTI type143770DC8 and source-assert-named WindDeformer_Create1417483F0/Deform141748650 identify the excluded subsystem",
        ],
        "contrasting_path": "Generic constructor caller14196641E passes argument14=true; no global dead-code claim",
        "falsifiable_prediction": "An unmodified newly initialized normal arms FgInstance has no SubMesh+80 WindDeformer and this command builder emits no ComputeWind for it, not merely a zero-amplitude wind command",
        "limits": [
            "Admission assumes the normal pinned arms is instantiated through the reviewed FgInstance_Init path",
            "No current game instance was sampled; later replacement/reinitialization or arbitrary live writes remain outside this proof",
            "This excludes ComputeWind in this path, not all material vertex shaders, cloth/helper poses or other position writers",
            "No actual weapon contact, grip quality or visually fixed pose is established",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--body", type=Path, default=DEFAULT_BODY)
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    rendered = json.dumps(audit(args.body, args.exe), indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)


if __name__ == "__main__":
    main()
