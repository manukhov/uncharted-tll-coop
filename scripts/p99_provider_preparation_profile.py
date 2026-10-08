"""Explicit local-owned fixture inputs for the NEW preparation artifact only.

Not a game/remote/target-owner inventory. No auto-discovery, hash acceptance or
API-set schema proof. Reserve one of Owner's32 slots for the artifact itself.
"""
from p99_wrist_import_profile import _read_profile

SCHEMA = 'p99-preparation-owned-import-profile-v1'
SCOPE = 'owned-preparation-loader-fixture-only'
IMPORTER = 'tll_p99_provider_preparation.dll'
MAX_DEPENDENCIES = 31


def read_profile(text):
    return _read_profile(text, schema=SCHEMA, scope=SCOPE,
                         importer=IMPORTER, max_providers=MAX_DEPENDENCIES)
