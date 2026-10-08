#!/usr/bin/env python3
"""Generate a TEST-ONLY native host header from independently pinned disk files.

No loading/process APIs. Synthetic bases exist only to resolve the export graph;
they are never emitted. The native fixture obtains its own full-path references
and constructs the SAME-BUILD typed Request, not a serialized remote ABI.
This is partial metadata/prefix/IAT evidence, not complete dependency admission.
"""
import argparse
import json
from pathlib import Path, PureWindowsPath

from p99_wrist_abi_manifest import digest, require
from p99_wrist_import_profile import _read_profile
from p99_wrist_import_graph import read_provider, Resolver
from p99_provider_evidence import comparison_spans
from p99_provider_preparation_manifest import manifest, ENTRY, OFFER, PROVIDERS
from p99_provider_preparation_profile import IMPORTER

SCHEMA = 'p99-native-preparation-inventory-v1'
SCOPE = 'owned-native-preparation-fixture-only'


def read_profile(text):
    profile = _read_profile(text, schema=SCHEMA, scope=SCOPE,
                            importer=IMPORTER, max_providers=31)
    # Compiled test data has a deliberately narrower path policy than production.
    # No Python CRT profile, arbitrary provider directory or C++ string injection.
    for pin in profile.providers:
        require(pin.path.isascii() and len(pin.path) < 1024 and
                str(PureWindowsPath(pin.path).parent).lower() == r'c:\windows\system32',
                'native fixture requires explicit C:\\Windows\\System32 provider paths')
    return profile


def build(raw, sha256, profile_text, read_file):
    profile = read_profile(profile_text)
    artifact = manifest(raw, sha256)
    selected = tuple(read_provider(read_file(pin.path), pin.sha256, pin.name,
                                  0x100000000 + index*0x10000000)
                     for index, pin in enumerate(profile.providers))
    resolver = Resolver(selected, profile.api_routes)
    indices = {pin.name: i+1 for i, pin in enumerate(profile.providers)}
    entries = {pin.name: set() for pin in profile.providers}
    bindings = []
    for root in artifact['imports']:
        result = resolver.resolve(root['dll'], root['name'], importer=IMPORTER)
        entries[result.provider].add(result.rva)
        bindings.append((root['iat_rva'], indices[result.provider], result.rva))
    require(all(entries.values()), 'selected provider lacks terminal entry evidence')
    require(len(bindings) <= 4096 and len({r[0] for r in bindings}) == len(bindings),
            'native inventory IAT bound/duplicate')
    entry = artifact['preparation_entry']
    pins = [('', artifact['file_sha256'], artifact['size_of_image'], entry['rva'], entry['prefix_hex'])]
    spans = []
    for index, (pin, provider) in enumerate(zip(profile.providers, selected), 1):
        _, expected = comparison_spans(provider, sorted(entries[pin.name]))
        prefixes = [s for s in expected if s.label == 'entry-prefix']
        anchor = prefixes[0]
        pins.append((pin.path, pin.sha256, provider.size, anchor.rva, anchor.expected.hex()))
        spans.extend((index, s.rva, len(s.expected), s.protection, digest(s.expected)) for s in expected)
    return {'pins': pins, 'spans': spans, 'bindings': bindings,
            'offer': artifact['roles'][OFFER]['rva'], 'providers': artifact['roles'][PROVIDERS]['rva'],
            'entry': artifact['roles'][ENTRY]['rva'], 'prefix_count': sum(map(len, entries.values()))}


def render(data):
    def octets(value):
        return '{'+','.join('0x'+value[i:i+2] for i in range(0, len(value), 2))+'}'

    def wide(value):
        return 'L"'+''.join('\\x%04x' % ord(c) for c in value)+'"'

    lines = ['// Generated TEST DATA ONLY. No target ABI, loading or admission.', '#pragma once',
             '#include "nadine_p99_provider_preparation.h"', '#include <string_view>',
             'namespace native_inventory {', 'using Hash=coop::p99_provider::Hash;',
             'struct Pin {std::wstring_view path; Hash hash; std::uint32_t bytes,anchor; Hash prefix;};',
             'struct Span {std::uint32_t provider,rva,size,protect; Hash hash;};',
             'struct Binding {std::uint32_t iat,provider,rva;};',
             f'inline constexpr std::array<Pin,{len(data["pins"])}> pins{{{{']
    lines += [f'  {{{wide(path)},{octets(sha)},{size},{rva},{octets(prefix)}}},'
              for path, sha, size, rva, prefix in data['pins']]
    lines += ['}};', f'inline constexpr std::array<Span,{len(data["spans"])}> spans{{{{']
    lines += [f'  {{{i},{rva},{size},{protect},{octets(sha)}}},' for i, rva, size, protect, sha in data['spans']]
    lines += ['}};', f'inline constexpr std::array<Binding,{len(data["bindings"])}> bindings{{{{']
    lines += [f'  {{{iat},{i},{rva}}},' for iat, i, rva in data['bindings']]
    lines += ['}};', f'inline constexpr std::uint32_t entry={data["entry"]},offer={data["offer"]},providers={data["providers"]};',
              f'inline constexpr unsigned prefix_count={data["prefix_count"]};',
              'static_assert(pins.size()>1 && pins.size()<=coop::p99_provider::kMax);',
              '} // namespace native_inventory', '']
    return '\n'.join(lines)


def bounded_read(path):
    path = Path(path)
    require(0 < path.stat().st_size <= 16*1024*1024, 'native fixture file size')
    raw = path.read_bytes()
    require(0 < len(raw) <= 16*1024*1024, 'native fixture file size changed')
    return raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--provider-profile', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    require(args.provider_profile.stat().st_size <= 65536, 'profile file size')
    data = build(bounded_read(args.dll), args.sha256,
                 args.provider_profile.read_text(encoding='utf-8-sig'), bounded_read)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = args.output_dir / 'nadine_p99_native_inventory_generated.h'
    output.write_text(render(data), encoding='ascii')
    print(json.dumps({'output': str(output), 'selected': len(data['pins']),
        'imports': len(data['bindings']), 'prefixes': data['prefix_count'],
        'provider_bytes': sum(r[2] for r in data['spans']), 'admitted_for_install': False}))


if __name__ == '__main__':
    main()
