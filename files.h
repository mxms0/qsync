using SerializedFileInfo = std::vector<uint8_t>;

typedef void FileResultsCallback(
    const std::vector<SerializedFileInfo>& Files);

bool
FindFiles(
    const std::string& Root,
    FileResultsCallback& Callback);