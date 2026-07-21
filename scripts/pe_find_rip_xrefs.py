import bisect
import json
import struct
import sys


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def s32(data, offset):
    return struct.unpack_from("<i", data, offset)[0]


def parse_pe(data):
    pe_offset = u32(data, 0x3C)
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError("Not a PE file")

    coff = pe_offset + 4
    section_count = u16(data, coff + 2)
    optional_size = u16(data, coff + 16)
    optional = coff + 20
    image_base = struct.unpack_from("<Q", data, optional + 24)[0]
    sections_offset = optional + optional_size

    sections = []
    for index in range(section_count):
        offset = sections_offset + index * 40
        name = data[offset:offset + 8].rstrip(b"\0").decode("ascii")
        virtual_size = u32(data, offset + 8)
        virtual_address = u32(data, offset + 12)
        raw_size = u32(data, offset + 16)
        raw_offset = u32(data, offset + 20)
        sections.append(
            {
                "name": name,
                "virtual_size": virtual_size,
                "virtual_address": virtual_address,
                "raw_size": raw_size,
                "raw_offset": raw_offset,
            }
        )

    return image_base, sections


def section_by_name(sections, name):
    return next(section for section in sections if section["name"] == name)


def parse_runtime_functions(data, pdata):
    functions = []
    start = pdata["raw_offset"]
    end = start + pdata["raw_size"]

    for offset in range(start, end - 11, 12):
        begin_rva, end_rva, unwind_rva = struct.unpack_from("<III", data, offset)
        if begin_rva == 0 or end_rva <= begin_rva:
            continue
        functions.append((begin_rva, end_rva, unwind_rva))

    functions.sort()
    return functions


def find_function(functions, rva):
    starts = [function[0] for function in functions]
    index = bisect.bisect_right(starts, rva) - 1
    if index < 0:
        return None

    function = functions[index]
    return function if function[0] <= rva < function[1] else None


def main():
    executable_path, targets_path, output_path = sys.argv[1:4]
    data = open(executable_path, "rb").read()
    targets = json.load(open(targets_path, "r", encoding="utf-8"))

    image_base, sections = parse_pe(data)
    text = section_by_name(sections, ".text")
    pdata = section_by_name(sections, ".pdata")
    runtime_functions = parse_runtime_functions(data, pdata)

    text_data = data[text["raw_offset"]:text["raw_offset"] + text["raw_size"]]
    results = {}
    target_lookup = {}

    for target_name, matches in targets["targets"].items():
        results[target_name] = []
        for match in matches:
            result = {
                "string_va": match["address"],
                "value": match["value"],
                "xrefs": [],
            }
            results[target_name].append(result)
            target_lookup.setdefault(int(match["address"], 16), []).append(result)

    starts = [function[0] for function in runtime_functions]
    for displacement_offset in range(0, len(text_data) - 4):
        displacement = s32(text_data, displacement_offset)
        displacement_end_rva = text["virtual_address"] + displacement_offset + 4
        target_va = image_base + displacement_end_rva + displacement
        target_results = target_lookup.get(target_va)
        if target_results is None:
            continue

        reference_rva = text["virtual_address"] + displacement_offset
        function_index = bisect.bisect_right(starts, reference_rva) - 1
        function = (
            runtime_functions[function_index] if function_index >= 0 else None
        )
        if function and not function[0] <= reference_rva < function[1]:
            function = None

        context_start = max(0, displacement_offset - 8)
        context_end = min(len(text_data), displacement_offset + 12)
        xref = {
            "displacement_va": f"0x{image_base + reference_rva:X}",
            "function_va": f"0x{image_base + function[0]:X}" if function else None,
            "function_end_va": (
                f"0x{image_base + function[1]:X}" if function else None
            ),
            "bytes": text_data[context_start:context_end].hex(" "),
        }
        for target_result in target_results:
            target_result["xrefs"].append(xref)

    with open(output_path, "w", encoding="utf-8") as output:
        json.dump(
            {
                "image_base": f"0x{image_base:X}",
                "targets": results,
            },
            output,
            indent=2,
        )


if __name__ == "__main__":
    main()
