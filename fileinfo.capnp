@0xab49860d09250a02;

struct FileInfo {
    size @0 :UInt64;
    modifiedTime @1 :UInt64;
    path @2 :Text;
    enum Type {
        file @0;
        dir @1;
        symlink @2;
    }
    type @3 :Type;
    linkPath @4 :Text;
}
