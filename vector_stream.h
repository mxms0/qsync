// Based on implementation by Niek J. Bouman.

class VectorStream: public kj::BufferedOutputStream {
public:
    explicit VectorStream(std::vector<uint8_t>& Vec)
        : InnerVec(Vec) {}

    void write(const void* Buffer, size_t Size)
    {
        if (Buffer != nullptr) {
            // std::cout << "wrote " << Size << " bytes normally\n";
            InnerVec.insert(InnerVec.end(), (uint8_t*)Buffer, (uint8_t*)Buffer + Size);
        }
    }

    kj::ArrayPtr<kj::byte> getWriteBuffer()
    {
        // std::cout << "getWriteBuffer(" << BytesWritten << ") " << InnerVec.capacity() - BytesWritten <<"\n";
        return kj::ArrayPtr<kj::byte>(nullptr, 0ull); // disable this from being used by having a zero-length array
    }
private:
    std::vector<uint8_t>& InnerVec;
};
