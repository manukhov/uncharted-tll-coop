import pefile, struct, sys
BIN="/mnt/c/Program Files (x86)/Steam/steamapps/common/Uncharted Legacy of Thieves Collection/tll.exe"
pe=pefile.PE(BIN, fast_load=True); base=pe.OPTIONAL_HEADER.ImageBase; raw=pe.__data__
secs=[(s.Name.rstrip(b'\0').decode(errors='ignore'), base+s.VirtualAddress, base+s.VirtualAddress+s.Misc_VirtualSize, s.PointerToRawData) for s in pe.sections]
target=int(sys.argv[1],16); tb=struct.pack("<Q", target)
for n,a,b,o in secs:
    if n in ('.rdata','.data','_RDATA'):
        data=raw[o:o+(b-a)]; i=0
        while True:
            j=data.find(tb,i)
            if j<0: break
            print(f"{n} @0x{a+j:x} (rva 0x{a+j-base:x})"); i=j+1
