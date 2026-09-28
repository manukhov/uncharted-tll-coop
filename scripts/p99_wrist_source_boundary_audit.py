#!/usr/bin/env python3
"""STATIC read-boundary contract for the five missing wrist replay inputs.

This reads disk assets and PE bytes only. It neither captures a game pose nor
admits a hook: ownership, command grammar, validity provenance and lifetime
must accompany any future snapshot. A pointer or 256 readable bytes is not
evidence of those facts. No native function is executed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import p99_wrist_helper_inputs_audit as dependency


ROOT = Path(__file__).resolve().parents[1]
EXE_SHA256 = 'ed1371a4b47961abc160e92aa0b9bdfd787638fa1a39ba2403b423ab45c210be'
DEFAULT_EXE = Path(r'C:\UNCHARTED Legacy of Thieves Collection (2016-2017-2022)\Uncharted Legacy of Thieves Collection\tll.exe')
LOCAL_JOINTS = (24, 26, 49, 51)
PROCESS = 0x141AB97C0
EXECUTOR = 0x14217B630
TABLE_LEA = 0x141AB9B3D
TABLE = 0x143026CB0
CMD16_CALL = 0x141ABA973
CMD16_RETURN = 0x141ABA978
# Exact independently observed retail instructions. In particular TABLE_LEA is
# an address-of-data operation, NOT a call and NOT the cmd16 return boundary.
INSTRUCTIONS = {
    PROCESS: bytes.fromhex('405556574154415541564157488dac2490eaffffb870160000e83227a300482be0'),
    TABLE_LEA: bytes.fromhex('488d056cd15601'),
    0x141ABA96B: bytes.fromhex('488d5580'),       # rdx = stack SegmentContext
    0x141ABA96F: bytes.fromhex('488d4d58'),       # rcx = status destination
    CMD16_CALL: bytes.fromhex('e8b80c6c00'),
    CMD16_RETURN: bytes.fromhex('448b542454'),
    0x141A92E78: bytes.fromhex('e843690200'),     # normal Async g0
    0x141A932F8: bytes.fromhex('e8c3640200'),     # later groups, not g0
    0x14217B64F: bytes.fromhex('4c8b4a28'),       # overwrite incoming r9
    0x14217B66D: bytes.fromhex('440fb74006'),     # overwrite incoming r8
    0x14217B7A5: bytes.fromhex('498bc445892c24'), # return status; store zero
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def direct_call_target(read, address):
    code = read(address, 5)
    require(code is not None and len(code) == 5 and code[0] == 0xE8,
            f'not a complete direct CALL at {address:X}')
    return address + 5 + struct.unpack_from('<i', code, 1)[0]


def verify_boundary_instructions(read):
    """Check actual instruction roles, without trusting a handoff address."""
    require(direct_call_target(read, CMD16_CALL) == EXECUTOR,
            'cmd16 CALL target changed')
    for address in (0x141A92E78, 0x141A932F8):
        require(direct_call_target(read, address) == PROCESS,
                'Async Process call target changed')
    code = read(TABLE_LEA, 7)
    require(code is not None and len(code) == 7 and code[:3] == b'\x48\x8d\x05',
            'custom-table LEA instruction changed')
    require(TABLE_LEA + 7 + struct.unpack_from('<i', code, 3)[0] == TABLE,
            'custom-table LEA target changed')
    for address, expected in INSTRUCTIONS.items():
        require(read(address, len(expected)) == expected,
                f'pinned boundary bytes changed at {address:X}')
    return {
        'custom_table_address_instruction': f'{TABLE_LEA:X}',
        'custom_table_address_is_cmd16_call': False,
        'cmd16_call': f'{CMD16_CALL:X}', 'cmd16_callee': f'{EXECUTOR:X}',
        'post_cmd16_boundary': f'{CMD16_RETURN:X}',
        'normal_async_g0_Process_call': '141A92E78',
        'later_groups_Process_call': '141A932F8',
        'native_input_arguments': ['RCX=status', 'RDX=original SegmentContext'],
        'native_return': 'RAX=original status pointer; *status=0 is not program coverage',
        'incoming_R8_R9_are_not_extra_arguments': True,
        'process_entry_return_slot': {
            'seven_pushes_bytes': 0x38,
            'rbp_below_entry_return_slot': 0x15A8,
            'segment_below_entry_return_slot': 0x1628,
            'status_below_entry_return_slot': 0x1550,
            'slot_must_be_published_independently_before_native_call': True,
            'Segment_plus_8_is_parent_pointer': False,
            'invocation_identity_is_owner_or_lifetime_admission': False,
        },
    }


def prove_post_cmd16_inputs(data):
    """Whole-g0 write inventory, including partial/vector overlaps.

    Four locals survive unchanged, while W22 is created by op40 ordinal36.
    Thus the five values coexist after one admitted execution, not before it.
    This proves an asset-dependent read boundary, NOT live owner/final skin.
    """
    skeleton = dependency.source.load_skeleton(data, dependency.source.NADINE_SKELETON)
    group = skeleton.groups[0]
    require((group.first, group.parameter_count, group.workspace_count) == (0, 66, 104),
            'g0 prefix/workspace layout changed')
    base, ops = dependency.group_program(data, skeleton, 0)
    require(len(ops) == 51, 'g0 program count changed')
    expected = {(0, 48*joint+4*component): ()
                for joint in LOCAL_JOINTS for component in range(12)}
    expected.update({(1, 64*23+4*component): ((36, 40),) for component in range(16)})
    writers = {cell: [] for cell in expected}
    for op in ops:
        writes = {cell for dest, width in dependency.program.writes(data, base, op)
                  for cell in dependency.cells(dest, width)}
        for cell in writes & expected.keys():
            writers[cell].append((op.ordinal, op.opcode))
    for cell, wanted in expected.items():
        require(tuple(writers[cell]) == wanted,
                'source boundary writer changed at '+dependency.format_cell(cell))
    require(ops[36].opcode == 40, 'shoulder producer opcode changed')
    require(sum(joint == 22 for joint, _ in
                dependency.program.propagation_records(data, base, ops[36])) == 1,
            'shoulder workspace producer not unique')
    graph = dependency.Graph()
    for op in ops[:37]:
        graph.run(dependency.decode_batches(data, base, op))
    leaves, opaque, ancestors = graph.trace(dependency.cells(0x5C1, 64))
    shoulder_sources = (0, 14, 15, 16, 17, 18, 20, 22)
    expected_leaves = {(0,48*joint+4*component) for joint in shoulder_sources
                       for component in (*range(3), *range(4,11))}
    expected_leaves.update((1,4*i) for i in range(16))
    require(leaves == expected_leaves and not opaque and
            ancestors == {(ordinal,40) for ordinal in (9,23,28,32,34,36)},
            'shoulder source ancestry changed')
    ranges = [{'operand': f'LOCAL JP{joint}', 'segment_pointer_offset': 0x50,
               'buffer_offset': 48*joint, 'size': 48} for joint in LOCAL_JOINTS]
    ranges.append({'operand': 'shoulder22 workspace', 'segment_pointer_offset': 0x58,
                   'buffer_offset': 64*23, 'size': 64})
    return {
        'unchanged_local_JP_rows': list(LOCAL_JOINTS),
        'shoulder22_only_writer': {'ordinal': 36, 'opcode': 40},
        'shoulder22_source_local_rows': list(shoulder_sources),
        'shoulder22_depends_on_expanded_tail_or_selected_four_JP': False,
        'normal_cmd14_import_rows': 66, 'normal_cmd14_import_bytes': 3168,
        'five_inputs_coexist_after_same_g0_execution': True,
        'pre_cmd16_workspace22_admitted': False,
        'raw_operand_ranges': ranges, 'raw_operand_bytes': sum(r['size'] for r in ranges),
        'raw_bytes_include_owner_validity_or_generation': False,
        'raw_workspace_bytes_are_all_semantic_components': False,
    }


def audit(skeleton_path, exe_path):
    from pe_disassemble_va import Image
    image = Image(str(exe_path))
    try:
        require(hashlib.sha256(image.data).hexdigest() == EXE_SHA256,
                'retail executable SHA-256 changed')
        instructions = verify_boundary_instructions(image.read)
    finally:
        image.pe.close()
    operands = prove_post_cmd16_inputs(Path(skeleton_path).read_bytes())
    return {
        'classification': 'STATIC_POST_CMD16_WRIST_SOURCE_BOUNDARY',
        'executable_sha256': EXE_SHA256,
        'skeleton_sha256': dependency.source.NADINE_SKELETON.entry_sha256,
        'instructions': instructions, 'operands': operands,
        'retail_or_game_execution': False, 'runtime_writes': False,
        'actual_pose_captured': False, 'source_or_runtime_admission': False,
        'final_render_palette_proven': False, 'full_skin_contact_admitted': False,
        'required_external_provenance': [
            'retained local Nadine owner and selected P99 generation',
            'normal Async mode2 g0 at 141A92E78, not Perform/profile/other groups',
            'actual command grammar and final cmd16, not optional preliminary cmd16',
            'one SegmentContext invocation and current target skeleton/group descriptors',
            'completed prefix import/finalization; pre-finalize ValidBits needed only to claim fallback/producer provenance',
            'copy before subsequent publication/cleanup; never retain workspace pointers',
            'separate final render/weapon coordinate-frame correspondence',
        ],
        'lifetime_and_validity': {
            'finalize_call': '141AB9D18 -> 141AB78F0',
            'missing_mask': '141AB7993/799F invert valid; 141AB79A7 count mask',
            'fill_and_valid_update': '141AB79D7 fill; 141AB79DC/79E6 OR missing into valid',
            'publish_JP': '141AB9DD6 -> Segment+50',
            'normal_import_supersedes_cmd3_validity': 'cmd14 141ABA7F5 -> 14217B310 imports external Fg+100 prefix66',
            'post16_next_commands_may_write': 'cmd17 141ABAA12 CopyEncodedScalars',
            'workspace_release_and_null': 'cmd5 141ABB107/141ABB10C',
            'JP_release_and_null': 'cmd5 141ABB120/141ABB125',
        },
        'later_finality_counterexamples': {
            'postpose_JP_writer': '141937D20 id03D27700: JP load141937F82; T/Q/S stores141937FCC/FE8/141938006',
            'late_CPU_palette_override': '143682F4A != 0 && Fg+230 != 0 && !(Fg.flags & 0x200):141A935C0..5E0',
            'render_palette_selection': '14181AB5A..AB80 chooses Fg+250 when nonnull, otherwise Fg+240',
            'all_later_paths_excluded_for_an_actual_frame': False,
        },
        'limits': [
            'Compiled read-only adapters are uninstalled; live capture admission and payload export remain disabled',
            'A repeated pointer or equal source values cannot establish same generation',
            'The pre-stock Async source observer does not produce these post-cmd16 inputs',
            'A 256-byte operand block alone is insufficient; no default pose is substituted',
            'Post-cmd16 is a procedural source boundary, not by itself final rendered skin',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skeleton', type=Path, default=ROOT/'reports/actor_packages/nadine-dlc-base.pak')
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(args.skeleton, args.exe)
    encoded = json.dumps(result, indent=2, allow_nan=False)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding='utf-8')
    print(encoded, end='')


if __name__ == '__main__':
    main()
