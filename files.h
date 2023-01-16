
typedef void FileResultsCallback(
    const std::vector<std::filesystem::path>& Files,
    const std::vector<std::filesystem::path>& Directories);

bool
FindFiles(
    const std::string& Root,
    FileResultsCallback& Callback);