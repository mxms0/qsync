using SerializedFileInfo = std::vector<uint8_t>;

typedef void (FileResultsCallback)(
    uint64_t Id,
    SerializedFileInfo&& File);

bool
FindFiles(
    const std::string& Root,
    std::function<FileResultsCallback> Callback);
