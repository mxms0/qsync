using SerializedFileInfo = std::vector<uint8_t>;

typedef class QsyncServer QsyncServer;

struct ReceivedFileInfo {
    SerializedFileInfo FileInfo;
    QsyncServer* Server;
};

bool
DoesFileNeedUpdate(
    const std::filesystem::path& Destination,
    const FileInfo::Reader& File);

typedef void (FileResultsCallback)(
    uint64_t Id,
    SerializedFileInfo&& File);

bool
FindFiles(
    const std::string& Root,
    std::function<FileResultsCallback> Callback);
