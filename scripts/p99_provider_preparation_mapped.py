"""Strict ten-export preparation artifact comparison plan; never admission.

Shares only transformations/transport with the older inert ABI plan. Both disk
manifest and metadata reads use the NEW contract; no guessing, global policy
mutation or fallback. Independent selected import addresses remain caller input.
The two mutable publication pages are excluded, not verified by this plan.
"""
import p99_provider_preparation_manifest as disk
import p99_wrist_mapped_image as mapped


class _MetadataReads(mapped._MetadataReads):
    def exports(self):
        return disk.Image.exports(self)


def build_plan(raw, trusted_sha256, base, bindings, *, header_base=mapped.HeaderBase.PREFERRED):
    return mapped._build_plan(raw, trusted_sha256, base, bindings,
        header_base=header_base, manifest_reader=disk.manifest, metadata_reader=_MetadataReads)
