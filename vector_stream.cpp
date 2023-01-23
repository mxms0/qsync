#include "qsync.h"

VectorStream::VectorStream(std::vector<uint8_t>& Vec)
    : InnerVec(Vec), BytesWritten(0) {}

void VectorStream::write(const void* Buffer, size_t Size)
{
    if (Buffer == InnerVec.data() + BytesWritten) {
        BytesWritten += Size;
    } else {
        if (Size > available()) {
            InnerVec.resize(InnerVec.size() + Size);
        }
        memcpy(InnerVec.data() + BytesWritten, Buffer, Size);
        BytesWritten += Size;
    }
}
