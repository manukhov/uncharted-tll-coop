import struct
import sys


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def parse_pe(data):
    pe_offset = u32(data, 0x3C)
    coff = pe_offset + 4
    section_count = u16(data, coff + 2)
    optional_size = u16(data, coff + 16)
    optional = coff + 20
    image_base = struct.unpack_from("<Q", data, optional + 24)[0]
    sections_offset = optional + optional_size

    sections = []
    for index in range(section_count):
        offset = sections_offset + index * 40
        sections.append(
            {
                "name": data[offset:offset + 8]
                .rstrip(b"\0")
                .decode("ascii"),
                "virtual_size": u32(data, offset + 8),
                "virtual_address": u32(data, offset + 12),
                "raw_size": u32(data, offset + 16),
                "raw_offset": u32(data, offset + 20),
            }
        )
    return image_base, sections


def file_to_rva(sections, file_offset):
    for section in sections:
        start = section["raw_offset"]
        end = start + section["raw_size"]
        if start <= file_offset < end:
            return section["virtual_address"] + file_offset - start
    raise ValueError(f"File offset 0x{file_offset:X} is outside sections")


def rva_to_file(sections, rva):
    for section in sections:
        start = section["virtual_address"]
        end = start + max(section["virtual_size"], section["raw_size"])
        if start <= rva < end:
            return section["raw_offset"] + rva - start
    raise ValueError(f"RVA 0x{rva:X} is outside sections")


def find_all(data, pattern):
    offset = 0
    while True:
        offset = data.find(pattern, offset)
        if offset < 0:
            return
        yield offset
        offset += 1


def main():
    executable_path, class_name = sys.argv[1:3]
    data = open(executable_path, "rb").read()
    image_base, sections = parse_pe(data)

    decorated = f".?AV{class_name}@@".encode("ascii") + b"\0"
    for name_offset in find_all(data, decorated):
        descriptor_offset = name_offset - 16
        descriptor_rva = file_to_rva(sections, descriptor_offset)
        descriptor_pattern = struct.pack("<I", descriptor_rva)

        for reference_offset in find_all(data, descriptor_pattern):
            col_offset = reference_offset - 12
            if col_offset < 0:
                continue

            signature, object_offset, cd_offset, type_rva, hierarchy_rva, self_rva = (
                struct.unpack_from("<IIIIII", data, col_offset)
            )
            try:
                col_rva = file_to_rva(sections, col_offset)
            except ValueError:
                continue

            if (
                signature != 1
                or type_rva != descriptor_rva
                or self_rva != col_rva
            ):
                continue

            col_va_pattern = struct.pack("<Q", image_base + col_rva)
            for col_pointer_offset in find_all(data, col_va_pattern):
                vtable_rva = file_to_rva(sections, col_pointer_offset + 8)
                print(
                    f"{class_name}: descriptor_rva=0x{descriptor_rva:X} "
                    f"col_rva=0x{col_rva:X} "
                    f"object_offset=0x{object_offset:X} "
                    f"vtable_rva=0x{vtable_rva:X}"
                )


if __name__ == "__main__":
    main()
