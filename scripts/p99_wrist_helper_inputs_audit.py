#!/usr/bin/env python3
"""Pinned STATIC storage-dependency slice for Nadine's group6 wrist helpers.

This is a read-dependency graph, NOT a numerical procedural-pose evaluator.
It preserves read/modify/write edges and stops at group-entry operands or an
explicit unsupported operation. A retained edge need not be numerically active
(zero coefficients, masks and cancellation are not simplified).
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source
import p99_group_program_audit as program
import pak_skin_influence_audit as skin

ROOT = Path(__file__).resolve().parents[1]
TARGETS = (535, 536, 537, 554)
NATIVE = {40: "142189FB0", 41: "14218BC40", 44: "14218C530",
          47: "142192B10/142192D30 (arity3/4 remain opaque and are not reached)",
          49: "14218D570/14218D800", 50: "14218F660/142190910",
          52: "1421897C0", 53: "142194B10", 54: "142189370"}
EXPECTED_INPUTS = {
    535: {(3,x) for x in (*range(0x480,0x48C,4), *range(0x490,0x4AC,4),
                         *range(0x1290,0x12D0,4), 0x1940)},
    536: {(3,x) for x in (*range(0x12D0,0x1310,4),0x1924,0x192C,0x1944,0x1948)},
    537: {(3,x) for x in (*range(0x12D0,0x1310,4),0x1924,0x192C,0x1944,0x1948)},
    554: {(1,x) for x in range(0,64,4)} | {(3,x) for x in
          (*range(0x250,0x260,4),*range(0xED0,0xF00,4),*range(0xF50,0xF90,4),0x1924)},
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def cells(encoded, width):
    """Encoded scalar word addresses, preserving bank and vector alignment."""
    offset = encoded & (-16 if width >= 16 else -4)
    return tuple((encoded & 3, offset+i) for i in range(0, width, 4))


@dataclass(frozen=True)
class Edge:
    ordinal: int
    opcode: int
    destination: tuple[int, int]
    inputs: tuple[tuple[int, int], ...]
    unresolved: bool = False


def decode_batches(data, base, op):
    """Each batch reads a snapshot before its writes (native SIMD four lanes).

    Vector results conservatively retain every component of the operands read
    for that logical output. There is no bind-parent inference in this decoder.
    Op51 is an explicit opaque boundary; the target slice must not reach it.
    """
    p = op.payload
    program.writes(data, base, op)  # Validate outer/nested selector contract.
    desc = base+p[3 if op.opcode == 52 else 2]
    count = p[0]
    batches = []

    def add(outputs, inputs=(), unresolved=False):
        batches.append([Edge(op.ordinal, op.opcode, out, tuple(inputs), unresolved)
                        for out in outputs])

    if op.opcode == 52:
        for destination, width in program.writes(data, base, op):
            add(cells(destination, width))  # Authored constants, no bank read.
    elif op.opcode == 47:
        require(count % 4 == 0 and p[3] in (1, 2, 3, 4), "scalar worker arity")
        if p[3] not in (1,2):
            for dest,width in program.writes(data,base,op):
                add(cells(dest,width),unresolved=True)
            return batches
        for batch in range(count//4):
            edges = []
            for lane in range(4):
                origin, dest = struct.unpack_from("<HH", data, desc+16*batch+4*lane)
                output = cells(dest, 4)[0]
                edges.append(Edge(op.ordinal, op.opcode, output,
                                  cells(origin, 4)+(output,)))
            batches.append(edges)
    elif op.opcode in (41, 44):
        require(count % 4 == 0, "four-lane vector conversion count")
        for batch in range(count//4):
            edges = []
            for lane in range(4):
                words = struct.unpack_from("<8H", data, desc+64*batch+16*lane)
                if op.opcode == 44:
                    inputs = tuple(cells(word, 4)[0] for word in words[5:8])
                    outputs = cells(words[4], 16)
                else:
                    inputs = cells(words[4], 16)
                    outputs = tuple(cells(word, 4)[0] for word in words[5:8]
                                    if word != 0xFFFF)
                edges.extend(Edge(op.ordinal, op.opcode, out, inputs) for out in outputs)
            batches.append(edges)
    elif op.opcode == 40:
        for local, parent in program.propagation_records(data, base, op):
            # S.xyz, Q.xyzw, T.xyz are meaningful; parent is native workspace.
            jp = tuple((0, 48*local+4*i) for i in (*range(3), *range(4, 11)))
            add(cells(64*(local+1)|1, 64), jp+cells(parent, 64))
    elif op.opcode in (53, 54):
        width = 16 if op.opcode == 53 else 4
        for index in range(count):
            dest, origin = struct.unpack_from("<HH", data, desc+4*index)
            for output, inp in zip(cells(dest, width), cells(origin, width)):
                add((output,), (inp,))
    elif op.opcode == 49:
        require(count % 4 == 0, "vector writer count")
        for index in range(count):
            parent, dest = struct.unpack_from("<HH", data, desc+4*index)
            if not parent:
                continue
            inputs = cells(parent, 64)+tuple(cells(x, 4)[0] for x in
                struct.unpack_from("<4H", data, desc+4*count+8*index))
            outputs = cells(dest, 16)
            add(outputs, inputs+(() if parent & 8 else outputs))
        header = desc+12*count
        pair_count, _, batch_count = struct.unpack_from("<III", data, header)
        require(pair_count % 8 == 0 and batch_count == 0, "unreviewed op49 auxiliary phase")
        for index in range(0, pair_count, 8):
            edges = []
            for dest in struct.unpack_from("<8H", data, header+16+2*index):
                vec = cells(dest, 16)
                edges.extend(Edge(op.ordinal, op.opcode, out, vec) for out in vec)
            batches.append(edges)
    elif op.opcode == 50:
        require(count % 4 == 0, "vector composer count")
        for batch in range(count//4):
            edges = []
            for lane in range(4):
                index = 4*batch+lane
                a, b, vector, dest = struct.unpack_from("<4H", data, desc+8*index)
                mode = program.u32(data, desc+24*count+4*index)
                outputs = cells(dest, 16)
                inputs = (cells(a, 48)+cells(b, 64)+cells(vector, 16)+cells(mode, 4)
                          +(() if vector & 8 else outputs))
                edges.extend(Edge(op.ordinal, op.opcode, out, inputs) for out in outputs)
            batches.append(edges)
        aux_count = program.u32(data, desc+28*count)
        addresses = desc+28*count+16
        for index in range(aux_count):
            edges = []
            for dest in struct.unpack_from("<4H", data, addresses+8*index):
                vec = cells(dest, 16)
                edges.extend(Edge(op.ordinal, op.opcode, out, vec) for out in vec)
            batches.append(edges)
    else:
        # Explicit opaque write nodes, never silently pretend an op is readonly.
        for dest, width in program.writes(data, base, op):
            add(cells(dest, width), unresolved=True)
    return batches


class Graph:
    def __init__(self):
        self.state = {}
        self.nodes = []

    def read(self, cell):
        if cell not in self.state:
            self.state[cell] = len(self.nodes)
            self.nodes.append((None, cell, ()))
        return self.state[cell]

    def run(self, batches):
        for batch in batches:
            pending = [(edge, tuple(self.read(inp) for inp in edge.inputs)) for edge in batch]
            for edge, inputs in pending:
                self.state[edge.destination] = len(self.nodes)
                self.nodes.append((edge, edge.destination, inputs))

    def trace(self, outputs):
        stack = [self.read(cell) for cell in outputs]
        visited, leaves, opaque, operators = set(), set(), set(), set()
        while stack:
            node = stack.pop()
            if node in visited:
                continue
            visited.add(node)
            edge, cell, inputs = self.nodes[node]
            if edge is None:
                leaves.add(cell)
            else:
                operators.add((edge.ordinal, edge.opcode))
                if edge.unresolved:
                    opaque.add((edge.ordinal, edge.opcode, cell))
                stack.extend(inputs)
        return leaves, opaque, operators


def group6_graph(data, skeleton):
    raw = skeleton.spec.raw_offset
    group = skeleton.groups[6]
    require((group.first, group.workspace_count, group.parameter_count) == (503, 52, 3),
            "group6 layout")
    desc = raw+program.u32(data, raw+0x2C)+6*32
    table = raw+program.u32(data, raw+0x34)
    require(data[desc+15] == 1 and
            program.u32(data, table+8*program.u16(data, desc+6)+4) == 0xCE70,
            "group6 program identity")
    ops = program.operators(data, raw, 0xCE70)
    require(tuple(op.opcode for op in ops) ==
            (52,49,50,47,47,47,47,41,40,47,54,44,40,51,44,40), "group6 opcode contract")
    graph = Graph()
    for op in ops:
        batches = decode_batches(data, raw+0xCE70, op)
        expected = Counter(cell for dest,width in program.writes(data,raw+0xCE70,op)
                           for cell in cells(dest,width))
        actual = Counter(edge.destination for batch in batches for edge in batch)
        require(actual == expected, f"operator{op.ordinal} destination coverage")
        graph.run(batches)
    return graph


def format_cell(cell):
    return f"{('JP','W','AUX','SHARED')[cell[0]]}+{cell[1]:X}"


def export_origins(data, skeleton):
    """Exact g0/g1 shared copy producers; NOT their full upstream evaluator."""
    result = {}
    raw = skeleton.spec.raw_offset
    for group, relative in ((0, 0x53E0), (1, 0x8D90)):
        for op in program.operators(data, raw, relative):
            if op.opcode not in (53,54):
                continue
            width = 16 if op.opcode == 53 else 4
            desc = raw+relative+op.payload[2]
            for index in range(op.payload[0]):
                dest, origin = struct.unpack_from("<HH", data, desc+4*index)
                for output, inp in zip(cells(dest,width), cells(origin,width)):
                    if output[0] == 3:
                        result[output] = (group, op.ordinal, op.opcode, inp)
    return result


def group_program(data, skeleton, group):
    raw = skeleton.spec.raw_offset
    desc = raw+program.u32(data,raw+0x2C)+group*32
    table = raw+program.u32(data,raw+0x34)
    require(data[desc+15] == 1, "one program per group")
    relative = program.u32(data,table+8*program.u16(data,desc+6)+4)
    return raw+relative, program.operators(data,raw,relative)


def op48_destinations(data, base, op):
    """1421947C0 -> 14218D2D0/14218DA70/14218E880 write inventory only."""
    count, selector, relative = op.payload
    require(selector == 3 and relative & 3 == 0, "op48 selectors")
    desc = base+relative
    out = [(program.u16(data,desc+4*i+2),16) for i in range(count)]
    header = desc+4*count+2*((count+7)&~7)
    pair_count, batch_offset, batch_count = struct.unpack_from("<III",data,header)
    require(pair_count%8 == 0 and batch_count%4 == 0, "op48 padding counts")
    out.extend((program.u16(data,header+16+2*i),16) for i in range(pair_count))
    out.extend((program.u16(data,header+batch_offset+4*i+2),16)
               for i in range(batch_count))
    return out


def prove_shared_survival(data, skeleton, required):
    """Exclude encoded-buffer writes by intervening groups1..5 to exact leaves.

    This does not equate distinct live bank pointers or model custom-wrapper
    owner globals. Custom operators use the established authored destination
    inventory, just as the existing g0/g1 selected-parameter audit does.
    """
    counts = {}
    for group in range(1,6):
        base, ops = group_program(data,skeleton,group)
        writes = [(dest,width) for op in ops for dest,width in
                  (op48_destinations(data,base,op) if op.opcode == 48 else
                   program.writes(data,base,op))]
        touched = {cell for dest,width in writes for cell in cells(dest,width)}
        require(not required & touched, f"group{group} overwrites helper shared input")
        counts[str(group)] = len(writes)
    return counts


def scalar_producers(data, skeleton):
    """Close the five g0 auxiliary weights to Q26 and authored constants.

    These are mathematical operand dependencies only; custom wrappers retain
    their documented QPC/global/FLS side effects and are never executed here.
    """
    base, ops = group_program(data,skeleton,0)
    expected = {0x400:(4,129),0x3E4:(16,128),0x3EC:(18,128),
                0x404:(21,128),0x408:(22,128)}
    result = {}
    for offset,(ordinal,opcode) in expected.items():
        op = ops[ordinal]
        require(op.opcode == opcode and op.payload[0] == 1, "scalar producer identity")
        desc = base+op.payload[2]
        kind,q,_,_,optional,_,_,dest = struct.unpack_from("<8I",data,desc+32)
        require(kind == (0x10A581 if opcode == 128 else 0x10A582) and q == 0x4F0
                and dest == offset|2 and optional == (0x402 if opcode==128 else 0xFFFFFFFF),
                "custom scalar operand contract")
        # Prove last authored encoded-buffer writer, not merely one matching writer.
        later = [o for o in ops[ordinal+1:] if any((2,offset) in cells(e,w)
                    for e,w in program.writes(data,base,o))]
        require(not later, "later g0 scalar overwrite")
        result[f"AUX+{offset:X}"] = {
            "ordinal":ordinal,"opcode":opcode,"operator":f"{skeleton.spec.raw_offset+op.relative:X}",
            "descriptor":f"{desc:X}","quaternion":"JP+4F0 (local Q26)",
            "optional_scalar":"AUX+400" if opcode==128 else None,
            "native":"141A97060" if opcode==128 else "141A951F0"}
    return result


def g0_matrix_inventory(data,skeleton,names):
    base,ops = group_program(data,skeleton,0)
    result = {}
    for joint in (24,26,49,85,86,87,88):
        jp = set(cells(48*joint,48))
        writers = [{"ordinal":o.ordinal,"opcode":o.opcode,
                    "operator":f"{skeleton.spec.raw_offset+o.relative:X}"}
                   for o in ops if any(jp & set(cells(e,w)) for e,w in program.writes(data,base,o))]
        parents = [{"ordinal":o.ordinal,"encoded_parent":f"{parent:04X}"}
                   for o in ops if o.opcode==40
                   for local,parent in program.propagation_records(data,base,o) if local==joint]
        result[str(joint)] = {"name":names[joint][1],"jp_writers":writers,
                              "matrix_parents_including_padding":parents}
    return result


def audit(path):
    pak = skin.PagedPak(Path(path))
    data = pak.data
    skeleton = source.load_skeleton(data, source.NADINE_SKELETON)
    names = skin.read_joint_names(pak, skeleton.spec.joint_table_offset, 580)
    graph = group6_graph(data, skeleton)
    origins = export_origins(data, skeleton)
    required_shared = {x for row in EXPECTED_INPUTS.values() for x in row if x[0]==3}
    survival = prove_shared_survival(data,skeleton,required_shared)
    targets = {}
    for joint in TARGETS:
        leaves, opaque, ops = graph.trace(cells((joint-503+1)*64|1, 64))
        require(not opaque, f"target{joint} reaches an opaque operation: {opaque}")
        require(leaves == EXPECTED_INPUTS[joint], f"target{joint} complete input contract")
        require(all(cell[0]!=3 or cell in origins for cell in leaves), "unmapped shared input")
        mapped = {}
        for cell in sorted(leaves):
            if cell in origins:
                group, ordinal, opcode, inp = origins[cell]
                item = {"group": group, "ordinal": ordinal, "opcode": opcode,
                        "source": format_cell(inp)}
                if inp[0] in (0,1):
                    global_joint = skeleton.groups[group].first+inp[1]//(48 if inp[0]==0 else 64)-(inp[0]==1)
                    item.update(joint=global_joint, name=names[global_joint][1])
                mapped[format_cell(cell)] = item
        targets[str(joint)] = {
            "name": names[joint][1], "entry_read_operands": [format_cell(x) for x in sorted(leaves)],
            "g0_g1_copy_origins": mapped,
            "reached_operators": [list(x) for x in sorted(ops)],
            "opaque_group6_operations": [],
            "group1_shared_finger_export_reached": any(bank==3 and 0x720 <= off < 0xAE0 for bank,off in leaves)}
    return {"classification": "STATIC", "model": "nadine-wrist-helper-read-dependencies-v1",
            "skeleton_sha256": hashlib.sha256(data).hexdigest(), "native_workers": NATIVE,
            "retail_or_game_execution": False, "numeric_helper_replay": False,
            "targets": targets,
            "intervening_group_write_counts":survival,
            "group0_auxiliary_scalar_producers":scalar_producers(data,skeleton),
            "group0_matrix_producer_inventory":g0_matrix_inventory(data,skeleton,names),
            "limits": ["Read-dependency upper bound; zero coefficients/masks/cancellation are not simplified",
                       "No arbitrary callback or live generation/aliasing claim",
                       "Group0 copied operands are named producers, not a completed group0 procedural replay",
                       "Unknown helper baselines do not admit mixed vertices to absolute contact tests"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skeleton", type=Path, default=ROOT/"reports/actor_packages/nadine-dlc-base.pak")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = audit(args.skeleton)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
