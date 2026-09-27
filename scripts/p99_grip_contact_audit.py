#!/usr/bin/env python3
"""Pure P99 lower-grip patch and explicitly conditional surface-distance audit.

The author-defined y<=0 patch excludes the trigger guard/upper receiver. It is
NOT an engine contact tag, the whole grip, a closed collision solid or a live
weapon/hand alignment. Distances are unsigned and cannot rank grip quality.
Retail pose/game code is never mapped or called. Optional preview uses Pillow.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct

import p99_hand_surface_delta_audit as surface
import p99_weapon_mesh_audit as weapon

MODEL = "conditional-p99-lower-grip-distance-v1"
ALIGNMENT = "ASSUME_WEAPON_BIND_ROOT_EQUALS_MODELED_RIGHT_PROP"
FINGERS = {"pinky": (105, 107, 109, 111), "ring": (113, 115, 117, 119),
           "middle": (121, 123, 125, 127), "index": (129, 131, 133, 135),
           "thumb": (137, 139, 141)}


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def finite_point(point):
    point = tuple(float(x) for x in point)
    if len(point) != 3 or not all(math.isfinite(x) for x in point):
        raise ValueError("expected finite xyz point")
    return point


def segment_point(p, a, b):
    ab = sub(b, a)
    length2 = dot(ab, ab)
    t = min(1., max(0., dot(sub(p, a), ab)/length2)) if length2 else 0.
    return tuple(a[i]+t*ab[i] for i in range(3))


def triangle_point(p, a, b, c):
    """Closest point: orthogonal face projection if inside, else three edges.

    Degenerate triangles are treated as their segments, not fictitious planes.
    Inputs are validated by the index; this internal primitive assumes finite xyz.
    """
    ab, ac = sub(b, a), sub(c, a)
    normal = cross(ab, ac)
    n2 = dot(normal, normal)
    if n2:
        ap = sub(p, a)
        u = dot(cross(ap, ac), normal)/n2
        v = dot(cross(ab, ap), normal)/n2
        if u >= 0. and v >= 0. and u+v <= 1.:
            return tuple(a[i]+u*ab[i]+v*ac[i] for i in range(3))
    candidates = (segment_point(p, a, b), segment_point(p, b, c), segment_point(p, c, a))
    return min(candidates, key=lambda q: dot(sub(p, q), sub(p, q)))


def aabb(points):
    return tuple((min(p[i] for p in points), max(p[i] for p in points)) for i in range(3))


def box_distance2(p, bounds):
    return sum(max(lo-p[i], 0., p[i]-hi)**2 for i, (lo, hi) in enumerate(bounds))


def boxes_overlap(a, b):
    return all(lo1 <= hi2 and lo2 <= hi1 for (lo1, hi1), (lo2, hi2) in zip(a, b))


def strict_segment_crossing(p, q, triangle, plane_epsilon=1e-9, bary_epsilon=1e-9):
    """Only a transversal, face-interior crossing; boundary/coplanar undecided.

    Epsilon is in authored asset units (plane) or dimensionless (barycentric).
    None is NOT proof of disjointness or absence of contact/penetration.
    """
    a, b, c = triangle
    ab, ac = sub(b, a), sub(c, a)
    n = cross(ab, ac)
    n2 = dot(n, n)
    if n2 == 0.:
        return None
    length = math.sqrt(n2)
    hp, hq = dot(sub(p, a), n)/length, dot(sub(q, a), n)/length
    if not ((hp > plane_epsilon and hq < -plane_epsilon) or
            (hq > plane_epsilon and hp < -plane_epsilon)):
        return None
    t = hp/(hp-hq)
    hit = tuple(p[i]+t*(q[i]-p[i]) for i in range(3))
    ah = sub(hit, a)
    u, v = dot(cross(ah, ac), n)/n2, dot(cross(ab, ah), n)/n2
    return hit if u > bary_epsilon and v > bary_epsilon and u+v < 1.-bary_epsilon else None


def strict_triangle_crossing(a, b):
    for moving, fixed in ((a, b), (b, a)):
        for p, q in zip(moving, moving[1:]+moving[:1]):
            hit = strict_segment_crossing(p, q, fixed)
            if hit is not None:
                return hit
    return None


class TriangleIndex:
    """Deterministic AABB hierarchy; results use original triangle ordinals."""
    def __init__(self, vertices, triangles, selected=None):
        self.vertices = tuple(finite_point(p) for p in vertices)
        self.triangles = tuple(tuple(t) for t in triangles)
        if not self.vertices or not self.triangles:
            raise ValueError("empty triangle mesh")
        if any(len(t) != 3 or any(type(i) is not int or not 0 <= i < len(self.vertices)
                                  for i in t) for t in self.triangles):
            raise ValueError("invalid triangle indices")
        ids = tuple(range(len(triangles))) if selected is None else tuple(selected)
        if (not ids or len(set(ids)) != len(ids) or
                any(type(i) is not int or not 0 <= i < len(triangles) for i in ids)):
            raise ValueError("invalid triangle selection")
        self.bounds = {i: aabb([self.vertices[j] for j in self.triangles[i]]) for i in ids}
        self.root = self._build(ids)

    def _build(self, ids):
        bounds = tuple((min(self.bounds[i][a][0] for i in ids),
                        max(self.bounds[i][a][1] for i in ids)) for a in range(3))
        if len(ids) <= 8:
            return bounds, tuple(sorted(ids)), None
        axis = max(range(3), key=lambda a: bounds[a][1]-bounds[a][0])
        ids = sorted(ids, key=lambda i: (sum(self.bounds[i][axis]), i))
        mid = len(ids)//2
        return bounds, None, (self._build(ids[:mid]), self._build(ids[mid:]))

    def nearest(self, point):
        p = finite_point(point)
        best = (math.inf, len(self.triangles), None)
        stack = [self.root]
        while stack:
            bounds, ids, children = stack.pop()
            if box_distance2(p, bounds) > best[0]:
                continue
            if children:
                stack.extend(sorted(children, key=lambda node: box_distance2(p, node[0]),
                                    reverse=True))
                continue
            for i in ids:
                q = triangle_point(p, *(self.vertices[j] for j in self.triangles[i]))
                delta = sub(p, q)
                candidate = dot(delta, delta), i, q
                if candidate[:2] < best[:2]:
                    best = candidate
        return best

    def overlapping(self, bounds):
        stack = [self.root]
        while stack:
            node_bounds, ids, children = stack.pop()
            if not boxes_overlap(bounds, node_bounds):
                continue
            if children:
                stack.extend(children)
            else:
                yield from (i for i in ids if boxes_overlap(bounds, self.bounds[i]))


def topology(vertices, triangles):
    """Exact-position weld only: no tolerance silently seals cracks or seams."""
    welded, mapping = {}, []
    for point in vertices:
        key = finite_point(point)
        welded.setdefault(key, len(welded))
        mapping.append(welded[key])
    edges, directed = Counter(), Counter()
    parent = list(range(len(welded)))
    used = set()
    degenerate = 0

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for tri in triangles:
        ids = tuple(mapping[i] for i in tri)
        points = [vertices[i] for i in tri]
        n = cross(sub(points[1], points[0]), sub(points[2], points[0]))
        degenerate += dot(n, n) == 0.
        used.update(ids)
        for a, b in zip(ids, ids[1:]+ids[:1]):
            edges[tuple(sorted((a, b)))] += 1
            directed[(a, b)] += 1
            parent[root(b)] = root(a)
    return {"position_weld": "exact xyz; no epsilon", "welded_used_vertices": len(used),
            "connected_components": len({root(i) for i in used}),
            "boundary_edges": sum(n == 1 for n in edges.values()),
            "nonmanifold_edges": sum(n > 2 for n in edges.values()),
            "same_direction_pairs": sum(n == 2 and directed[edge] != 1
                                         for edge, n in edges.items()),
            "degenerate_triangles": degenerate,
            "solid_interior_proven": False}


def lower_grip_patch(vertices, triangles):
    # Chosen from orthographic geometry, independent of either modeled hand pose.
    # All vertices must satisfy the plane. No centroid rule or synthetic cap.
    selected = [i for i, tri in enumerate(triangles) if all(vertices[j][1] <= 0. for j in tri)]
    crossing = [i for i, tri in enumerate(triangles)
                if min(vertices[j][1] for j in tri) <= 0. < max(vertices[j][1] for j in tri)]
    if not selected:
        raise ValueError("empty lower-grip patch")
    return selected, {"name": "manual lower-grip-only surface patch",
        "rule": "all three source vertices have weapon-bind-root y <= 0",
        "plane_asset_units": 0., "engine_semantic_region": False,
        "full_grip_coverage": False, "synthetic_cap_added": False,
        "triangle_count": len(selected), "excluded_plane_crossing_triangles": len(crossing),
        "source_triangle_ids_sha256": hashlib.sha256(struct.pack(f"<{len(selected)}I", *selected)).hexdigest(),
        "bounds": aabb([vertices[j] for i in selected for j in triangles[i]]),
        "topology": topology(vertices, [triangles[i] for i in selected])}


def modeled_points(report, body_path, skeleton_path):
    # Reuse all typed geometry/decoder/source/skin-support admission checks.
    admission = surface.audit(report, body_path, skeleton_path)
    direct, proposed = surface.report_frames(report)
    body = surface.skin.PagedPak(Path(body_path))
    skeleton = surface.skin.PagedPak(Path(skeleton_path))
    _, inverse, _ = surface.locator.load_basis(skeleton.data, surface.locator.source.NADINE_SKELETON)
    inverse = {j: inverse[j] for j in surface.JOINTS}
    mesh = next(m for m in surface.skin.find_mesh_headers(body, 580) if m.absolute == surface.MESH_OFFSET)
    influences = tuple(surface.skin.iter_vertex_influences(body, mesh, 580))
    samples = []
    for i, (p, weights) in enumerate(zip(surface.read_positions(body, mesh), influences)):
        support = {w.joint for w in weights if w.weight_quantized}
        if not support or not support <= set(surface.JOINTS):
            continue
        groups = [name for name, joints in FINGERS.items()
                  if support & set(joints) and support <= set(joints) | {28, 30}]
        samples.append({"vertex": i, "finger_support_group": groups[0] if len(groups) == 1 else "mixed_or_palm",
                        "positive_joints": sorted(support),
                        "direct": surface.skin_point(p, weights, direct, inverse),
                        "proposed": surface.skin_point(p, weights, proposed, inverse)})
    if len(samples) != admission["closed_vertices"]:
        raise ValueError("surface materialization does not match admitted subset")
    # 1418789AB/9BD read native +28 count/+48 source; 1418789D4/A2E/A86
    # allocate/upload 2*count, A9E stride2, AA6 format57=DXGI_FORMAT_R16_UINT.
    # Exact ownership is
    # pinned by the body digest plus header+50 pointer and all indices in range.
    index_base = body.resolve_absolute(mesh.absolute+0x50)
    if index_base != 0x5C2FA4 or mesh.index_count != 36582:
        raise ValueError("body arms index ownership changed")
    surface.skin._require_page_span(body, index_base, mesh.index_count*2)
    indices = struct.unpack_from(f"<{mesh.index_count}H", body.data, index_base)
    if max(indices) >= mesh.vertex_count or mesh.index_count % 3:
        raise ValueError("body arms index range changed")
    triangles = [tuple(indices[i:i+3]) for i in range(0, len(indices), 3)]
    return samples, admission, triangles


def compare_crossings(samples, body_triangles, index, *, alignment_assumption):
    if alignment_assumption != ALIGNMENT:
        raise ValueError("explicit unverified identity alignment assumption required")
    rows = {r["vertex"]: r for r in samples}
    closed = [(i, t) for i, t in enumerate(body_triangles) if all(v in rows for v in t)]
    result = {"closed_hand_triangles": len(closed), "all_body_triangles": len(body_triangles),
              "plane_epsilon_asset_units": 1e-9, "barycentric_epsilon": 1e-9,
              "method": "opposite-side segment crossing strictly inside other triangle",
              "completeness": "lower bound only; coplanar/boundary/tiny crossings unresolved",
              "solid_penetration_depth_computed": False, "live_surface_intersection_proven": False}
    for pose in ("direct", "proposed"):
        intersections = []
        for source_id, tri in closed:
            points = tuple(rows[i][pose] for i in tri)
            for weapon_id in sorted(index.overlapping(aabb(points))):
                weapon_tri = tuple(index.vertices[j] for j in index.triangles[weapon_id])
                hit = strict_triangle_crossing(points, weapon_tri)
                if hit is not None:
                    groups = {rows[i]["finger_support_group"] for i in tri}
                    intersections.append({"body_triangle": source_id, "weapon_triangle": weapon_id,
                                          "point": hit, "finger_support_group": next(iter(groups))
                                          if len(groups) == 1 else "mixed_or_palm"})
        result[pose] = {"crossing_pairs": len(intersections),
                        "hand_triangles": len({r["body_triangle"] for r in intersections}),
                        "weapon_triangles": len({r["weapon_triangle"] for r in intersections}),
                        "pairs_by_finger_support_group": dict(sorted(Counter(
                            r["finger_support_group"] for r in intersections).items())),
                        "witnesses": intersections}
    return result


def compare_distances(samples, index, *, alignment_assumption):
    if alignment_assumption != ALIGNMENT:
        raise ValueError("explicit unverified identity alignment assumption required")
    if not samples:
        raise ValueError("empty modeled surface")
    by_finger = defaultdict(lambda: {"direct": [], "proposed": []})
    output = []
    for sample in samples:
        row = dict(sample)
        for pose in ("direct", "proposed"):
            squared, tri, nearest = index.nearest(sample[pose])
            distance = math.sqrt(squared)
            row[pose+"_nearest"] = {"unsigned_distance": distance, "source_triangle": tri,
                                      "point": nearest}
            by_finger[sample["finger_support_group"]][pose].append(distance)
        output.append(row)
    return {"samples": output, "per_finger_support_group": {
                finger: {pose: surface.summarize(values) for pose, values in poses.items()}
                for finger, poses in sorted(by_finger.items())},
            "all_supported_vertices": {pose: surface.summarize([r[pose+"_nearest"]["unsigned_distance"]
                                                              for r in output]) for pose in ("direct", "proposed")},
            "preferred_pose": None, "signed_penetration_computed": False,
            "skin_contact_or_improvement_proven": False}


def audit(p99, pistol, pose_report=None, *, body_path=None, skeleton_path=None,
          alignment_assumption=None):
    asset = weapon.audit_assets(p99, pistol, include_geometry=True)
    vertices, triangles = asset["vertices_asset_units"], asset["triangles"]
    ids, patch = lower_grip_patch(vertices, triangles)
    result = {"model": MODEL, "status": "STATIC_GEOMETRY_WITH_OPTIONAL_CONDITIONAL_DISTANCES",
        "asset_sha256": weapon.P99_SHA256, "root_asset_sha256": weapon.PISTOL_SHA256,
        "retail_or_game_execution": False, "runtime_writes": False,
        "live_frame_proven": False, "skin_contact_or_improvement_proven": False,
        "main_mesh": {"triangles": len(triangles), "topology": topology(vertices, triangles)},
        "lower_grip_patch": patch,
        "limits": ["ROI is a manually defined LOWER grip patch, not the entire grip or an engine contact tag",
                   "Unsigned point-to-surface distances are not penetration or grasp quality",
                   "Shared weapon-root/rightProp frame is assumed, not live verified",
                   "Modeled positions are scalar approximations; no world affine weight-sum cancellation claim",
                   "Mixed/helper vertices and most palm surface are not in the 21-joint closed subset",
                   "Index/upper thumb are expected outside this lower patch and must not be scored as a bad grip",
                   "No corrected runtime pose or left support-arm result is provided"]}
    if pose_report is not None:
        if alignment_assumption != ALIGNMENT:
            raise ValueError("explicit unverified identity alignment assumption required")
        samples, admission, body_triangles = modeled_points(pose_report, body_path, skeleton_path)
        result["alignment_assumption"] = ALIGNMENT
        result["source_report_sha256"] = admission["source_report_canonical_sha256"]
        result["closed_vertex_indices_sha256"] = admission["closed_vertex_indices_sha256"]
        index = TriangleIndex(vertices, triangles, ids)
        result["comparison"] = compare_distances(samples, index,
                                                alignment_assumption=alignment_assumption)
        result["conditional_surface_crossings"] = compare_crossings(samples, body_triangles, index,
                                                                    alignment_assumption=alignment_assumption)
    return result


def preview(result, vertices, triangles, output):
    """Scientific orthographic diagram, NOT an edited game image/render oracle."""
    from PIL import Image, ImageDraw
    image = Image.new("RGB", (1500, 1000), "white")
    draw = ImageDraw.Draw(image)
    selected, _ = lower_grip_patch(vertices, triangles)
    selected = set(selected)
    samples = result.get("comparison", {}).get("samples", [])
    for row, pose in enumerate(("direct", "proposed")):
        for pane, (a, b) in enumerate(((2, 1), (0, 1), (2, 0))):
            def xy(p):
                return pane*500+175+p[a]*1750, row*500+230-p[b]*1750
            for i, tri in enumerate(triangles):
                points = [xy(vertices[j]) for j in tri]
                draw.line(points+[points[0]], fill="#6d7980" if i in selected else "#d7dce0")
            for sample in samples:
                x, y = xy(sample[pose])
                draw.ellipse((x-1, y-1, x+1, y+1), fill="#047da3" if row == 0 else "#ce6819")
            draw.text((pane*500+14, row*500+18), f"{pose}  {'xyz'[a]}/{'xyz'[b]}", fill="black")
            draw.text((pane*500+14, row*500+405), "Dark: manual lower grip, y <= 0. Dots: supported vertices.", fill="black")
            draw.text((pane*500+14, row*500+423), "Identity root/prop ASSUMED. No signed penetration or pose ranking.", fill="black")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=weapon.DEFAULT_ARCHIVE)
    parser.add_argument("--pose-report", type=Path)
    parser.add_argument("--assume-identity-weapon-prop", action="store_true")
    parser.add_argument("--body", type=Path, default=surface.ROOT/"reports/actor_packages/nadine-rural-body.pak")
    parser.add_argument("--skeleton", type=Path, default=surface.ROOT/"reports/actor_packages/nadine-dlc-base.pak")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    p99, pistol = weapon.load_assets(args.archive)
    pose = json.loads(args.pose_report.read_text(encoding="utf-8")) if args.pose_report else None
    result = audit(p99, pistol, pose, body_path=args.body, skeleton_path=args.skeleton,
                   alignment_assumption=ALIGNMENT if args.assume_identity_weapon_prop else None)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    if args.preview:
        geometry = weapon.audit_assets(p99, pistol, include_geometry=True)
        preview(result, geometry["vertices_asset_units"], geometry["triangles"], args.preview)
    # Never flood console/context with all vertex records; full output is local.
    if "comparison" in result:
        result["comparison"].pop("samples")
        for pose in ("direct", "proposed"):
            result["conditional_surface_crossings"][pose].pop("witnesses")
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
