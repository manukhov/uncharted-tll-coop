"""Read-only, pinned DXIL disassembly of TLL's packed skinning shaders.

Only the installed Windows SDK disassembler executes: no retail function,
shader, D3D device, game process, compilation, or runtime write is involved.
The scalar weight helper describes the decoded influence, not rendered contact.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import mmap
from pathlib import Path
import re
import struct
import uuid

import native_anim_pose_audit as native

DEFAULT_ARCHIVE = native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy/shaders.psarc"
DEFAULT_COMPILER = Path(r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxcompiler.dll")
WEIGHT_MASK = (1 << 22) - 1
WEIGHT_SCALE_BITS = 0x34800002
WEIGHT_SCALE = struct.unpack("<f", struct.pack("<I", WEIGHT_SCALE_BITS))[0]
SHADERS = {
    "Cs_SkinningPosOnly-mode1": "2df022edb0076fe59a2e024223508182f9a3aaaaf726a2d3b0294e5d753510e5",
    "Cs_SkinningPosNormal-mode1": "5b71091d9a688bcee502c606da6de5d63c01f9a15e225ee4b7b3940cc1f560dc",
    "Cs_SkinningPosNormalTangent-mode1": "be454c78e9fe286eae122e9d6815b719d927f51c1757564d2efe068426800cbd",
    "Cs_SkinningPosNormalExtraNormal-mode1": "63486dc04b36ac210339cc682e4f6a3dbfc2a97bc3add24ccf847c690aacb725",
    "Cs_SkinningPosNormalTangentExtraNormal-mode1": "6fd2764df195c4e9647d24cae1f7fdf8efe6eed2877ca0b08bffe287236ea545",
}


def decode_word(word: int):
    if type(word) is not int or not 0 <= word <= 0xFFFFFFFF:
        raise ValueError("influence must be an unsigned 32-bit word")
    numerator = word & WEIGHT_MASK
    weight = struct.unpack("<f", struct.pack("<f", numerator * WEIGHT_SCALE))[0]
    return word >> 22, numerator, weight


def admit_blob(name: str, blob: bytes):
    if name not in SHADERS or hashlib.sha256(blob).hexdigest() != SHADERS[name]:
        raise ValueError("unreviewed skinning shader bytes")
    if len(blob) < 36 or blob[:4] != b"DXBC" or struct.unpack_from("<I", blob, 24)[0] != len(blob):
        raise ValueError("invalid DXBC container")
    count = struct.unpack_from("<I", blob, 28)[0]
    if not 0 < count <= 32 or 32 + 4 * count > len(blob):
        raise ValueError("invalid DXBC chunk table")
    chunks = []
    for index in range(count):
        at = struct.unpack_from("<I", blob, 32 + 4 * index)[0]
        if at < 32 + 4 * count or at + 8 > len(blob):
            raise ValueError("invalid DXBC chunk offset")
        size = struct.unpack_from("<I", blob, at + 4)[0]
        if at + 8 + size > len(blob):
            raise ValueError("invalid DXBC chunk extent")
        chunks.append(blob[at:at + 4].decode("ascii"))
    if chunks.count("DXIL") != 1:
        raise ValueError("expected one DXIL chunk")
    return chunks


def disassemble(blob: bytes, compiler: Path = DEFAULT_COMPILER) -> str:
    # ABI verified against SDK26100 um/dxcapi.h. Only Compiler3::Disassemble.
    class Buffer(ctypes.Structure):
        _fields_ = [("Ptr", ctypes.c_void_p), ("Size", ctypes.c_size_t), ("Encoding", ctypes.c_uint32)]

    def guid(value):
        return (ctypes.c_ubyte * 16).from_buffer_copy(uuid.UUID(value).bytes_le)

    def method(pointer, index, result, *arguments):
        table = ctypes.cast(pointer, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        return ctypes.WINFUNCTYPE(result, ctypes.c_void_p, *arguments)(table[index])

    def checked(value):
        if value < 0:
            raise RuntimeError(f"DXC disassembly HRESULT 0x{value & 0xFFFFFFFF:08X}")

    compiler = Path(compiler).resolve(strict=True)
    library = ctypes.WinDLL(str(compiler))
    create = library.DxcCreateInstance
    create.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    create.restype = ctypes.c_long
    instance, result, output = ctypes.c_void_p(), ctypes.c_void_p(), ctypes.c_void_p()
    try:
        checked(create(guid("73e22d93-e6ce-47f3-b5bf-f0664f39c1b0"),
                       guid("228b4687-5a6a-4730-900c-9702b2203f54"), ctypes.byref(instance)))
        storage = ctypes.create_string_buffer(blob)
        buffer = Buffer(ctypes.cast(storage, ctypes.c_void_p), len(blob), 0)
        checked(method(instance, 4, ctypes.c_long, ctypes.POINTER(Buffer), ctypes.c_void_p,
                       ctypes.POINTER(ctypes.c_void_p))(
                           instance, ctypes.byref(buffer), guid("58346cda-dde7-4497-9461-6f87af5e0659"),
                           ctypes.byref(result)))
        status = ctypes.c_long()
        checked(method(result, 3, ctypes.c_long, ctypes.POINTER(ctypes.c_long))(result, ctypes.byref(status)))
        checked(status.value)
        checked(method(result, 4, ctypes.c_long, ctypes.POINTER(ctypes.c_void_p))(result, ctypes.byref(output)))
        if not output:
            raise RuntimeError("DXC returned no disassembly")
        pointer = method(output, 3, ctypes.c_void_p)(output)
        size = method(output, 4, ctypes.c_size_t)(output)
        if not pointer or not 0 < size <= 2 * 1024 * 1024:
            raise RuntimeError("invalid DXC disassembly extent")
        return ctypes.string_at(pointer, size).rstrip(b"\0").decode("utf-8")
    finally:
        for pointer in (output, result, instance):
            if pointer:
                method(pointer, 2, ctypes.c_ulong)(pointer)


def prove_packed_ir(text: str):
    """Check the exact connected unpack chain, not unrelated constant hits.

    The pinned full IR is the evidence for linear weighted dot4 accumulation;
    these checks guard its critical format, stride, and no-renormalization facts.
    """
    shifted = re.findall(r"(%\d+) = lshr i32 (%\d+), 22\b", text)
    if len(shifted) != 1:
        raise ValueError("expected one packed joint extraction")
    joint, packed = shifted[0]
    mask = re.search(rf"(%\d+) = and i32 {re.escape(packed)}, 4194303\b", text)
    if not mask:
        raise ValueError("packed 22-bit weight mask changed")
    cast = re.search(rf"(%\d+) = uitofp i32 {re.escape(mask[1])} to float", text)
    if not cast:
        raise ValueError("packed weight is not unsigned integer to float")
    scale = re.search(rf"(%\d+) = fmul fast float {re.escape(cast[1])}, 0x3E90000040000000\b", text)
    if not scale:
        raise ValueError("packed UNORM22 float scale changed")
    if not re.search(rf"mul nuw nsw i32 {re.escape(joint)}, 48\b", text):
        raise ValueError("direct 48-byte palette row addressing changed")
    if not re.search(r"add i32 %\d+, 4\b", text) or "g_infl" not in text or "SkinningVertexInfo" not in text:
        raise ValueError("packed influence stream/cursor changed")
    if "@dx.op.dot4.f32" not in text or "fdiv" in text or "@dx.op.binary.f32(i32 21" in text:
        raise ValueError("linear position math/no weight renormalization changed")
    weighted = re.findall(rf"(%\d+) = fmul fast float (%\d+), {re.escape(scale[1])}\b", text)
    dot4 = set(re.findall(r"(%\d+) = call float @dx.op.dot4.f32\(", text))
    positions = [output for output, operand in weighted if operand in dot4]
    if len(positions) not in (3, 6) or not all(
            re.search(rf"fadd fast float {re.escape(output)}, %\d+\b", text) for output in positions):
        raise ValueError("weighted linear position accumulation missing")
    excerpts = []
    for number, line in enumerate(text.splitlines(), 1):
        if any(token in line for token in (f"{joint} =", f"{mask[1]} =", f"{cast[1]} =", f"{scale[1]} =")):
            excerpts.append({"line": number, "instruction": line.strip()})
    return {"joint_shift": 22, "weight_mask": WEIGHT_MASK, "weight_scale_f32_bits": f"{WEIGHT_SCALE_BITS:08X}",
            "weight_scale": WEIGHT_SCALE, "palette_row_bytes": 48, "weight_sum_renormalized": False,
            "weighted_position_lanes": len(positions),
            "position_model": "linear sum of weight * dot4(palette_row, (bind_position,1)); not dual quaternion",
            "unpack_ir": excerpts}


def audit(archive=DEFAULT_ARCHIVE, compiler=DEFAULT_COMPILER):
    archive = Path(archive)
    with archive.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapping:
        package = native.load_audit_module().Psarc(archive, None, data=mapping)
        blobs = {name: package.extract(f"bytecode/{name}.cxo") for name in SHADERS}
    shaders = []
    for name in SHADERS:
        blob = blobs[name]
        chunks = admit_blob(name, blob)
        text = disassemble(blob, compiler)
        shaders.append({"name": name, "bytes": len(blob), "sha256": SHADERS[name], "chunks": chunks,
                        "disassembly_sha256": hashlib.sha256(text.encode()).hexdigest(), **prove_packed_ir(text)})
    return {"classification": "STATIC_PINNED_DXIL_PACKED_SKINNING_FORMAT",
            "retail_or_shader_execution": False, "sdk_disassembly_only": True, "shaders": shaders,
            "limits": ["Mode1 only; mode5 cache optimization and debug passthrough not admitted here",
                       "Palette ordinals require separately proven mesh/animation palette matching",
                       "GPU fast float math is not an instruction-exact scalar surface oracle",
                       "No current drawable ownership, skin contact, pose fix, or game test claim"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--compiler", type=Path, default=DEFAULT_COMPILER)
    args = parser.parse_args()
    print(json.dumps(audit(args.archive, args.compiler), indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
