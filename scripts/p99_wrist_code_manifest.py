#!/usr/bin/env python3
"""Generate a bounded loaded-code integrity contract; never execute retail code.

The region sizes/names were checked through the single-IDB MCP. Disk SHA pins
the exact source bytes. This is NOT a transitive executable/lifetime proof:
OS/CRT imports, mutable globals, native unwind metadata, and the wider Async /
Process call graph remain explicit boundaries. The generated header is inert.
--emit prints a header; --check compares it. Neither mode writes files.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct

import p99_wrist_source_boundary_audit as boundary
import p99_wrist_helper_inputs_audit as dependency

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'scanner/src/nadine_p99_wrist_code_manifest.h'
IMAGE_BASE = 0x140000000
EXE_SHA256 = boundary.EXE_SHA256
OPCODES = (40, 41, 44, 47, 49, 50, 51, 52, 53, 54, 128, 129, 130, 131, 132)
WRAPPERS = dict(zip(OPCODES, (
    0x2194470, 0x2194680, 0x21946D0, 0x2194720, 0x2194880,
    0x2194940, 0x21949E0, 0x2194AB0, 0x2194B10, 0x2194C30,
    0x1A97060, 0x1A951F0, 0x1A945F0, 0x1A94D80, 0x1A95E80)))
CALLS = (
    (0x1A92E78, bytes.fromhex('e843690200'), 0x1AB97C0),
    (0x1A932F8, bytes.fromhex('e8c3640200'), 0x1AB97C0),
    (0x1ABA973, bytes.fromhex('e8b80c6c00'), 0x217B630),
)


@dataclass(frozen=True)
class Region:
    rva: int
    size: int
    name: str
    role: str


# MCP function envelopes, not prologue-only hashes. Existing IDB names are kept
# verbatim; sub_* entries are not newly claimed semantic labels.
REGIONS = tuple(sorted((Region(*row) for row in (
    (0x1A92A00,0xC3D,'AnimManager_AsyncObjectUpdateJointBlendJob','frame'),
    (0x1AB97C0,0x1A4C,'ProcessAnimCmdListSegment','frame'),
    (0x217B630,0x197,'AnimCmdEvaluator_ExecuteSkeletonJointPrograms','executor'),
    (0x2194470,0x2F,'AnimJointProgramOp40_PropagateJointMatrices','opcode'),
    (0x2194680,0x42,'AnimJointProgramOp41_ScatterQuaternionEulerDegrees','opcode'),
    (0x21946D0,0x42,'AnimJointProgramOp44_AssembleEulerQuaternions','opcode'),
    (0x2194720,0x91,'AnimJointProgramOp47_ApplyScalarCoefficients','opcode'),
    (0x2194880,0xB2,'AnimJointProgramOp49_TransformEncodedPoints','opcode'),
    (0x2194940,0x99,'AnimJointProgramOp50_BuildRelativeRotationQuaternions','opcode'),
    (0x21949E0,0xC5,'AnimJointProgramOp51_BuildAimQuaternions','opcode'),
    (0x2194AB0,0x5C,'AnimJointProgramOp52_WriteAuthoredVectorsAndScalars','opcode'),
    (0x2194B10,0x11B,'AnimJointProgramOp53_CopyEncodedVectors','opcode'),
    (0x2194C30,0x2F,'AnimJointProgramOp54_CopyEncodedScalars','opcode'),
    (0x1A97060,0x731,'AnimJointProgramCustomOp128','opcode'),
    (0x1A951F0,0x8EC,'AnimJointProgramCustomOp129','opcode'),
    (0x1A945F0,0x5B7,'AnimJointProgramCustomOp130','opcode'),
    (0x1A94D80,0x3D4,'AnimJointProgramCustomOp131','opcode'),
    (0x1A95E80,0xE54,'AnimJointProgramCustomOp132','opcode'),
    (0x2189FB0,0x292,'AnimJointProgram_PropagateJointMatrices','math'),
    (0x218BC40,0x5BA,'AnimJointProgram_ScatterQuaternionEulerDegrees','math'),
    (0x218B780,0x4B5,'AnimJointProgram_QuaternionSoAToEulerDegrees','math'),
    (0x218C530,0x58F,'AnimJointProgram_AssembleEulerQuaternions','math'),
    (0x218C200,0x321,'AnimJointProgram_EulerDegreesSoAToQuaternion','math'),
    (0x2192B10,0x215,'AnimJointProgram_ApplyScalarCoefficients_Arity1','math'),
    (0x2192D30,0x29F,'AnimJointProgram_ApplyScalarCoefficients_Arity2','math'),
    (0x2192FD0,0x314,'AnimJointProgram_ApplyScalarCoefficients_Arity3','math'),
    (0x218D570,0x28A,'AnimJointProgram_AccumulateWeightedTransformedPoints','math'),
    (0x218D800,0x267,'AnimJointProgram_NormalizeHomogeneousVectors8','math'),
    (0x218E120,0x754,'AnimJointProgram_WriteEncodedVectorBatches','math'),
    (0x218F660,0xA57,'AnimJointProgram_AccumulateRelativeRotationQuaternions','math'),
    (0x2190910,0x283,'AnimJointProgram_NormalizeAndPostMultiplyQuaternions4','math'),
    (0x2181430,0x1FA,'AnimJointProgram_BasisColumnsToQuaternion4','math'),
    (0x218D2D0,0x292,'AnimJointProgram_InitializeEncodedVectors','math'),
    (0x21902D0,0x638,'AnimJointProgram_BuildAimQuaternions','math'),
    (0x218F3B0,0x2A1,'AnimJointProgram_BasisColumnsToQuaternion','math'),
    (0x21897C0,0x1C1,'AnimJointProgram_WriteAuthoredVectorsAndScalars','math'),
    (0x2189370,0x446,'AnimJointProgram_CopyEncodedScalars','math'),
    (0x217FCF0,0x189,'sub_14217FCF0','math'),
    (0x217FE80,0x1A6,'sub_14217FE80','math'),
    (0x2180030,0x226,'sub_142180030','math'),
    (0x21817F0,0x139,'sub_1421817F0','math'),
    (0x21803E0,0x1DA,'sub_1421803E0','math'),
    (0x2182E50,0x27B,'sub_142182E50','math'),
    (0x2182A20,0x2B0,'sub_142182A20','math'),
    (0x1A96D10,0xFF,'sub_141A96D10','math'),
    (0x942E30,0xA3,'sub_140942E30','math'),
    (0x98B3C0,0xB0,'sub_14098B3C0','math'),
    (0x21777E0,0x23A,'sub_1421777E0','math'),
    (0xA7F090,0x16E,'sub_140A7F090','math'),
    (0x9CD8C0,0xB8,'sub_1409CD8C0','math'),
    (0x1A945E0,0x3,'nullsub_80','support'),
    (0x14D7C0,0x63,'sub_14014D7C0','support'),
    (0x160C120,0x17B,'sub_14160C120','support'),
    (0x15B1C80,0x5B,'sub_1415B1C80','support'),
    (0x2207E90,0x31,'sub_142207E90','support'),
    (0x24EAB30,0x21,'__security_check_cookie','support'),
    (0x24EBF10,0x51,'_alloca_probe','support'),
    (0x24ED25C,0x6,'cosf','import_thunk'),
    (0x24ED262,0x6,'sinf','import_thunk'),
    (0x24ED2AA,0x6,'acosf','import_thunk'),
    (0x24ED256,0x6,'atan2f','import_thunk'),
    (0x24ED268,0x6,'sqrtf','import_thunk'),
    (0x255E2D4,0x6,'fmodf','import_thunk'),
)), key=lambda region: region.rva))

# Frame bodies are pinned, NOT their whole scheduler/evaluator dependency tree.
# These boundaries must not be silently promoted to a complete native closure.
LIMITS = (
    'Async/Process other direct callees and virtual/callback targets are not recursively pinned.',
    'Executor indirect CALL 14217B742 is bound by the separate live dispatch/program manifest.',
    'CRT math thunks are pinned, but their loaded IAT targets and external DLL bodies are not.',
    'QPC/FlsGetValue imports, profiler globals/TLS/FLS state and assertion callbacks remain external.',
    'Security-cookie failure tail and native unwind metadata are not a normal-path code proof.',
    'Read-only numeric constant tables outside these envelopes are not hashed by this code-only contract.',
    'No module/page lifetime, later code immutability, owner, source or runtime admission follows.',
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_layout(regions=REGIONS):
    require(len({r.rva for r in regions}) == len(regions), 'duplicate code region')
    for previous, region in zip(regions, regions[1:]):
        require(previous.rva+previous.size <= region.rva, 'overlapping code regions')
    require(all(r.rva > 0 and 0 < r.size <= 0x10000 for r in regions), 'code extent')
    require({r.rva for r in regions if r.role == 'opcode'} == set(WRAPPERS.values()),
            'all fifteen g0 opcode bodies are required')
    for rva, stock, target in CALLS:
        require(stock[0] == 0xE8 and len(stock) == 5 and
                rva+5+struct.unpack_from('<i',stock,1)[0] == target, 'self-call identity')
        require(sum(r.rva <= rva and rva+5 <= r.rva+r.size for r in regions) == 1,
                'self-call region membership')


def canonicalize(region, data, stage='stock', installed_calls=()):
    """Owned-byte pure model. Never wildcard/mask an unverified displacement.

    Installed expectations must come from this installer's actual relay plans;
    accepting arbitrary E8 targets is not sufficient. Only after equality checks
    are all four displacement bytes restored to their canonical STOCK values.
    """
    require(region in REGIONS and len(data) == region.size, 'region identity/extent')
    require(stage in ('stock','installed'), 'code stage')
    if stage == 'stock':
        require(not installed_calls, 'stock stage cannot carry installed calls')
    else:
        require(len(installed_calls) == len(CALLS) and
                all(len(call) == 5 and call[0] == 0xE8 for call in installed_calls),
                'three exact installed E8 plans required')
    result = bytearray(data)
    replacements = []
    for i, (rva, stock, _) in enumerate(CALLS):
        if region.rva <= rva and rva+5 <= region.rva+region.size:
            offset = rva-region.rva
            expected = stock if stage == 'stock' else bytes(installed_calls[i])
            require(data[offset:offset+5] == expected, f'self-call bytes {rva:X}')
            replacements.append((offset,stock))
    for offset,stock in replacements:
        result[offset+1:offset+5] = stock[1:]
    return bytes(result)


def reject_relocation_overlaps(regions, relocations):
    """Do not silently mask ASLR fixups or tolerate partial overlaps."""
    for rva,width in relocations:
        require(width > 0, 'relocation extent')
        for region in regions:
            require(rva+width <= region.rva or region.rva+region.size <= rva,
                    f'base relocation overlaps code region {region.rva:X}')


def branch_inventory(data):
    skeleton = dependency.source.load_skeleton(data, dependency.source.NADINE_SKELETON)
    base, ops = dependency.group_program(data,skeleton,0)
    require(len(ops) == 51 and tuple(sorted({op.opcode for op in ops})) == OPCODES,
            'g0 opcode inventory')
    arities = tuple((op.ordinal,op.payload[3]) for op in ops if op.opcode == 47)
    require(arities == ((1,2),(10,1),(11,2),(12,3),(25,1),(26,2)), 'op47 branch inventory')
    points, rotations, aims = [], [], []
    for op in ops:
        if op.opcode not in (49,50,51):
            continue
        count = op.payload[0]
        desc = base+(op.payload[2]&~3)
        if op.opcode == 49:
            points.append((op.ordinal,*struct.unpack_from('<III',data,desc+12*count)))
        elif op.opcode == 50:
            rotations.append((op.ordinal,dependency.program.u32(data,desc+28*count)))
        else:
            header = desc+4*count+2*((count+7)&~7)
            aims.append((op.ordinal,dependency.program.u32(data,header)))
    require(points == [(30,8,32,0),(33,8,32,0),(38,0,16,8),(42,8,32,0)], 'op49 branch inventory')
    require(rotations == [(24,1),(29,1),(37,1)], 'op50 auxiliary inventory')
    require(aims == [(35,1),(45,1),(47,1)], 'op51 auxiliary inventory')
    return {'op47_ordinal_arity': arities, 'op49_ordinal_norm_offset_vectors': points,
            'op50_ordinal_aux_count': rotations, 'op51_ordinal_aux_count': aims,
            'op47_arity4_generic_excluded_by_authored_payload': True,
            'op49_vector_worker_14218E120_required_by_ordinal38': True}


def read_image_regions(image):
    import pefile
    require(hashlib.sha256(image.data).hexdigest() == EXE_SHA256, 'retail executable SHA-256')
    require(image.base == IMAGE_BASE and image.pe.FILE_HEADER.Machine == 0x8664, 'PE identity')
    image.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_BASERELOC']])
    relocations = []
    for block in getattr(image.pe,'DIRECTORY_ENTRY_BASERELOC',()):
        for entry in block.entries:
            if entry.type == 0:
                continue
            require(entry.type == 10, 'unreviewed PE relocation type')
            relocations.append((entry.rva,8))
    reject_relocation_overlaps(REGIONS,relocations)
    boundary.verify_boundary_instructions(image.read)
    contents = []
    for region in REGIONS:
        sections = [s for s in image.pe.sections if s.VirtualAddress <= region.rva and
                    region.rva+region.size <= s.VirtualAddress+s.SizeOfRawData and
                    region.rva+region.size <= s.VirtualAddress+s.Misc_VirtualSize]
        require(len(sections) == 1 and sections[0].Characteristics & 0x20000000,
                'complete executable raw-backed region')
        raw = image.read(IMAGE_BASE+region.rva,region.size)
        require(raw is not None and len(raw) == region.size, 'truncated code region')
        canonicalize(region,raw)
        contents.append(raw)
    return tuple(contents), len(relocations)


def control_edge_inventory(contents):
    """Advisory linear PE decode: exposes gaps, never proves reachability.

    Immediate CALLs and out-of-envelope tail JMPs are both retained, unlike
    get_callees which misses several native tailcalls. Embedded switch tables
    can stop linear decoding; MCP-reviewed branch inventory remains separate.
    """
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
    md.detail = True
    edges, indirect = [], []
    for region,raw in zip(REGIONS,contents):
        for ins in md.disasm(raw,IMAGE_BASE+region.rva):
            if ins.mnemonic not in ('call','jmp') or not ins.operands:
                continue
            op = ins.operands[0]
            if op.type == capstone.x86.X86_OP_IMM:
                target = op.imm-IMAGE_BASE
                if region.rva <= target < region.rva+region.size:
                    continue
                covered = any(r.rva <= target < r.rva+r.size for r in REGIONS)
                excluded = region.rva == WRAPPERS[47] and target in (0x218CAC0,0x21932F0)
                edges.append({'site_rva':ins.address-IMAGE_BASE,'target_rva':target,
                              'mnemonic':ins.mnemonic,'covered':covered,
                              'authored_branch_excluded':excluded})
            else:
                slot = (ins.address+ins.size+op.mem.disp-IMAGE_BASE
                        if op.type == capstone.x86.X86_OP_MEM and
                        op.mem.base == capstone.x86.X86_REG_RIP else None)
                indirect.append({'site_rva':ins.address-IMAGE_BASE,'instruction':ins.mnemonic+' '+ins.op_str,
                                 'rip_slot_rva':slot,'region_rva':region.rva})
    return {'linear_decode_is_reachability_proof':False,'direct_and_tail_edges':edges,
            'indirect_edges':indirect}


def cpp_bytes(data):
    return '{'+','.join(f'0x{x:02X}' for x in data)+'}'


def emit_header(contents):
    validate_layout()
    require(len(contents) == len(REGIONS), 'code content count')
    lines = ['#pragma once', '#include <array>', '#include <cstddef>', '#include <cstdint>',
             '#include <span>', '', '// Generated by scripts/p99_wrist_code_manifest.py --emit.',
             '// Exact pinned retail PE; no covered base relocations. STATIC only.',
             '// This is NOT a complete call-graph, data-integrity or live admission proof.',
             '// Installer owns checked reads, hashes, exact relay plans and module/code lifetime.',
             'namespace coop::p99_wrist_code_manifest {',
             f'inline constexpr char kExecutableSha256[] = "{EXE_SHA256}";',
             'inline constexpr bool kTransitiveClosureComplete = false;',
             'inline constexpr bool kSourceAdmission = false;',
             'inline constexpr std::size_t kCoveredBaseRelocations = 0;',
             'struct CodeRegion { std::uintptr_t rva; std::size_t byte_count;',
             '    std::array<std::uint8_t,32> sha256; const char* name; };',
             f'inline constexpr std::array<CodeRegion,{len(REGIONS)}> kRegions{{{{']
    for region,raw in zip(REGIONS,contents):
        require(len(raw) == region.size, 'code content extent')
        canonicalize(region,raw)
        lines.append(f'    {{0x{region.rva:X},0x{region.size:X},{cpp_bytes(hashlib.sha256(raw).digest())},"{region.name}"}},')
    lines.extend(['}};', 'using CallBytes = std::array<std::uint8_t,5>;',
        'struct Callsite { std::uintptr_t rva; CallBytes stock; std::uintptr_t native_target_rva; };',
        'inline constexpr std::array<Callsite,3> kCallsites{{'])
    for rva,stock,target in CALLS:
        lines.append(f'    {{0x{rva:X},{cpp_bytes(stock)},0x{target:X}}},')
    lines.extend(['}};', 'enum class Stage { Stock, Installed };', '',
        '// Mutates ONLY a caller-owned copy, after validating every applicable CALL.',
        '// Installed expectations MUST be the installer\'s exact retained relay plans.',
        '// It does not resolve/admit those targets or read any live pointer.',
        '[[nodiscard]] inline bool canonicalize(std::size_t index, std::span<std::uint8_t> owned,',
        '    Stage stage, std::span<const CallBytes> planned_calls = {}) noexcept {',
        '    if (index >= kRegions.size() || owned.size() != kRegions[index].byte_count) return false;',
        '    if (stage != Stage::Stock && stage != Stage::Installed) return false;',
        '    if (stage == Stage::Stock) { if (!planned_calls.empty()) return false; }',
        '    else {',
        '        if (planned_calls.size() != kCallsites.size()) return false;',
        '        for (const auto& call : planned_calls) if (call[0] != 0xE8) return false;',
        '    }',
        '    const auto& region = kRegions[index];',
        '    for (std::size_t i=0;i<kCallsites.size();++i) {',
        '        const auto& call=kCallsites[i];',
        '        if (call.rva < region.rva || call.rva-region.rva >= region.byte_count) continue;',
        '        const auto offset=call.rva-region.rva;',
        '        if (region.byte_count-offset < call.stock.size()) return false;',
        '        const auto& expected=stage==Stage::Stock ? call.stock : planned_calls[i];',
        '        for (std::size_t j=0;j<expected.size();++j) if (owned[offset+j]!=expected[j]) return false;',
        '    }',
        '    for (const auto& call:kCallsites) {',
        '        if (call.rva < region.rva || call.rva-region.rva >= region.byte_count) continue;',
        '        const auto offset=call.rva-region.rva;',
        '        for (std::size_t j=1;j<call.stock.size();++j) owned[offset+j]=call.stock[j];',
        '    }',
        '    return true;',
        '}',
        '// Ingress LEA1417815B7/JMP141782B3A are checked by group_ingress, not masked here.',
        '// Known boundaries: CRT/QPC/FLS imports; profiler globals; assert/cookie failure;',
        '// wider Async/Process callees; native unwind data; out-of-envelope constant tables.',
        '} // namespace coop::p99_wrist_code_manifest', ''])
    return '\n'.join(lines)


def audit(exe_path=boundary.DEFAULT_EXE, asset_path=ROOT/'reports/actor_packages/nadine-dlc-base.pak'):
    from pe_disassemble_va import Image
    validate_layout()
    branches = branch_inventory(Path(asset_path).read_bytes())
    image = Image(str(exe_path))
    try:
        contents, relocations = read_image_regions(image)
        edges = control_edge_inventory(contents)
    finally:
        image.pe.close()
    report = {'classification':'STATIC_BOUNDED_WRIST_LOADED_CODE_MANIFEST',
        'executable_sha256':EXE_SHA256,'region_count':len(REGIONS),
        'covered_bytes':sum(r.size for r in REGIONS),'covered_base_relocations':0,
        'image_dir64_relocations_checked':relocations,'g0_opcode_count':len(OPCODES),
        'transitive_closure_complete':False,'source_or_runtime_admission':False,
        'regions':[{'rva':r.rva,'size':r.size,'name':r.name,'role':r.role,
                    'sha256':hashlib.sha256(raw).hexdigest()} for r,raw in zip(REGIONS,contents)],
        'branches':branches,'control_edges':edges,'remaining_boundaries':LIMITS}
    return report,emit_header(contents)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=boundary.DEFAULT_EXE)
    parser.add_argument('--asset',type=Path,default=ROOT/'reports/actor_packages/nadine-dlc-base.pak')
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument('--emit',action='store_true')
    mode.add_argument('--check',action='store_true')
    args=parser.parse_args()
    report,header=audit(args.exe,args.asset)
    if args.emit:
        print(header,end='')
    elif args.check:
        require(HEADER.read_text(encoding='utf-8')==header,'generated code header is stale')
        print(f"PASS: {report['region_count']} regions/{report['covered_bytes']} bytes; 15 g0 wrappers; 0 covered relocations; closure/admission FALSE")
    else:
        print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
