// Based on implementation by Niek J. Bouman.

class VectorStream: public kj::BufferedOutputStream {
public:
    explicit VectorStream(std::vector<uint8_t>& Vec)
        : InnerVec(Vec) {}

    void write(const void* Buffer, size_t Size)
    {
        if (Buffer != nullptr) {
            // This is UB when `getWriteBuffer` returns non-null
            InnerVec.insert(InnerVec.end(), (uint8_t*)Buffer, (uint8_t*)Buffer + Size);
        }
    }

    kj::ArrayPtr<kj::byte> getWriteBuffer()
    {
        return kj::ArrayPtr<kj::byte>(nullptr, 0ull); // disable this from being used by having a zero-length array
        // return kj::ArrayPtr<kj::byte>(InnerVec.data() + InnerVec.size(), InnerVec.capacity() - InnerVec.size());
    }
private:
    std::vector<uint8_t>& InnerVec;
};
