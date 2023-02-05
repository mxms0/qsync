using SerializedFileInfo = std::vector<uint8_t>;

typedef void FileResultsCallback(
    const kj::Vector<SerializedFileInfo>& Files);

bool
FindFiles(
    const std::string& Root,
    std::function<FileResultsCallback> Callback);
