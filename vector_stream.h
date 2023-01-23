// Based on implementation by Niek J. Bouman.

class VectorStream: public kj::BufferedOutputStream {
public:
    explicit VectorStream(std::vector<uint8_t>& Vec);

    void write(const void* Buffer, size_t Size) override;
    // Always writes the full size.  Throws exception on error.

    inline
    kj::ArrayPtr<kj::byte> getWriteBuffer() override { return kj::ArrayPtr<kj::byte>(InnerVec.data() + BytesWritten, available()); }

    inline
    size_t available() { return InnerVec.size() - BytesWritten; }
private:
    std::vector<uint8_t>& InnerVec;
    size_t BytesWritten;
};