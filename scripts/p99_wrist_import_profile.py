"""Explicit, externally reviewed inputs for the OWNED Python fixture only.

No automatic discovery, hash acceptance, schema extraction or remote reuse.
Hashes identify selected bytes, not Authenticode trust or loaded-image identity.
"""
from dataclasses import dataclass
import json
from pathlib import PureWindowsPath
import re

from p99_wrist_abi_manifest import require
from p99_wrist_import_graph import ApiRoute, api_contract, module_name

SCHEMA = 'p99-owned-import-profile-v1'
IMPORTER = 'tll_p99_wrist_abi.dll'


@dataclass(frozen=True)
class FilePin:
    name: str
    path: str
    sha256: str


@dataclass(frozen=True)
class Profile:
    providers: tuple[FilePin, ...]
    api_routes: tuple[ApiRoute, ...]


def read_profile(text):
    require(isinstance(text, str) and len(text) <= 65536, 'profile text bound')

    def unique(rows):
        result = {}
        for key, value in rows:
            require(key not in result, 'duplicate JSON key')
            result[key] = value
        return result

    doc = json.loads(text, object_pairs_hook=unique)
    require(isinstance(doc, dict) and set(doc) == {'schema', 'scope', 'providers', 'api_routes'} and
            doc['schema'] == SCHEMA and doc['scope'] == 'owned-python-fixture-only', 'profile schema/scope')
    require(isinstance(doc['providers'], list) and 0 < len(doc['providers']) <= 32 and
            isinstance(doc['api_routes'], list) and len(doc['api_routes']) <= 128, 'profile rows')
    providers, names, paths = [], set(), set()
    for row in doc['providers']:
        require(isinstance(row, dict) and set(row) == {'name', 'path', 'sha256'}, 'provider profile fields')
        name = module_name(row['name'])
        require(not api_contract(name) and name not in names and name != IMPORTER, 'provider profile name')
        require(isinstance(row['path'], str) and 0 < len(row['path']) < 32768 and
                not any(ord(c) < 32 for c in row['path']), 'provider profile path')
        path = PureWindowsPath(row['path'])
        require(path.is_absolute() and re.fullmatch('[A-Za-z]:', path.drive) and
                '..' not in path.parts and path.name.lower() == name and
                ':' not in str(path)[2:] and str(path).lower() not in paths,
                'explicit absolute provider file path required')
        sha = row['sha256']
        require(isinstance(sha, str) and re.fullmatch('[0-9a-fA-F]{64}', sha), 'profile SHA256')
        providers.append(FilePin(name, str(path), sha.lower()))
        names.add(name)
        paths.add(str(path).lower())
    routes, keys = [], set()
    for row in doc['api_routes']:
        require(isinstance(row, dict) and set(row) == {'importer', 'contract', 'host'}, 'route profile fields')
        importer, contract, host = map(module_name, (row['importer'], row['contract'], row['host']))
        require(importer in names | {IMPORTER} and api_contract(contract) and host in names and
                (importer, contract) not in keys, 'route profile identity/duplicate')
        routes.append(ApiRoute(importer, contract, host))
        keys.add((importer, contract))
    return Profile(tuple(providers), tuple(routes))
