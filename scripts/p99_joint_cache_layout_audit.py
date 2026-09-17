#!/usr/bin/env python3
"""Prove P99 target storage domains from the exact C39 asset, without the game.

Native contract: 141686DF5..E28 allocates 48*u16(ArtItemSkeleton+3C).
The serialized loader begins at ArtItemSkeleton+20.  Its +1C is the
allocation count; it is not the workspace count or all packed parameters.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source


def audit(path: Path) -> None:
    data = path.read_bytes()
    skeleton = source.load_skeleton(data, source.NADINE_SKELETON)
    loader = skeleton.spec.database_signature_offset - 4
    group_count, joint_count, world_count, local_count = struct.unpack_from(
        "<IIII", data, loader + 0x10
    )
    if (group_count, joint_count, world_count, local_count) != (8, 580, 104, 66):
        raise ValueError("C39 serialized allocation counts changed")
    if local_count != skeleton.groups[0].parameter_count:
        raise ValueError("persistent allocation no longer matches group 0")
    packed_count = max(
        group.parameter_count + skeleton.processing[group.processing_index].first_parameter
        for group in skeleton.groups if group.parameter_count
    )
    if packed_count != 426:
        raise ValueError("C39 reference-pose extent changed")

    names = ["r_palm", "r_hand_prop_attachment"]
    for finger, parts in (
        ("pinky", ("a", "b", "c", "d")),
        ("ring", ("_metacarpal", "a", "b", "c")),
        ("middle", ("_metacarpal", "a", "b", "c")),
        ("index", ("_metacarpal", "a", "b", "c")),
        ("thumb", ("a", "b", "c")),
    ):
        names.extend("r_" + finger + part for part in parts)
    string_id = source.native.load_audit_module().string_id
    print(f"asset_sha256={source.sha256(data)}")
    print(f"loader=0x{loader:X} local_rows={local_count} local_bytes={48*local_count} "
          f"world_rows={world_count} packed_reference_rows={packed_count} "
          f"workspace_joints={joint_count}")
    print("name global group local packed_reference JointCache_access")
    rejected = []
    for name in names:
        semantic = string_id(name.encode("ascii"))
        matches = [i for i, value in enumerate(skeleton.joint_ids) if value == semantic]
        if len(matches) != 1:
            raise ValueError(f"{name} is not a unique semantic joint")
        global_joint = matches[0]
        group_index, local = skeleton.location(global_joint)
        group = skeleton.groups[group_index]
        processing = skeleton.processing[group.processing_index]
        if local >= group.parameter_count:
            raise ValueError(f"{name} has no local parameter row")
        packed = processing.first_parameter + local
        fits = group_index == 0 and global_joint < local_count
        if not fits:
            rejected.append(global_joint)
            if packed < local_count:
                raise ValueError("unexpected finger packed index in persistent bank")
        print(name, global_joint, group_index, local, packed,
              "in-range" if fits else "OUTSIDE-ALLOCATION")
    if rejected != list(range(105, 142, 2)):
        raise ValueError(f"unexpected rejected targets: {rejected}")
    print(f"AGL rejected: {len(rejected)} finger rows / {48*len(rejected)} bytes "
          f"written at offsets {48*min(rejected)}..{48*(max(rejected)+1)-1}; "
          f"allocation ends at {48*local_count-1}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--asset", type=Path,
        default=source.ROOT / "reports/actor_packages/nadine-dlc-base.pak",
    )
    audit(parser.parse_args().asset)


if __name__ == "__main__":
    main()
